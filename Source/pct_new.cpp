#define PCT_BOOTSTRAP
#include "pct.h"
#include "core.hpp"

/*
step counter some thread needs to always be ru
*/

#if defined(__APPLE__) || defined(__MACH__)
    #define sem_wait(s) dispatch_semaphore_wait(*(s), DISPATCH_TIME_FOREVER)
    #define sem_post(s) dispatch_semaphore_signal(*(s))
#endif

typedef enum ThreadExecutionState {
    PCT_THREAD_NONE,
    PCT_THREAD_READY, 
    PCT_THREAD_RUNNING, 
    PCT_THREAD_WAITING
} ThreadExecutionState;

typedef struct ThreadContext {
    int priority;
    int generation;
    ThreadExecutionState execution_state;
    sem_t* semaphore;
} ThreadContext;

struct FunctionArgument {
    void*(*func)(void*);
    void* arg;
};

pthread_mutex_t pct_mutex;
pthread_key_t pct_pthread_key;

// NOTE(Jovanni) PCT_MAX_THREAD_COUNT is 1024 + 1 because the zero index is nullspace
#define PCT_MAX_THREAD_COUNT 1024 + 1

u64 pct_thread_index = 1; // NOTE(Jovanni): the 0th index is nullspace
u64 pct_active_thread_count = 0;
ThreadContext pct_threads[PCT_MAX_THREAD_COUNT] = {};
u64 pct_step_counter = 0;

typedef enum WaitingBehavior {
    PCT_WAIT_NONE = 0,
    PCT_WAIT_STRICT = 1,
    PCT_WAIT_NO_RUNNING = 2
} WaitingBehavior;

bool PCT_DISABLE = false;
bool PCT_GENERATION = false;
bool PCT_RANDOM_PRIORITY = false;
WaitingBehavior PCT_WAIT_AND_SYNC = PCT_WAIT_STRICT;

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

void pct_init() {
    srand(time(NULL));

    // TODO(Jovanni): Fix the arena, to have expendable pages so you can just use an arena allocator
    // arena = ARENA_CREATE_EXTENDABLE_PAGES()
    Allocator allocator = allocator_general();

    Error err = Error::SUCCESS;
    size_t file_size = 0;
    u8* data = platform_read_entire_file(allocator, "../../../../pct.json", file_size, err);
    if (err != Error::SUCCESS) {
        RUNTIME_ASSERT_MSG(false, "Error initializing pct | %s\n", error_get_string(err));
        return;
    }
    
    // TODO(Jovanni): Just do some basics checks so that if the json is malformed it doesn't assert and crash
    JSON* root = JSON::Parse(allocator, (char*)data, file_size);
    PCT_DISABLE = root->get<bool>("PCT_DISABLE");
    PCT_GENERATION = root->get<bool>("PCT_GENERATION");
    PCT_RANDOM_PRIORITY = root->get<bool>("PCT_RANDOM_PRIORITY");
    PCT_WAIT_AND_SYNC = (WaitingBehavior)root->get<int>("PCT_WAIT_AND_SYNC");

    if (PCT_DISABLE) {
        return;
    }

    pct_threads[0].execution_state = PCT_THREAD_NONE;
    pct_threads[0].priority = -1;
    pct_threads[0].generation = -1;
    pthread_key_create(&pct_pthread_key, NULL);
}

void pct_shutdown() {
    if (PCT_DISABLE) return;

    // TODO(Jovanni): go through hashmap and destroy all the sems, actually isn't necessary if I use an arena
    // free the memory the arena is using
    // bootstrap_allocator.free(arena.memory)
    pthread_mutex_destroy(&pct_mutex);
    pthread_key_delete(pct_pthread_key);
}

void pct_markthread_done() {
    if (PCT_DISABLE) return;

    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    pthread_mutex_lock(&pct_mutex);
        if (thread_index && pct_threads[thread_index].execution_state != PCT_THREAD_NONE) {
            pct_threads[thread_index].execution_state = PCT_THREAD_NONE;
            pct_active_thread_count -= 1;
        }
    pthread_mutex_unlock(&pct_mutex);
}

