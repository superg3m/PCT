#define PCT_BOOTSTRAP
#include "pct.h"
#include "ckg.h"

typedef struct ThreadContext {
    int priority;
    volatile bool running;
    sem_t semaphore;
} ThreadContext;

int pct_thread_count = 0;
#define MAX_PCT_THREAD_COUNT 1024
ThreadContext threads[MAX_PCT_THREAD_COUNT];

volatile bool pct_done = false;
pthread_mutex_t ptc_mutex;
pthread_t pct_scheduling_thread_id;
CKG_HashMap(pthread_t, ThreadContext)* pct_thread_map = NULL;

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    pthread_mutex_lock(&ptc_mutex);
        pthread_t key = pthread_self();
        ThreadContext* temp = ckg_hashmap_get_pointer(pct_thread_map, key);
        temp->running = false;

        // NOTE(Jovanni): I need to cache this because once i'm outside of this mutex
        // its possible that I grow the hashmap and ctx is invalid
        ThreadContext ctx = *temp;
    pthread_mutex_unlock(&ptc_mutex);

    sem_wait(ctx.semaphore); // NOTE(Jovanni): wait to be signaled by main thread
    return pthread_mutex_lock(mutex);
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

#define PCT_SYNC 1
// NOTE(Jovanni): This is the main engine of hte scheduler, not sure if it should be join or not tbh
void* pct_scheduling_thread(void* arg) {
    // TODO(Jovanni): I need to figure out how to tell the scheduler if a thread is dead or not, like how do we know to stop or not?
    while (!pct_done) {
        // TODO(Jovanni): [SLOW] I can replace this later with a heap data structure, or just sort or whatever
        pthread_mutex_lock(&ptc_mutex);
            ThreadContext* highest_prio_thread = NULL;
            int running_count = pct_thread_map->meta.count;
            for (int i = 0; i < pct_thread_map->meta.capacity; i++) {
                if (!pct_thread_map->entries[i].filled || pct_thread_map->entries[i].dead) {
                    continue;
                }

                ThreadContext* value = &pct_thread_map->entries[i].value;
                #if PCT_SYNC
                    if (!value->running) {
                        running_count--;
                    }
                #else
                    if (value->running) {
                        continue;
                    }
                #endif

                if (!highest_prio_thread || highest_prio_thread->priority < value->priority) {
                    highest_prio_thread = value;
                }
            }

            #if PCT_SYNC
                if (highest_prio_thread && running_count == 0) {
                    highest_prio_thread->running = true;
                    sem_signal(highest_prio_thread->semaphore);
                }
            #else
                if (highest_prio_thread) {
                    highest_prio_thread->running = true;
                    sem_signal(highest_prio_thread->semaphore);
                }
            #endif
        pthread_mutex_unlock(&ptc_mutex);
    }

    return 0;
}

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

int pct_pthread_create(pthread_t* restrict thread_id, const pthread_attr_t *restrict attr, void*(*func)(void*), void *restrict arg, int temp_prio) {
    ckg_assert_msg(pct_thread_count < MAX_PCT_THREAD_COUNT, "Thread count exceeded\n");
    
    ThreadContext ctx = {0};
    ctx.running = true;
    ctx.priority = temp_prio; // random_range(0, 50);
    sem_init(&ctx.semaphore, 0, 0);

    pthread_mutex_lock(&ptc_mutex);
        pthread_create(thread_id, NULL, func, arg);
        ckg_hashmap_put(pct_thread_map, *thread_id, ctx);
        pct_thread_count++;
    pthread_mutex_unlock(&ptc_mutex);
    
    return 0;
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