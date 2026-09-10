// #include "scheduler.c"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include "ckg.h"

/*
My current plan of action is to do a job system basically. But just create thread and run the func(arg)
This should mean semaphore waits and mutex stuff should still work properly


VERY IMPORTANT: Write the usage code first, I want to make as few compromises as possible

OK I solved it:
mutex locks and semaphore waits will put the thread to sleep waiting for the main thread to wake it up

basically I want the thread id to uniquely map to an array slot (I could use my hashmap...)
*/

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
    int priority;
    volatile bool running;
    sem_t semaphore;
} ThreadContext;

int thread_count = 0;
int unique_thread_id = 0;
#define MAX_THREAD_COUNT 1024
ThreadContext threads[MAX_THREAD_COUNT];

pthread_mutex_t ptc_mutex;
CKG_HashMap(u64, ThreadContext)* thread_map = NULL;
// ckg_hashmap_init_siphash(thread_context_hashmap, pid_t, ThreadContext);

void pthread_mutex_lock_wrapper(pthread_mutex_t* mutex) {
    pthread_mutex_lock(&ptc_mutex);
        pid_t key = gettid();
        ThreadContext* ctx = ckg_hashmap_get_pointer(thread_map, key);
        ctx->running = false;
    pthread_mutex_unlock(&ptc_mutex);

    sem_wait(ctx->semaphore); // wait to be signaled by main thread
    pthread_mutex_lock(mutex);
}

void scheduler() {
    // TODO(Jovanni): I need to figure out how to tell the scheduler if a thread is dead or not, like how do we know to stop or not?
    while (thread_count > 0) {
        // TODO(Jovanni): THIS IS SLOW, I can replace this later with a heap data structure, or just sort or whatever
        ThreadContext* highest_prio_thread = NULL;
        for (int i = 0; i < thread_map->meta.capacity; i++) {
            if (thread_map->entries[i].filled || thread_map->entries[i].dead) {
                continue;
            }

            ThreadContext* value = &thread_map->entries[i].value;
            if (value->running) {
                continue;
            }

            if (!highest_prio_thread || highest_prio_thread->priority < value->priority) {
                highest_prio_thread = value;
            }
        }

        if (highest_prio_thread) {
            sem_signal(highest_prio_thread->semaphore);
        }
    }
}

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

int pthread_create_fake(pthread_t* restrict thread_id, const pthread_attr_t *restrict attr, void*(*func)(void*), void *restrict arg) {
    ThreadContext ctx = {0};
    ctx.running = true;
    ctx.priority = random_range(0, 50);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        ckg_hashmap_put(thread_map, (u64)*thread_id, ctx);
        thread_count++;
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int main() {
    sem_t s;
    sem_init(&s, 0, 0);
    sem_close(s);

    srand(time(NULL));

    // pthread_create_fake();


    // pthread_join()
    // does the scheduler

    return 0;
}