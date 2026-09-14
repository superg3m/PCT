#define PCT_BOOTSTRAP
#include "pct.h"
#include "ckg.h"

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


bool pct_pthread_equal(void *c1, size_t c1_size, void *c2, size_t c2_size) {
    pthread_t* v1 = (pthread_t*)c1;
    pthread_t* v2 = (pthread_t*)c2;

    return pthread_equal(*v1, *v2);
}

// NOTE(Jovanni): For now just linear chain
u64 pct_pthread_hash(void *data, u64 size) {
    return 0;
}

CKG_HashMap(pthread_t, ThreadContext)* pct_thread_map = NULL;

void* pct_scheduling_thread(void* arg);
int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

void pct_init() {
    srand(time(NULL));
    ckg_hashmap_init_with_hash(pct_thread_map, pthread_t, ThreadContext, false, pct_pthread_hash, pct_pthread_equal);
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
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = ckg_hashmap_has(pct_thread_map, key);
        int ret = tracked ? ckg_hashmap_get(pct_thread_map, key).priority : -1;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

void pct_markthread_done() {
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        ckg_hashmap_pop(pct_thread_map, key);
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
        ckg_hashmap_put(pct_thread_map, *thread_id, ctx);
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = ckg_hashmap_has(pct_thread_map, key);
        if (tracked) {
            ThreadContext* value = ckg_hashmap_get_pointer(pct_thread_map, key);
            value->execution_state = PCT_THREAD_WAITING;
        }
        sem_t* scheduler_semaphore = tracked ? ckg_hashmap_get(pct_thread_map, key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler

    int ret = pthread_mutex_lock(mutex);
    pthread_mutex_lock(&ptc_mutex);
        tracked = ckg_hashmap_has(pct_thread_map, key);
        if (tracked) {
            ThreadContext* value = ckg_hashmap_get_pointer(pct_thread_map, key);
            value->execution_state = PCT_THREAD_RUNNING;
        } 
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

int pct_sem_wait(sem_t* s) {
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = ckg_hashmap_has(pct_thread_map, key);
        if (tracked) {
            ThreadContext* value = ckg_hashmap_get_pointer(pct_thread_map, key);
            value->execution_state = PCT_THREAD_WAITING;
        }
        sem_t* scheduler_semaphore = tracked ? ckg_hashmap_get(pct_thread_map, key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler

    int ret = sem_wait(s);
    pthread_mutex_lock(&ptc_mutex);
        tracked = ckg_hashmap_has(pct_thread_map, key);
        if (tracked) {
            ThreadContext* value = ckg_hashmap_get_pointer(pct_thread_map, key);
            value->execution_state = PCT_THREAD_RUNNING;
        } 
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_sem_post(sem_t* s) {
    return sem_post(s);
}

#define PCT_WAIT_NONE 0
#define PCT_WAIT_STRICT 1
#define PCT_WAIT_NO_RUNNING 2

#define PCT_GENERATION 0
#define PCT_RANDOM_PRIORITY 0
#define PCT_WAIT_AND_SYNC PCT_WAIT_NO_RUNNING
// NOTE(Jovanni): This is the main engine of hte scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            ThreadContext* ctx = NULL;
            int waiting = 0; int ready = 0; int running = 0;
            ckg_hashmap_iterator_pointer(pct_thread_map, pthread_t, ThreadContext, {
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

                #if PCT_GENERATION
                    if (generation_is_less || generation_is_equal && priority_is_higher) {
                        ctx = value;
                    }
                #else
                    if (priority_is_higher) {
                        ctx = value;
                    }
                #endif
            })

            #if PCT_WAIT_AND_SYNC == PCT_WAIT_NONE
                bool wait_and_sync = true;
            #elif PCT_WAIT_AND_SYNC == PCT_WAIT_STRICT
                bool wait_and_sync = waiting == pct_thread_map->meta.count;
            #elif PCT_WAIT_AND_SYNC == PCT_WAIT_NO_RUNNING
                bool wait_and_sync = running == 0;
            #endif

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