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
pthread_cond_t threads_are_waiting_condition;

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

bool PCT_DISABLE = false;
bool PCT_GENERATION = false;
bool PCT_RANDOM_PRIORITY = false;
WaitingBehavior PCT_WAIT_AND_SYNC = PCT_WAIT_STRICT;
Hashmap<PThreadKey, ThreadContext> pct_thread_map = {};

void* pct_scheduling_thread(void* arg);
int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

#define INTERNAL_PCT_SAFE_PTHREADS_LOCK(m) int GLUE(result, __LINE__) = pthread_mutex_lock((m)); RUNTIME_ASSERT_MSG(GLUE(result, __LINE__) == 0, "Error locking pct_mutex: %s\n", strerror(GLUE(result, __LINE__)))
#define INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(m) int GLUE(result, __LINE__) = pthread_mutex_unlock((m)); RUNTIME_ASSERT_MSG(GLUE(result, __LINE__) == 0, "Error unlocking pct_mutex: %s\n", strerror(GLUE(result, __LINE__)));

void pct_init() {
    srand(time(NULL));

    // TODO(Jovanni): Fix the aren, to have expendable pages so you can just use an arena allocator
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

    pct_thread_map = hashmap_create<PThreadKey, ThreadContext>(allocator, KB(4));
    pthread_mutex_init(&ptc_mutex, NULL);
    pthread_cond_init(&threads_are_waiting_condition, NULL);
    pthread_create(&pct_scheduling_thread_id, NULL, pct_scheduling_thread, NULL);
}

void pct_shutdown() {
    if (PCT_DISABLE) return;

    pct_done = true;

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pthread_cond_signal(&threads_are_waiting_condition);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    
    pthread_join(pct_scheduling_thread_id, NULL);

    // TODO(Jovanni): go through hashmap and destroy all the sems, actually isn't necessary if I use an arena
    // free the memory the arena is using
    // bootstrap_allocator.free(arena.memory)
    pthread_mutex_destroy(&ptc_mutex);
}

int pct_get_thread_priority() {
    if (PCT_DISABLE) return -1;

    PThreadKey key = PThreadKey(pthread_self());
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        int ret = tracked ? hashmap_get(&pct_thread_map, key).priority : -1;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    return ret;
}

void pct_markthread_done() {
    if (PCT_DISABLE) return;

    PThreadKey key = PThreadKey(pthread_self());
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        hashmap_remove(&pct_thread_map, key);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    if (PCT_DISABLE) return pthread_create(thread_id, NULL, func, arg);

    ThreadContext ctx = {0};
    ctx.generation = 0;
    ctx.execution_state = PCT_THREAD_RUNNING;
    ctx.priority = random_range(1, 1000);
    ctx.semaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(ctx.semaphore, 0, 0);

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        // NOTE(Jovanni): Just ensure no reallocations, so I can test something
        RUNTIME_ASSERT(pct_thread_map.count + pct_thread_map.count <= KB(1));

        pthread_create(thread_id, NULL, func, arg);
        PThreadKey key = PThreadKey(*thread_id);
        hashmap_put(&pct_thread_map, key, ctx);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (PCT_DISABLE) return pthread_mutex_lock(mutex);

    PThreadKey key = PThreadKey(pthread_self());
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) {
            hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_WAITING;
            pthread_cond_signal(&threads_are_waiting_condition);
        }
        sem_t* scheduler_semaphore = tracked ? hashmap_get(&pct_thread_map, key).semaphore : NULL;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler, this might be unsafe if hashmap reallocates???

    int ret = pthread_mutex_lock(mutex);

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_RUNNING;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    return ret;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

int pct_sem_wait(sem_t* s) {
    if (PCT_DISABLE) return sem_wait(s);;

    PThreadKey key = PThreadKey(pthread_self());
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        bool tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) {
            hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_WAITING;
            pthread_cond_signal(&threads_are_waiting_condition);
        }
        sem_t* scheduler_semaphore = tracked ? hashmap_get(&pct_thread_map, key).semaphore : NULL;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler

    int ret = sem_wait(s);

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        tracked = hashmap_has(&pct_thread_map, key);
        if (tracked) hashmap_get_pointer(&pct_thread_map, key)->execution_state = PCT_THREAD_RUNNING;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    return ret;
}

int pct_sem_post(sem_t* s) {
    return sem_post(s);
}

void* pct_scheduling_thread(void* arg) {
    if (PCT_DISABLE) return 0; // NOTE(Jovanni): Techincially this thread never starts if this is true, but its more clear this way

    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever


        INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
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

            if (waiting == 0) pthread_cond_wait(&threads_are_waiting_condition, &ptc_mutex);

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

            INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

            sem_post(scheduler_semaphore);
        } else {
            INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
        }
    }

    return 0;
}