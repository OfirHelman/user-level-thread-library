#define _POSIX_C_SOURCE 200112L
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <sys/types.h>

#include "uthreads.h"
#include "ready_queue.h"

// These indices refer to the positions in the jmp_buf where the
// stack pointer (SP) and program counter (PC) are stored.
#define JB_SP 6
#define JB_PC 7

// Defines a type for memory addresses.
typedef unsigned long address_t;
address_t translate_address(address_t addr);

// The Thread Control Block (TCB) array
static thread_t threads[MAX_THREAD_NUM];

// Statically allocated memory for thread stacks. stacks[i] serves as the stack for thread with tid i.
static char stacks[MAX_THREAD_NUM][STACK_SIZE];

// The ID of the currently running thread. Initialized to 0 for the main thread.
static int current_tid = 0;

// System-wide quantum counter. Starts at 1 after uthread_init().
static int total_quantums = 0;

// The length of a quantum in microseconds, as set by the user in uthread_init().
static int quantum_usecs = 0;

/* Translates an address to the architecture-specific format required for manually setting up a thread's sigjmp_buf context. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor %%fs:0x30, %0\n"
                 "rol $0x11, %0\n"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}

/* Initializes a thread's context by populating its sigjmp_buf with the initial stack pointer and entry point. */
void setup_thread(int tid, char *stack, thread_entry_point entry_point)
{
    address_t sp = (address_t)(stack + STACK_SIZE - sizeof(address_t));
    address_t pc = (address_t)(entry_point);

    // Save the current context to serve as a template for the new thread's context.
    sigsetjmp(threads[tid].env, 1);
    threads[tid].env->__jmpbuf[JB_SP] = translate_address(sp);
    threads[tid].env->__jmpbuf[JB_PC] = translate_address(pc);
    sigemptyset(&threads[tid].env->__saved_mask);
}

/* Initializes the user-level threads library, setting up the main thread and the timer-based scheduler. */
int uthread_init(int usecs)
{
    if (usecs <= 0)
    {
        fprintf(stderr, "quantum_usecs must be positive\n");
        return -1;
    }

    quantum_usecs = usecs;
    total_quantums = 1;
    current_tid = 0;

    // Initialize the thread table
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
        threads[i].tid = i;
        threads[i].state = THREAD_UNUSED;
        threads[i].quantums = 0;
    }

    // Setup the main thread (tid 0)
    threads[0].state = THREAD_RUNNING;
    threads[0].quantums = 1;
    sigsetjmp(threads[0].env, 1);
    sigemptyset(&threads[0].env->__saved_mask);

    init_ready_queue();

    // Configure the timer signal handler
    struct sigaction sa;
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        fprintf(stderr, "system error: sigaction failed\n");
        exit(1);
    }

    // Start the virtual timer
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        fprintf(stderr, "system error: setitimer failed\n");
        exit(1);
    }

    return 0;
}

/* Creates a new thread that will run the given entry point function. */
int uthread_spawn(thread_entry_point entry_point)
{
    if (entry_point == NULL)
    {
        fprintf(stderr, "thread entry point is NULL\n");
        return -1;
    }

    // Find an available thread ID, starting from 1 to protect the main thread (tid 0).
    int tid = -1;
    for (int i = 1; i < MAX_THREAD_NUM; ++i)
    {
        if (threads[i].state == THREAD_UNUSED)
        {
            tid = i;
            break;
        }
    }

    if (tid == -1)
    {
        fprintf(stderr, "no available tid\n");
        return -1;
    }

    // Initialize the new thread's TCB
    threads[tid].tid = tid;
    threads[tid].state = THREAD_READY;
    threads[tid].quantums = 0;
    threads[tid].sleep_until = 0;
    threads[tid].entry = entry_point;

    char *stack = stacks[tid];
    setup_thread(tid, stack, entry_point);

    enqueue_ready(tid);

    return tid;
}

/* Selects the next thread to run from the ready queue and performs a context switch. */
void schedule_next()
{
    int prev_tid = current_tid;
    int next_tid = dequeue_ready();

    // If no threads are ready, the current thread simply continues to run.
    if (next_tid == -1)
    {
        return;
    }

    // If the thread was preempted by the timer, move it back to the ready queue.
    if (threads[prev_tid].state == THREAD_RUNNING)
    {
        threads[prev_tid].state = THREAD_READY;
        enqueue_ready(prev_tid);
    }

    threads[next_tid].state = THREAD_RUNNING;
    threads[next_tid].quantums++;
    current_tid = next_tid;
    context_switch(&threads[prev_tid], &threads[next_tid]);
}