void pct_step(ThreadContext* self) {
    pct_step_counter += 1;

    for (int i = 1; i < PCT_MAX_THREAD_COUNT; i++) {
        /*
        if (pct_step_counter == pct_priority_modify_points[i]) {
            self->priority = ???; // TODO(Jovanni): I need to reread the paper how they change prio
            self->execution_state = PCT_THREAD_READY;
            pct_wait_for_scheduler(self);
        }
        */
    }
}

bool pct_schedule() {
    ThreadContext* next_to_schedule = NULL;
    int waiting = 0; int ready = 0; int running = 0;
    for (int i = 1; i < PCT_MAX_THREAD_COUNT; i++) {  // NOTE(Jovanni): the 0th index is nullspace
        ThreadContext* value = &pct_threads[i];
        switch (value->execution_state) {
            case PCT_THREAD_NONE: {
                continue;
            } break;

            case PCT_THREAD_WAITING: {
                waiting += 1;
            } break;

            case PCT_THREAD_READY: {
                ready += 1;
            } break;

            case PCT_THREAD_RUNNING: {
                running += 1;
            } break;
        }

        if (value->execution_state != PCT_THREAD_WAITING) {
            continue;
        }

        if (!next_to_schedule) {
            next_to_schedule = value;
        }

        bool generation_is_less = value->generation < next_to_schedule->generation;
        bool generation_is_equal = value->generation == next_to_schedule->generation;
        bool priority_is_higher = !PCT_RANDOM_PRIORITY ? value->priority > next_to_schedule->priority : true; 

        if (PCT_GENERATION) {
            if (generation_is_less || generation_is_equal && priority_is_higher) {
                next_to_schedule = value;
            }
        } else {
            if (priority_is_higher) {
                next_to_schedule = value;
            }
        }
    }

    if (next_to_schedule) {
        next_to_schedule->execution_state = PCT_THREAD_RUNNING;
        next_to_schedule->generation += 1;
        sem_t* scheduler_semaphore = next_to_schedule->semaphore;
        sem_post(scheduler_semaphore);
    } else {
        // NOTE(Jovanni): I think this means that everything is finished or its a deadlock
        if (pct_active_thread_count > 0) RUNTIME_ASSERT_MSG(false, "REPORT_DEDLOCK\n");
        return false;
    }

    return true;
}

void pct_wait_for_scheduler(ThreadContext* self) {
    if (pct_schedule()) {
        sem_wait(self->semaphore);
    }
}

void* pct_thread_entrypoint(void* arg) {
    FunctionArgument* f = (FunctionArgument*)arg;
    
    pthread_mutex_lock(&pct_mutex);
        pthread_setspecific(pct_pthread_key, U64_TO_PTR(pct_thread_index));
        pct_active_thread_count += 1;

        ThreadContext* self = &pct_threads[pct_thread_index];
        self->generation = 0;
        self->execution_state = PCT_THREAD_RUNNING;
        self->priority = random_range(1, 1000);
        self->semaphore = (sem_t*)malloc(sizeof(sem_t));
        sem_init(self->semaphore, 0, 0);
        pct_thread_index += 1;
    pthread_mutex_unlock(&pct_mutex);

    pct_wait_for_scheduler(self);
    void* ret = f->func(f->arg);
    pct_markthread_done();

    return ret;
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    RUNTIME_ASSERT_MSG(!PCT_DISABLE || (pct_thread_index < PCT_MAX_THREAD_COUNT), "MAX_THREAD_COUNT: %d has been exceeded\n", PCT_MAX_THREAD_COUNT);

    // TODO(Jovanni): use the arena allocator here so I can just free the arena
    FunctionArgument* f = new FunctionArgument();
    f->func = func;
    f->arg = arg;

    return pthread_create(thread_id, NULL, pct_thread_entrypoint, f);
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (PCT_DISABLE) return pthread_mutex_lock(mutex);
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}