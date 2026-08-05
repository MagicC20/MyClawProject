/*
 * FIFO vs OTHER scheduling test on same core
 * TEST_FIFO:  SCHED_FIFO max priority, sleeps 100us every 1000ms, rest computes
 * TEST_OTHER: SCHED_OTHER, wakes every 1ms, counts schedule hits per second
 * Purpose: verify if OTHER thread can be scheduled when sharing core with FIFO RT thread
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

#define NSEC_PER_SEC  1000000000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_USEC 1000L

/* ── shared counter & sync flags ─────────────────────── */
static atomic_int other_sched_count = ATOMIC_VAR_INIT(0);
static volatile int keep_running = 1;
static atomic_int fifo_ready  = ATOMIC_VAR_INIT(0);
static atomic_int other_ready = ATOMIC_VAR_INIT(0);

/* ── TEST_FIFO thread ───────────────────────────────── */
static void *test_fifo_thread(void *arg)
{
    (void)arg;
    cpu_set_t cpuset;
    struct sched_param param;

    /* set CPU affinity: core 0 only */
    CPU_ZERO(&cpuset);
    CPU_SET(20, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("[FIFO] pthread_setaffinity_np");
        return NULL;
    }

    /* set SCHED_FIFO max priority */
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        perror("[FIFO] pthread_setschedparam");
        return NULL;
    }

    /* verify actually running on core 0 (scheduler may have hysteresis) */
    int core = sched_getcpu();
    printf("[FIFO] started  priority=%d  affinity_set=20  actual_core=%d\n",
           param.sched_priority, core);

    /* signal that TEST_FIFO is initialised and on core 0 */
    atomic_store(&fifo_ready, 1);

    /*
     * Work pattern: every 1000ms sleep 100us, rest (999.9ms) do computation.
     */
    struct timespec ts_start, ts_now;
    long sleep_interval_ns = 100 * NSEC_PER_USEC;
    long cycle_ns = 1000 * NSEC_PER_MSEC;

    while (keep_running) {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        /* ── 100 us sleep ── */
        usleep(100);

        /* ── computation for remaining ~999900 us ── */
        while (1) {
            volatile unsigned long long dummy = 0;
            for (int i = 0; i < 5000; i++) {
                dummy++;
            }
            clock_gettime(CLOCK_MONOTONIC, &ts_now);
            long elapsed_ns = (ts_now.tv_sec - ts_start.tv_sec) * NSEC_PER_SEC
                            + (ts_now.tv_nsec - ts_start.tv_nsec);
            if (elapsed_ns >= cycle_ns - sleep_interval_ns)
                break;
        }
    }

    printf("[FIFO] exiting\n");
    return NULL;
}

/* ── TEST_OTHER thread ───────────────────────────────── */
static void *test_other_thread(void *arg)
{
    (void)arg;
    cpu_set_t cpuset;
    struct sched_param param;
    int policy;

    /* set CPU affinity: core 0 only */
    CPU_ZERO(&cpuset);
    CPU_SET(20, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("[OTHER] pthread_setaffinity_np");
        return NULL;
    }

    /* confirm SCHED_OTHER (default) */
    memset(&param, 0, sizeof(param));
    if (pthread_getschedparam(pthread_self(), &policy, &param) != 0) {
        perror("[OTHER] pthread_getschedparam");
    } else {
        int core = sched_getcpu();
        printf("[OTHER] started  policy=%d (SCHED_OTHER=%d)  affinity_set=20  actual_core=%d\n",
               policy, SCHED_OTHER, core);
    }

    /* signal that TEST_OTHER is initialised and on core 0 */
    atomic_store(&other_ready, 1);

    /*
     * Pattern: wake every 1 ms, count one schedule hit.
     * Re-align to next 1ms boundary based on ACTUAL current time,
     * not the intended wake time (which may have passed due to FIFO delay).
     */
    struct timespec now;

    while (keep_running) {
        /* increment counter whenever this thread gets scheduled in */
        atomic_fetch_add(&other_sched_count, 1);

        /* calculate next 1 ms boundary from actual current time */
        struct timespec next_wake;
        clock_gettime(CLOCK_MONOTONIC, &now);
        next_wake.tv_sec = now.tv_sec;
        next_wake.tv_nsec = ((now.tv_nsec / NSEC_PER_MSEC) + 1) * NSEC_PER_MSEC;
        if (next_wake.tv_nsec >= NSEC_PER_SEC) {
            next_wake.tv_sec++;
            next_wake.tv_nsec -= NSEC_PER_SEC;
        }

        /* sleep until next 1 ms tick */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);
    }

    printf("[OTHER] exiting\n");
    return NULL;
}

/* ── statistics reporter ─────────────────────────────── */
static void *reporter_thread(void *arg)
{
    (void)arg;

    /* wait until both TEST_FIFO and TEST_OTHER are confirmed on core 0 */
    while (!atomic_load(&fifo_ready) || !atomic_load(&other_ready)) {
        sched_yield();
    }
    printf("[REPORTER] both threads confirmed on core 0, starting stats\n\n");

    time_t next_sec;
    time(&next_sec);
    next_sec++;

    while (keep_running) {
        struct timespec ts;
        ts.tv_sec = next_sec;
        ts.tv_nsec = 0;
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        int count = atomic_exchange(&other_sched_count, 0);
        printf("[STAT]  OTHER_sched_count=%-6d  ratio=%6.2f%%  (1000 theoretical 1ms ticks/s)\n",
               count, count * 100.0 / 1000.0);

        next_sec++;
    }
    return NULL;
}

/* ── main ────────────────────────────────────────────── */
int main(void)
{
    pthread_t t_fifo, t_other, t_reporter;

    printf("=== FIFO vs OTHER scheduling test ===\n");
    printf("Both threads pinned to CPU 20\n");
    printf("FIFO:  SCHED_FIFO max priority, 100us sleep every 1000ms, compute rest\n");
    printf("OTHER: SCHED_OTHER, wakes every 1ms\n");
    printf("Watch OTHER_sched_count: ratio near 10%% => OTHER gets ~10%% CPU time;\n");
    printf("         near 0%% => FIFO starves OTHER almost completely.\n\n");

    pthread_create(&t_fifo,     NULL, test_fifo_thread,   NULL);
    pthread_create(&t_other,    NULL, test_other_thread,  NULL);
    pthread_create(&t_reporter, NULL, reporter_thread,    NULL);

    /* set thread names in main after pthread_create */
    pthread_setname_np(t_fifo,  "TEST_FIFO");
    pthread_setname_np(t_other, "TEST_OTHER");

    /* run for 30 seconds */
    sleep(30);
    keep_running = 0;

    pthread_join(t_fifo, NULL);
    pthread_join(t_other, NULL);
    pthread_join(t_reporter, NULL);

    printf("\n=== test finished ===\n");
    return 0;
}
