#define PCT_BOOTSTRAP
#include "pct.h"
#include <unordered_map>

typedef struct ThreadContext {
    int priority;
    int generation;
    volatile bool running;
    sem_t semaphore;
} ThreadContext;

volatile bool pct_done = false;
pthread_mutex_t ptc_mutex;
sem_t ptc_waiting_sem;
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
    sem_init(&ptc_waiting_sem, 0, 0);
    pthread_create(&pct_scheduling_thread_id, NULL, pct_scheduling_thread, NULL);
}

void pct_shutdown() {
    pct_done = true;
    pthread_join(pct_scheduling_thread_id, NULL);

    // TODO(Jovanni): go through hashmap and destroy all the sems
    pthread_mutex_destroy(&ptc_mutex);
}

int pct_get_thread_priority() {
    int ret = -1;
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
    if (!pct_thread_map.count(key)) {
        pthread_mutex_unlock(&ptc_mutex);
        sem_wait(&ptc_waiting_sem);
    } else {
        pthread_mutex_unlock(&ptc_mutex);
    }

    pthread_mutex_lock(&ptc_mutex);
        ret = pct_thread_map[key].priority;
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
    ctx.running = true;
    ctx.priority = random_range(0, 50);
    sem_init(&ctx.semaphore, 0, 0);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        pct_thread_map[*thread_id] = ctx;
        sem_post(&ptc_waiting_sem)
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    pthread_t key = pthread_self();
    pthread_mutex_lock(&ptc_mutex);
    if (!pct_thread_map.count(key)) {
        pthread_mutex_unlock(&ptc_mutex);
        sem_wait(&ptc_waiting_sem);
        pthread_mutex_lock(&ptc_mutex);
    }

    pct_thread_map[key].running = false;

    // NOTE(Jovanni): I need to cache this because once i'm outside of this mutex
    // its possible that I grow the hashmap while trying to access it thats dangerous.
    sem_t sem = pct_thread_map.at(key).semaphore;
    pthread_mutex_unlock(&ptc_mutex);
    sem_wait(&sem); // NOTE(Jovanni): wait to be signaled by main thread

    return pthread_mutex_lock(mutex);
}


#define PCT_GENERATION 1
#define PCT_WAIT_AND_SYNC 1
// NOTE(Jovanni): This is the main engine of hte scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            bool found = false;
            ThreadContext ctx = {0};
            ctx.generation = INT_MAX;
            ctx.priority = INT_MIN;
            int running_count = 0;
            for (const auto& [key, value] : pct_thread_map) {
                if (value.running) {
                    running_count += 1;
                    continue;
                }

                bool generation_is_less = value.generation < ctx.generation;
                bool generation_is_equal = value.generation == ctx.generation;
                bool priority_is_higher = value.priority > ctx.priority; 

                #if PCT_GENERATION
                    if (generation_is_less || generation_is_equal && priority_is_higher) {
                        found = true;
                        ctx = value;
                    }
                #else
                    if (priority_is_higher) {
                        found = true;
                        ctx = value;
                    }
                #endif
            }

            if (found && (PCT_WAIT_AND_SYNC ? running_count == 0 : true)) {
                ctx.running = true;
                ctx.generation += 1;
                sem_post(&ctx.semaphore);
            }
        pthread_mutex_unlock(&ptc_mutex);
    }

    return 0;
}