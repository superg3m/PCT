#define PCT_BOOTSTRAP
#include "pct.h"

#include <unordered_map>

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

struct pct_pthread_equal {
    bool operator()(const pthread_t& a, const pthread_t& b) const {
        return pthread_equal(a, b);
    }
};

struct pct_pthread_hash {
    size_t operator()(pthread_t thread) const {
        return 0;
    }
};

std::unordered_map<pthread_t, ThreadContext, pct_pthread_hash, pct_pthread_equal> pct_thread_map = {};

void* pct_scheduling_thread(void* arg);
int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

void pct_init() {
    srand(100);
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
        bool tracked = pct_thread_map.count(key);
        int ret = tracked ? pct_thread_map[key].priority : -1;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

void pct_markthread_done() {
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        pct_thread_map.erase(key);
    pthread_mutex_unlock(&ptc_mutex);
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    ThreadContext ctx = {0};
    ctx.generation = 0;
    ctx.execution_state = PCT_THREAD_RUNNING;
    ctx.priority = random_range(1, 50);
    ctx.semaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(ctx.semaphore, 0, 0);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        pct_thread_map[*thread_id] = ctx;
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = pct_thread_map.count(key);
        if (tracked) pct_thread_map[key].execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = tracked ? pct_thread_map.at(key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);


    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler
    int ret = pthread_mutex_lock(mutex);
    pthread_mutex_lock(&ptc_mutex);
        if (tracked) pct_thread_map[key].execution_state = PCT_THREAD_RUNNING;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

int pct_sem_wait(sem_t* s) {
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
        bool tracked = pct_thread_map.count(key);
        if (tracked) pct_thread_map[key].execution_state = PCT_THREAD_WAITING;
        sem_t* scheduler_semaphore = tracked ? pct_thread_map.at(key).semaphore : NULL;
    pthread_mutex_unlock(&ptc_mutex);
       
    if (tracked) sem_wait(scheduler_semaphore); // NOTE(Jovanni): wait for scheduler
    int ret = sem_wait(s);
    pthread_mutex_lock(&ptc_mutex);
        if (tracked) pct_thread_map[key].execution_state = PCT_THREAD_RUNNING;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

int pct_sem_post(sem_t* s) {
    return sem_post(s);
}

#define PCT_GENERATION 1
#define PCT_WAIT_AND_SYNC 1
// NOTE(Jovanni): This is the main engine of hte scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            ThreadContext* ctx = NULL;

            int waiting = 0;
            int ready = 0;
            int running = 0;
            for (const auto& [key, value] : pct_thread_map) {
                switch (value.execution_state) {
                    case PCT_THREAD_READY: {
                        ready += 1;
                    } break;

                    case PCT_THREAD_WAITING: {
                        waiting += 1;
                    } break;

                    case PCT_THREAD_RUNNING: {
                        running += 1;
                    } break;
                }

                if (!ctx) {
                    ctx = &pct_thread_map.at(key);
                }

                bool generation_is_less = value.generation < ctx->generation;
                bool generation_is_equal = value.generation == ctx->generation;
                bool priority_is_higher = value.priority > ctx->priority; 

                #if PCT_GENERATION
                    if (generation_is_less || generation_is_equal && priority_is_higher) {
                        ctx = &pct_thread_map[key];
                    }
                #else
                    if (priority_is_higher) {
                        ctx = &pct_thread_map[key];
                    }
                #endif
            }

        bool wait_and_sync = PCT_WAIT_AND_SYNC ? (waiting == pct_thread_map.size()) : true;
        bool should_resolve_possible_deadlock = PCT_WAIT_AND_SYNC ? (running == 0) : false;
        if (ctx && (wait_and_sync || should_resolve_possible_deadlock)) {
            ctx->execution_state = PCT_THREAD_READY;
            ctx->generation += 1;
            pthread_mutex_unlock(&ptc_mutex);
            sem_post(ctx->semaphore);
        } else {
            pthread_mutex_unlock(&ptc_mutex);
        }
    }

    return 0;
}