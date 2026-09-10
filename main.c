// #include "scheduler.c"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <semaphore.h>
#if defined(__APPLE__) || defined(__MACH__)
    #include <dispatch/dispatch.h>
    #define sem_t dispatch_semaphore_t

    int sem_init(sem_t* s, int pshared, unsigned int value) {
        *s = dispatch_semaphore_create(value);
        return *s;
    }

    #define sem_close(s) dispatch_release(s)
#endif

typedef struct ThreadContext {
    int id;
    int priority;
    bool ran_this_tick;
    sem_t semaphore;
    
} ProritySlot;

// 1. fork, join
#define MAX_THREAD_COUNT 1024
ProritySlot thread_priorities[MAX_THREAD_COUNT];

void scheduler() {
    for (int i = 0; i < MAX_THREAD_COUNT; i++) {
        pthread_create()
    }
}

int main() {
    sem_t s;
    sem_init(&s, 0, 0);
    sem_close(s);


    // pthread_join()
    // does the scheduler

    return 0;
}