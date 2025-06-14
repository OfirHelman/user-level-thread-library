#ifndef READY_QUEUE_H
#define READY_QUEUE_H

void enqueue_ready(int tid);
int dequeue_ready(void);
void init_ready_queue(void);
void remove_from_ready_queue(int tid);

#endif
