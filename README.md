# User-Level Thread Library (uthreads)

A C library for managing user-level threads, featuring a custom preemptive scheduler, signal-based context switching, and full thread lifecycle management.

## Features

- Create and manage user-level threads
- Handle thread states: ready, running, blocked, and terminated
- Timer-based preemptive scheduling using signals
- Custom round-robin scheduler with fixed time quantum
- FIFO ready queue for thread scheduling

## Compilation

This project builds as a static library.

To compile (note the required `gnu17` standard due to inline assembly):

```bash
gcc -Wall -Wextra -std=gnu17 -c uthreads.c ready_queue.c
ar rcs libuthreads.a uthreads.o ready_queue.o
```


## Files

- `uthreads.c`, `uthreads.h` – thread lifecycle logic, context switching, and signal handling  
- `ready_queue.c`, `ready_queue.h` – FIFO ready queue implementation for thread scheduling  
- `.devcontainer/` – development environment setup for Ubuntu with GCC  