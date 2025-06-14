#define _POSIX_C_SOURCE 200112L
#include "ready_queue.h"
#include "uthreads.h"  

// Static array to implement the circular ready queue for thread IDs.
static int ready_queue[MAX_THREAD_NUM];
static int front = 0;
static int back = 0;

/*
 * Adds a thread ID to the end of the ready queue.
 * Assumes the queue is not full; capacity is handled by uthread_spawn. 
 */
void enqueue_ready(int tid) {
    ready_queue[back] = tid;
    back = (back + 1) % MAX_THREAD_NUM;
}

/*
 * Removes and returns the thread ID from the front of the ready queue.
 * Returns -1 if the queue is empty.
 */
int dequeue_ready(void) {
    if (front == back) return -1;
    int tid = ready_queue[front];
    front = (front + 1) % MAX_THREAD_NUM;
    return tid;
}

/*
 * Initializes (resets) the ready queue to an empty state.
 */
void init_ready_queue(void) {
    front = 0;
    back = 0;
}

/*
 * Removes a specific thread ID from the ready queue.
 * This operation rebuilds the queue by copying all other elements.
 */
void remove_from_ready_queue(int tid) {
    // Temporary queue to hold elements not being removed.
    int new_queue[MAX_THREAD_NUM];
    int new_back = 0;

    // Iterate through the current queue and copy elements that are not 'tid'.
    while (front != back) {
        int current = ready_queue[front];
        front = (front + 1) % MAX_THREAD_NUM;
        if (current != tid) {
            new_queue[new_back++] = current;
        }
    }

    // Rebuild the original ready_queue with elements from the temporary queue.
    for (int i = 0; i < new_back; ++i) {
        ready_queue[i] = new_queue[i];
    }
    // Reset front to start of the rebuilt queue.
    front = 0;
    // Set back to the end of the rebuilt queue.
    back = new_back;
}

