/*
 * FIFO scheduling competition test
 * Test1: Multiple SCHED_FIFO threads with SAME priority competing on one core
 * Test2: Multiple SCHED_FIFO threads with DIFFERENT priorities competing on one core
 *
 * Each thread increments its own counter on every scheduling hit.
 * A reporter prints the distribution every second.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <math.h>

#define NSEC_PER_SEC   1000000000L
#define NSEC_PER_MSEC  1000000L
#define NSEC_PER_USEC  1000L

/* ── configurable parameters ─────────────────────────── */
#define DEFAULT_N_THREADS     4
#define DEFAULT_SLEEP_US      100   /* each thread sleeps 100us per cycle */
#define DEFAULT_CYCLE_US      1000  /* each thread cycle: 1000us */
#define DEFAULT_TEST_DURATION  30    /* seconds */

/* ── shared state ───────────────────────────────────── */
static volatile int keep_running = 1;
static atomic_int test_ready = ATOMIC_VAR_INIT(0);

/* one counter per thread */
typedef struct {
    int                  id;         /* thread index */
    const char          *name;       /* thread name */
    int                  priority;    /* SCHED_FIFO priority, 0 = max */
    atomic_int          *sched_count;/* pointer to shared counter */
} thread_arg_t;

/* ── worker thread: busy-wait then sleep in cycle ──── */
static void *worker_thread(void *arg)
{
    thread_arg_t *a = arg;

    pthread_setname_np(pthread_self(), a->name);

    /* wait for all threads to be created */
    while (!atomic_load(&test_ready))
        sched_yield();

    /*
     * Work pattern:
     *   busy-wait for (cycle_us - sleep_us), then sleep for sleep_us.
     * This releases the CPU so other FIFO threads can get scheduled.
     */
    struct timespec ts_start, ts_now;
    long sleep_ns    = DEFAULT_SLEEP_US * NSEC_PER_USEC;
    long cycle_us    = DEFAULT_CYCLE_US;

    while (keep_running) {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        /* ── busy-wait for (cycle_us - sleep_us) ── */
        while (1) {
            volatile unsigned long long dummy = 0;
            for (int i = 0; i < 2000; i++)
                dummy++;
            clock_gettime(CLOCK_MONOTONIC, &ts_now);
            long elapsed_ns = (ts_now.tv_sec - ts_start.tv_sec) * NSEC_PER_SEC
                            + (ts_now.tv_nsec - ts_start.tv_nsec);
            if (elapsed_ns >= cycle_us * NSEC_PER_USEC - sleep_ns)
                break;
        }

        /* ── sleep for sleep_us ── */
        usleep(DEFAULT_SLEEP_US);

        /* record one scheduling hit */
        atomic_fetch_add(a->sched_count, 1);
    }

    printf("[%s] exiting\n", a->name);
    return NULL;
}

/* ── reporter thread ────────────────────────────────── */
static void *reporter_thread(void *arg)
{
    (void)arg;
    int n = DEFAULT_N_THREADS;
    atomic_int *counters = (atomic_int *)arg;

    while (!atomic_load(&test_ready))
        sched_yield();

    time_t next_sec;
    time(&next_sec);
    next_sec++;

    while (keep_running) {
        struct timespec ts = { .tv_sec = next_sec, .tv_nsec = 0 };
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        /* snapshot and reset all counters */
        int64_t total = 0;
        int snap[64] = {0};
        for (int i = 0; i < n; i++) {
            snap[i] = atomic_exchange(&counters[i], 0);
            total += snap[i];
        }

        /* print distribution */
        printf("[STAT]  total=%-6lld", (long long)total);
        for (int i = 0; i < n; i++)
            printf("  T%d=%-5d", i, snap[i]);
        if (total > 0) {
            printf("  |");
            for (int i = 0; i < n; i++)
                printf("  T%d=%.1f%%", i, snap[i] * 100.0 / total);
        }
        printf("\n");

        next_sec++;
    }
    return NULL;
}

