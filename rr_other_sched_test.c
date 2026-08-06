/*
 * RR vs OTHER scheduling test on same core
 * RR thread:  SCHED_RR max priority, 100us sleep every 1000ms, rest computes
 * OTHER thread: SCHED_OTHER, wakes every 1ms, counts schedule hits per second
 * Purpose: observe RR time-quantum behavior vs OTHER on shared core
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
static atomic_int rr_ready    = ATOMIC_VAR_INIT(0);
static atomic_int other_ready = ATOMIC_VAR_INIT(0);

/* ── RR thread (SCHED_RR max priority) ─────────────── */
static void *test_rr_thread(void *arg)
{
    (void)arg;
    cpu_set_t cpuset;
    struct sched_param param;

    /* bind to core 20 */
    CPU_ZERO(&cpuset);
    CPU_SET(20, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("[RR] pthread_setaffinity_np");
        return NULL;
    }

    /* set SCHED_RR max priority */
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &param) != 0) {
        perror("[RR] pthread_setschedparam");
        return NULL;
    }

    int core = sched_getcpu();
    printf("[RR_T0] started  policy=SCHED_RR  priority=%d  affinity=20  actual_core=%d\n",
           param.sched_priority, core);

    atomic_store(&rr_ready, 1);

    /*
     * Work pattern: every 1000ms sleep 100us, rest do computation.
     * SCHED_RR has a time quantum; when quantum expires the scheduler
     * moves this thread to the back of the RR queue — unlike SCHED_FIFO
     * which runs until it yields or blocks.
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

    printf("[RR_T0] exiting\n");
    return NULL;
}

/* ── OTHER thread (SCHED_OTHER) ─────────────────────── */
static void *test_other_thread(void *arg)
{
    (void)arg;
    cpu_set_t cpuset;
    struct sched_param param;
    int policy;

    /* bind to core 20 */
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
        printf("[RR_T1] started  policy=%d (SCHED_OTHER=%d)  affinity=20  actual_core=%d\n",
               policy, SCHED_OTHER, core);
    }

    atomic_store(&other_ready, 1);

    /*
     * Pattern: wake every 1 ms, count one schedule hit.
     * Re-align to next 1ms boundary from actual current time.
     */
    struct timespec now;

    while (keep_running) {
        atomic_fetch_add(&other_sched_count, 1);

        struct timespec next_wake;
        clock_gettime(CLOCK_MONOTONIC, &now);
        next_wake.tv_sec  = now.tv_sec;
        next_wake.tv_nsec = ((now.tv_nsec / NSEC_PER_MSEC) + 1) * NSEC_PER_MSEC;
        if (next_wake.tv_nsec >= NSEC_PER_SEC) {
            next_wake.tv_sec++;
            next_wake.tv_nsec -= NSEC_PER_SEC;
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);
    }

    printf("[RR_T1] exiting\n");
    return NULL;
}

/* ── statistics reporter ─────────────────────────────── */
static void *reporter_thread(void *arg)
{
    (void)arg;

    /* wait until both threads confirmed on core 20 */
    while (!atomic_load(&rr_ready) || !atomic_load(&other_ready)) {
        sched_yield();
    }
    printf("[REPORTER] both threads confirmed on core 20, starting stats\n\n");

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
    pthread_t t_rr, t_other, t_reporter;

    printf("=== RR vs OTHER scheduling test ===\n");
    printf("Both threads pinned to CPU 20 (RR_T0=SCHED_RR, RR_T1=SCHED_OTHER)\n");
    printf("RR:    SCHED_RR max priority, 100us sleep every 1000ms, compute rest\n");
    printf("OTHER: SCHED_OTHER, wakes every 1ms\n");
    printf("Watch OTHER_sched_count: ratio near 10%% => OTHER gets ~10%% CPU time;\n");
    printf("         near 0%%  => RR starves OTHER almost completely.\n\n");

    pthread_create(&t_rr,      NULL, test_rr_thread,    NULL);
    pthread_create(&t_other,   NULL, test_other_thread, NULL);
    pthread_create(&t_reporter, NULL, reporter_thread, NULL);

    pthread_setname_np(t_rr,    "RR_T0");
    pthread_setname_np(t_other, "RR_T1");

    /* run for 30 seconds */
    sleep(30);
    keep_running = 0;

    pthread_join(t_rr,      NULL);
    pthread_join(t_other,   NULL);
    pthread_join(t_reporter, NULL);

    printf("\n=== test finished ===\n");
    return 0;
}