/* Saves the context of the previous thread and jumps to the context of the next thread. */
void context_switch(thread_t *prev, thread_t *next)
{
    // Save the current context. Returns 0 on the initial call.
    // When the 'prev' thread is resumed later, siglongjmp will cause this to return a non-zero value.
    int ret_val = sigsetjmp(prev->env, 1);
    if (ret_val == 0)
    {
        // The timer signal was blocked by the handler. We must unblock it before
        // switching to the next thread to allow it to be preempted in the future.
        sigset_t set;
        if (sigemptyset(&set) == -1 || sigaddset(&set, SIGVTALRM) == -1)
        {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1)
        {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }

        // Jump to the next thread's context; this call does not return.
        siglongjmp(next->env, 1);
    }
}

int uthread_get_tid()
{
    return current_tid;
}

int uthread_get_total_quantums()
{
    return total_quantums;
}

int uthread_get_quantums(int tid)
{
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED)
    {
        return -1;
    }
    return threads[tid].quantums;
}

/* Terminates the thread with the given ID. */
int uthread_terminate(int tid)
{
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED)
    {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }

    // Case 1: Terminating the main thread (tid 0) terminates the entire process.
    if (tid == 0)
    {
        exit(0);
    }

    // Case 2: A thread terminates itself.
    else if (tid == current_tid)
    {
        threads[tid].state = THREAD_UNUSED;
        int next_tid = dequeue_ready();

        // If no other threads are ready, terminate the process.
        if (next_tid == -1)
        {
            exit(0);
        }

        current_tid = next_tid;
        threads[next_tid].state = THREAD_RUNNING;
        threads[next_tid].quantums++;
        siglongjmp(threads[next_tid].env, 1);
    }

    // Case 3: Terminating another thread.
    else
    {
        // If the thread is in the ready queue, remove it.
        if (threads[tid].state == THREAD_READY)
        {
            remove_from_ready_queue(tid);
        }

        // Mark the thread's TCB as unused.
        threads[tid].state = THREAD_UNUSED;
    }

    return 0;
}

/* Blocks the thread with the given ID, moving it to the BLOCKED state. */
int uthread_block(int tid)
{
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED)
    {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }
    // The main thread (tid 0) cannot be blocked.
    if (tid == 0)
    {
        fprintf(stderr, "thread library error: cannot block main thread\n");
        return -1;
    }

    // Blocking an already blocked thread has no effect.
    if (threads[tid].state == THREAD_BLOCKED)
    {
        return 0;
    }

    if (threads[tid].state == THREAD_READY)
    {
        remove_from_ready_queue(tid);
    }

    threads[tid].state = THREAD_BLOCKED;
    return 0;
}

/* Moves a thread from the BLOCKED state to the READY state. */
int uthread_resume(int tid)
{
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED)
    {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }

    // No effect if already RUNNING or READY.
    if (threads[tid].state == THREAD_READY || threads[tid].state == THREAD_RUNNING)
    {
        return 0;
    }

    if (threads[tid].state == THREAD_BLOCKED)
    {
        threads[tid].state = THREAD_READY;
        enqueue_ready(tid);
        return 0;
    }

    return 0;
}

/*
 * Blocks the currently running thread for a given number of quantums.
 * The main thread (tid 0) cannot call this function.
 */
int uthread_sleep(int num_quantums)
{
    if (current_tid == 0)
    {
        fprintf(stderr, "thread library error: main thread cannot sleep\n");
        return -1;
    }

    if (num_quantums <= 0)
    {
        fprintf(stderr, "thread library error: invalid sleep duration\n");
        return -1;
    }

    threads[current_tid].sleep_until = total_quantums + num_quantums;
    threads[current_tid].state = THREAD_BLOCKED;

    schedule_next();

    return 0;
}

/*
 * Timer signal handler.
 * Registered as the handler for timer signals (SIGVTALRM).
 * This function updates global quantum counters, wakes up sleeping threads,
 * and initiates a scheduling decision when a quantum expires.
 */
void timer_handler(int signum)
{
    (void)signum;

    // Block SIGVTALRM to prevent preemption during critical updates.
    // This ensures the timer signal doesn't interrupt while updating global counters
    // or modifying thread states and queues, preventing state corruption.
    sigset_t set;
    if (sigemptyset(&set) == -1 || sigaddset(&set, SIGVTALRM) == -1)
    {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
    {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    total_quantums++;
    threads[current_tid].quantums++;

    // Iterate through all threads to wake up sleeping threads whose sleep duration has expired.
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (threads[i].state == THREAD_BLOCKED &&
            threads[i].sleep_until > 0 &&
            threads[i].sleep_until <= total_quantums)
        {

            threads[i].sleep_until = 0;
            threads[i].state = THREAD_READY;
            enqueue_ready(i);
        }
    }

    schedule_next();
}
