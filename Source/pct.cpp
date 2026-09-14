#define PCT_BOOTSTRAP
#include "pct.h"
#include "core.hpp"

#if defined(__APPLE__) || defined(__MACH__)
    #define sem_wait(s) dispatch_semaphore_wait(*(s), DISPATCH_TIME_FOREVER)
    #define sem_post(s) dispatch_semaphore_signal(*(s))
#endif

typedef enum ThreadExecutionState {
    PCT_THREAD_READY, 
    PCT_THREAD_RUNNING, 
    PCT_THREAD_WAITING
} ThreadExecutionState;

typedef struct ThreadContext {
    int priority;
    int generation;
    volatile ThreadExecutionState execution_state;
    sem_t* semaphore;
} ThreadContext;

volatile bool pct_done = false;
pthread_mutex_t ptc_mutex;
pthread_t pct_scheduling_thread_id;

struct PThreadKey {
    pthread_t id;

    PThreadKey(pthread_t id) {
        this->id = id;
    }

    bool operator==(PThreadKey other) const {
        return pthread_equal(this->id, other.id);
    }
};

typedef enum WaitingBehavior {
    PCT_WAIT_NONE = 0,
    PCT_WAIT_STRICT = 1,
    PCT_WAIT_NO_RUNNING = 2
} WaitingBehavior;

int PCT_GENERATION = 0;
int PCT_RANDOM_PRIORITY = 0;
WaitingBehavior PCT_WAIT_AND_SYNC = PCT_WAIT_STRICT;
Hashmap<PThreadKey, ThreadContext> pct_thread_map = {};

void* pct_scheduling_thread(void* arg);
int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

void pct_init() {
    srand(time(NULL));

    Allocator allocator = allocator_general();

    Error err = Error::SUCCESS;
    size_t file_size = 0;
    u8* data = platform_read_entire_file(allocator, "../../../../pct.json", file_size, err);
    if (err != Error::SUCCESS) {
        RUNTIME_ASSERT_MSG(false, "Error initializing pct | %s\n", error_get_string(err));
        return;
    }
    
    JSON* root = JSON::Parse(allocator_general(), (char*)data, file_size);
    PCT_GENERATION = root->get<int>("PCT_GENERATION");
    PCT_RANDOM_PRIORITY = root->get<int>("PCT_RANDOM_PRIORITY");
    PCT_WAIT_AND_SYNC = (WaitingBehavior)root->get<int>("PCT_WAIT_AND_SYNC");

    // TODO(Jovanni): Fix the arean so you can just free all of this garbage, instead of pointer chasing

    pct_thread_map = hashmap_create<PThreadKey, ThreadContext>(allocator_general());
    pthread_mutex_init(&ptc_mutex, NULL);
    pthread_create(&pct_scheduling_thread_id, NULL, pct_scheduling_thread, NULL);
}

void pct_shutdown() {
    pct_done = true;
    pthread_join(pct_scheduling_thread_id, NULL);

    // TODO(Jovanni): go through hashmap and destroy all the sems
    pthread_mutex_destroy(&ptc_mutex);
}

int pct_get_thread_priority() {
    PThreadKey key = PThreadKey(pthread_self());
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        int ret = tracked ? hashmap_get(&pct_thread_map, key).priority : -1;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

void pct_markthread_done() {
    PThreadKey key = PThreadKey(pthread_self());
    pthread_mutex_lock(&ptc_mutex);
        hashmap_remove(&pct_thread_map, key);
    pthread_mutex_unlock(&ptc_mutex);
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    ThreadContext ctx = {0};
    ctx.generation = 0;
    ctx.execution_state = PCT_THREAD_RUNNING;
    ctx.priority = random_range(1, 1000);
    ctx.semaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(ctx.semaphore, 0, 0);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        PThreadKey key = PThreadKey(*thread_id);
        hashmap_put(&pct_thread_map, key, ctx);
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    PThreadKey key = PThreadKey(pthread_self());
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = tracked ? hashmap_get(&pct_thread_map, key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler

    int ret = pthread_mutex_lock(mutex);
    pthread_mutex_lock(&ptc_mutex);
        tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_RUNNING;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

int pct_sem_wait(sem_t* s) {
    PThreadKey key = PThreadKey(pthread_self());
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = tracked ? hashmap_get(&pct_thread_map, key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler

    int ret = sem_wait(s);
    pthread_mutex_lock(&ptc_mutex);
        tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_RUNNING;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_sem_post(sem_t* s) {
    return sem_post(s);
}

// NOTE(Jovanni): This is the main engine of the scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            ThreadContext* ctx = NULL;
            int waiting = 0; int ready = 0; int running = 0;
            for (HashmapEntry<PThreadKey, ThreadContext>& entry : pct_thread_map) {
                ThreadContext* value = &entry.value;
                switch (value->execution_state) {
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

                if (!ctx) {
                    ctx = value;
                }

                bool generation_is_less = value->generation < ctx->generation;
                bool generation_is_equal = value->generation == ctx->generation;
                bool priority_is_higher = !PCT_RANDOM_PRIORITY ? value->priority > ctx->priority : true; 

                if (PCT_GENERATION) {
                    if (generation_is_less || generation_is_equal && priority_is_higher) {
                        ctx = value;
                    }
                } else {
                    if (priority_is_higher) {
                        ctx = value;
                    }
                }
            }

            bool wait_and_sync = false;
            if (PCT_WAIT_AND_SYNC == PCT_WAIT_NONE) {
                wait_and_sync = true;
            } else if (PCT_WAIT_AND_SYNC == PCT_WAIT_STRICT) {
                wait_and_sync = waiting == pct_thread_map.count;
            } else if (PCT_WAIT_AND_SYNC == PCT_WAIT_NO_RUNNING) {
                wait_and_sync = running == 0;
            }

        if (ctx && wait_and_sync) {
            ctx->execution_state = PCT_THREAD_READY;
            ctx->generation += 1;
            sem_t* scheduler_semaphore = ctx->semaphore;
            pthread_mutex_unlock(&ptc_mutex);
            sem_post(scheduler_semaphore);
        } else {
            pthread_mutex_unlock(&ptc_mutex);
        }
    }

    return 0;
}