/* ── create FIFO threads with given priorities ───────── */
static void create_fifo_threads(pthread_t *threads, thread_arg_t *args,
                                atomic_int *counters, int n,
                                int base_priority, int step)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);

    for (int i = 0; i < n; i++) {
        args[i].id         = i;
        args[i].sched_count = &counters[i];
        if (step == 0) {
            /* same priority */
            args[i].priority = base_priority;
            if (asprintf((char **)&args[i].name, "FIFO_SAME_P%d", base_priority) < 0)
                args[i].name = "FIFO_SAME_P";
        } else {
            /* descending priority */
            args[i].priority = base_priority - i * step;
            if (asprintf((char **)&args[i].name, "FIFO_P%d", args[i].priority) < 0)
                args[i].name = "FIFO_P";
        }

        pthread_create(&threads[i], NULL, worker_thread, &args[i]);

        /* set CPU affinity */
        pthread_setaffinity_np(threads[i], sizeof(cpu_set_t), &cpuset);

        /* set SCHED_FIFO priority */
        struct sched_param param;
        memset(&param, 0, sizeof(param));
        param.sched_priority = args[i].priority;
        pthread_setschedparam(threads[i], SCHED_FIFO, &param);
    }
}

/* ── Test 1: same priority ───────────────────────────── */
static void run_test_same_priority(int n)
{
    printf("\n");
    printf("========================================================\n");
    printf("Test 1: %d x SCHED_FIFO threads, SAME priority (%d)\n",
           n, sched_get_priority_max(SCHED_FIFO));
    printf("All threads pinned to CPU 0, each sleeps %dus every %dus\n",
           DEFAULT_SLEEP_US, DEFAULT_CYCLE_US);
    printf("========================================================\n\n");

    pthread_t *threads    = calloc(n, sizeof(pthread_t));
    thread_arg_t *args   = calloc(n, sizeof(thread_arg_t));
    atomic_int *counters = calloc(n, sizeof(atomic_int));

    create_fifo_threads(threads, args, counters, n,
                        sched_get_priority_max(SCHED_FIFO), 0);

    pthread_t reporter;
    pthread_create(&reporter, NULL, reporter_thread, counters);

    atomic_store(&test_ready, 1);

    sleep(DEFAULT_TEST_DURATION);
    keep_running = 0;

    pthread_join(reporter, NULL);
    for (int i = 0; i < n; i++) pthread_join(threads[i], NULL);

    for (int i = 0; i < n; i++) free((char *)args[i].name);
    free(threads); free(args); free(counters);
}

/* ── Test 2: different priorities ──────────────────── */
static void run_test_different_priority(int n)
{
    int max_prio = sched_get_priority_max(SCHED_FIFO);
    int min_prio = sched_get_priority_min(SCHED_FIFO);
    int step = (max_prio - min_prio) / (n - 1);

    printf("\n");
    printf("========================================================\n");
    printf("Test 2: %d x SCHED_FIFO threads, DIFFERENT priorities\n", n);
    printf("P0=%d (highest) ... P%d=%d (lowest), step=%d\n",
           max_prio, n-1, min_prio, step);
    printf("All threads pinned to CPU 0, each sleeps %dus every %dus\n",
           DEFAULT_SLEEP_US, DEFAULT_CYCLE_US);
    printf("========================================================\n\n");

    pthread_t *threads    = calloc(n, sizeof(pthread_t));
    thread_arg_t *args   = calloc(n, sizeof(thread_arg_t));
    atomic_int *counters = calloc(n, sizeof(atomic_int));

    create_fifo_threads(threads, args, counters, n, max_prio, step);

    pthread_t reporter;
    pthread_create(&reporter, NULL, reporter_thread, counters);

    atomic_store(&test_ready, 1);

    sleep(DEFAULT_TEST_DURATION);
    keep_running = 0;

    pthread_join(reporter, NULL);
    for (int i = 0; i < n; i++) pthread_join(threads[i], NULL);

    for (int i = 0; i < n; i++) free((char *)args[i].name);
    free(threads); free(args); free(counters);
}

/* ── main ────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int n = DEFAULT_N_THREADS;
    if (argc >= 2) n = atoi(argv[1]);
    if (n < 2) n = 2;
    if (n > 64) n = 64;

    printf("=== FIFO scheduling competition test ===");
    printf(" (n=%d, sleep=%dus, cycle=%dus, duration=%ds)\n",
           n, DEFAULT_SLEEP_US, DEFAULT_CYCLE_US, DEFAULT_TEST_DURATION);

    printf("\nRun Test 1: same priority\n");
    keep_running = 1;
    atomic_store(&test_ready, 0);
    run_test_same_priority(n);

    printf("\nRun Test 2: different priorities\n");
    keep_running = 1;
    atomic_store(&test_ready, 0);
    run_test_different_priority(n);

    printf("\n=== all tests finished ===\n");
    return 0;
}
