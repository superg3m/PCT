#define PCT_BOOTSTRAP
#include "pct.h"
#include "core.hpp"

// TODO(Jovanni): Actually research volatile, I think its to tell the compiler not to optimize away
// a memory access via caching or any other method. I think if you surround a memory access around 
// a mutex then the compiler already knows not to reorder it or cache it.

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

volatile bool pct_done = false;
pthread_mutex_t ptc_mutex;
pthread_t pct_scheduling_thread_id;
pthread_cond_t pct_threads_are_waiting_condition;

pthread_t pct_main_thread;
pthread_key_t pct_pthread_key;

// PCT_MAX_THREAD_COUNT is 1024 + 1 because the zero index is nullspace
#define PCT_MAX_THREAD_COUNT 1024 + 1

u64 pct_thread_index = 1; // NOTE(Jovanni): the 0th index is nullspace
u64 pct_active_thread_count = 0;
ThreadContext pct_threads[PCT_MAX_THREAD_COUNT] = {};

typedef enum WaitingBehavior {
    PCT_WAIT_NONE = 0,
    PCT_WAIT_STRICT = 1,
    PCT_WAIT_NO_RUNNING = 2
} WaitingBehavior;

bool PCT_DISABLE = false;
bool PCT_GENERATION = false;
bool PCT_RANDOM_PRIORITY = false;
WaitingBehavior PCT_WAIT_AND_SYNC = PCT_WAIT_STRICT;

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
    pct_main_thread = pthread_self();

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
    pthread_mutex_init(&ptc_mutex, NULL);
    pthread_cond_init(&pct_threads_are_waiting_condition, NULL);
    pthread_key_create(&pct_pthread_key, NULL);
    pthread_create(&pct_scheduling_thread_id, NULL, pct_scheduling_thread, NULL);
}

void pct_shutdown() {
    if (PCT_DISABLE) return;

    pct_done = true;

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pthread_cond_signal(&pct_threads_are_waiting_condition);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    pthread_join(pct_scheduling_thread_id, NULL);

    // TODO(Jovanni): go through hashmap and destroy all the sems, actually isn't necessary if I use an arena
    // free the memory the arena is using
    // bootstrap_allocator.free(arena.memory)
    pthread_mutex_destroy(&ptc_mutex);
    pthread_key_delete(pct_pthread_key);
}

void init_pct_thread() {
    if (pthread_equal(pct_main_thread, pthread_self())) return;

    ThreadContext ctx = {0};
    ctx.generation = 0;
    ctx.execution_state = PCT_THREAD_RUNNING;
    ctx.priority = random_range(1, 1000);
    ctx.semaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(ctx.semaphore, 0, 0);
    pct_active_thread_count += 1;
    pthread_setspecific(pct_pthread_key, U64_TO_PTR(pct_thread_index));
    pct_threads[pct_thread_index++] = ctx;
}


int pct_get_thread_priority() {
    if (PCT_DISABLE) return -1;

    if (pthread_getspecific(pct_pthread_key) == NULL) {
        INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
            init_pct_thread();
        INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    }

    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    return pct_threads[thread_index].priority;
}

void pct_markthread_done() {
    if (PCT_DISABLE) return;

    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        if (thread_index && pct_threads[thread_index].execution_state != PCT_THREAD_NONE) {
            pct_threads[thread_index].execution_state = PCT_THREAD_NONE;
            pct_active_thread_count -= 1;
        }
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    RUNTIME_ASSERT_MSG(!PCT_DISABLE || (pct_thread_index < PCT_MAX_THREAD_COUNT), "MAX_THREAD_COUNT: %d has been exceeded\n", PCT_MAX_THREAD_COUNT);
    return pthread_create(thread_id, NULL, func, arg);
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (PCT_DISABLE) return pthread_mutex_lock(mutex);

    if (pthread_getspecific(pct_pthread_key) == NULL) {
        INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
            init_pct_thread();
        INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    }

    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    if (thread_index == 0) {
        return pthread_mutex_lock(mutex);
    }

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pct_threads[thread_index].execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = pct_threads[thread_index].semaphore;
        pthread_cond_signal(&pct_threads_are_waiting_condition);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    
    sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler, this might be unsafe if hashmap reallocates???

    int ret = pthread_mutex_lock(mutex);

    // TODO(Jovanni): Maybe just do an atomic set
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pct_threads[thread_index].execution_state = PCT_THREAD_RUNNING;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    return ret;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

int pct_sem_wait(sem_t* s) {
    if (PCT_DISABLE) return sem_wait(s);

    if (pthread_getspecific(pct_pthread_key) == NULL) {
        INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
            init_pct_thread();
        INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    }

    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    if (thread_index == 0) {
        return sem_wait(s);
    }

    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pct_threads[thread_index].execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = pct_threads[thread_index].semaphore;
        pthread_cond_signal(&pct_threads_are_waiting_condition);
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);
    
    sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler, this might be unsafe if hashmap reallocates???

    int ret = sem_wait(s);
    INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
        pct_threads[thread_index].execution_state = PCT_THREAD_RUNNING;
    INTERNAL_PCT_SAFE_PTHREADS_UNLOCK(&ptc_mutex);

    return ret;
}

int pct_sem_post(sem_t* s) {
    return sem_post(s);
}

void* pct_scheduling_thread(void* arg) {
    if (PCT_DISABLE) return 0; // NOTE(Jovanni): Techincially this thread never starts if this is true, but its more clear this way

    while (!pct_done) {
        INTERNAL_PCT_SAFE_PTHREADS_LOCK(&ptc_mutex);
            ThreadContext* ctx = NULL;
            int waiting = 0; int ready = 0; int running = 0;
            for (int i = 0; i < PCT_MAX_THREAD_COUNT - 1; i++) {
                ThreadContext* value = &pct_threads[(i + 1)]; // NOTE(Jovanni): the 0th index is nullspace
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

            if (waiting == 0) pthread_cond_wait(&pct_threads_are_waiting_condition, &ptc_mutex);

            bool wait_and_sync = false;
            if (PCT_WAIT_AND_SYNC == PCT_WAIT_NONE) {
                wait_and_sync = true;
            } else if (PCT_WAIT_AND_SYNC == PCT_WAIT_STRICT) {
                wait_and_sync = waiting == pct_active_thread_count;
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