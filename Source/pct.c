#define PCT_BOOTSTRAP
#include "pct.h"
#include "ckg.h"

typedef struct ThreadContext {
    int priority;
    int generation;
    volatile bool running;
    sem_t semaphore;
} ThreadContext;

volatile bool pct_done = false;
pthread_mutex_t ptc_mutex;
pthread_t pct_scheduling_thread_id;
CKG_HashMap(pthread_t, ThreadContext)* pct_thread_map = NULL;

void* pct_scheduling_thread(void* arg);

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

// NOTE(Jovanni): [SLOW] for now these will just collide and linear chain
u64 pct_pthread_hash(void* data, u64 size) {
    return 0;
}

bool pct_pthread_equal(void* c1, size_t c1_size, void* c2, size_t c2_size) {
    pthread_t* p1 = (pthread_t*)c1;
    pthread_t* p2 = (pthread_t*)c2;

    return pthread_equal(*p1, *p2);
}

void pct_init() {
    srand(100);
    pthread_mutex_init(&ptc_mutex, NULL);
    ckg_hashmap_init_with_hash(pct_thread_map, pthread_t, ThreadContext, false, pct_pthread_hash, pct_pthread_equal);
    pthread_create(&pct_scheduling_thread_id, NULL, pct_scheduling_thread, NULL);
}

void pct_shutdown() {
    pct_done = true;
    pthread_join(pct_scheduling_thread_id, NULL);

    // TODO(Jovanni): go through hashmap and destroy all the sems
    ckg_hashmap_free(pct_thread_map);
    pthread_mutex_destroy(&ptc_mutex);
}

int pct_get_thread_priority() {
    int ret = -1;
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        ThreadContext* temp = ckg_hashmap_get_pointer(pct_thread_map, key);
        ret = temp->priority;
    pthread_mutex_unlock(&ptc_mutex);

    return ret;
}

void pct_markthread_done() {
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        ckg_hashmap_pop(pct_thread_map, key); 
    pthread_mutex_unlock(&ptc_mutex);
}

int pct_pthread_create(pthread_t* restrict thread_id, const pthread_attr_t *restrict attr, void*(*func)(void*), void *restrict arg) {
    ThreadContext ctx = {0};
    ctx.generation = 0;
    ctx.running = true;
    ctx.priority = random_range(0, 50);
    sem_init(&ctx.semaphore, 0, 0);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        ckg_hashmap_put(pct_thread_map, *thread_id, ctx);
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        ThreadContext* temp = ckg_hashmap_get_pointer(pct_thread_map, key);
        temp->running = false;

        // NOTE(Jovanni): I need to cache this because once i'm outside of this mutex
        // its possible that I grow the hashmap and ctx is invalid
        ThreadContext ctx = *temp;
    pthread_mutex_unlock(&ptc_mutex);

    sem_wait(&ctx.semaphore); // NOTE(Jovanni): wait to be signaled by main thread
    return pthread_mutex_lock(mutex);
}


#define PCT_GENERATION 1
#define PCT_WAIT_AND_SYNC 1
// NOTE(Jovanni): This is the main engine of hte scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            ThreadContext* ctx = NULL;
            int running_count = 0;
            for (int i = 0; i < pct_thread_map->meta.capacity; i++) {
                if (!pct_thread_map->entries[i].filled || pct_thread_map->entries[i].dead) {
                    continue;
                }

                ThreadContext* value = &pct_thread_map->entries[i].value;
                if (value->running) {
                    running_count += 1;
                    continue;
                }

                if (!ctx) {
                    ctx = value;
                }

                bool generation_is_less = value->generation < ctx->generation;
                bool generation_is_equal = value->generation == ctx->generation;
                bool priority_is_higher = value->priority > ctx->priority; 

                #if PCT_GENERATION
                    if (generation_is_less || generation_is_equal && priority_is_higher) {
                        ctx = value;
                    }
                #else
                    if (priority_is_higher) {
                        ctx = value;
                    }
                #endif
            }

            if (ctx && (PCT_WAIT_AND_SYNC ? running_count == 0 : true)) {
                ctx->running = true;
                ctx->generation += 1;
                sem_post(&ctx->semaphore);
            }
        pthread_mutex_unlock(&ptc_mutex);
    }

    return 0;
}