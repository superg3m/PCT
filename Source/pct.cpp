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
    PCT_THREAD_RUNNING,
    PCT_THREAD_READY_TO_BE_SCHEDULED,
    PCT_THREAD_WAITING
} ThreadExecutionState;

typedef struct ThreadContext {
    int priority;
    int generation;
    ThreadExecutionState execution_state;
    sem_t* semaphore;
    void* waiting_on;
} ThreadContext;

struct FunctionArgument {
    void*(*func)(void*);
    void* arg;
};

void* program_memory = nullptr;
Arena arena = {};

pthread_mutex_t pct_mutex;
pthread_key_t pct_pthread_key;

#define PCT_MAX_THREAD_COUNT 1024

u64 pct_thread_index = 1; // NOTE(Jovanni): the 0th index is main_thread
u64 pct_thread_count = 1; // NOTE(Jovanni): main thread counts
ThreadContext pct_threads[PCT_MAX_THREAD_COUNT] = {};
u64 pct_step_counter = 0;


Hashmap<pthread_t, ThreadContext*> pct_thread_map = {};

bool PCT_DISABLE = false;
bool PCT_GENERATION = false;
bool PCT_RANDOM_PRIORITY = false;

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

ThreadContext* pct_allocate_thread_context() {
    ThreadContext* ctx = (ThreadContext*)arena_push(&arena, sizeof(ThreadContext));
    ctx->generation = 0;
    ctx->execution_state = PCT_THREAD_WAITING;
    ctx->priority = random_range(1, 1000);
    ctx->semaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(ctx->semaphore, 0, 0);

    return ctx;
}

void pct_init() {
    srand(time(NULL));

    // TODO(Jovanni): Fix the arena, to have expendable pages so you can just use an arena allocator
    // arena = ARENA_CREATE_EXTENDABLE_PAGES()

    const int ALLOCATION_SIZE = KB(128);
    program_memory = malloc(ALLOCATION_SIZE);
    arena = ARENA_CREATE_FIXED(program_memory, ALLOCATION_SIZE);
    Allocator allocator = arena_to_allocator(&arena);

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

    if (PCT_DISABLE) {
        return;
    }

    ThreadContext* ctx = pct_allocate_thread_context();

    pthread_t main_thread = pthread_self();
    hashmap_put(&pct_thread_map, main_thread, ctx);

    pct_threads[0].execution_state = PCT_THREAD_RUNNING;
    pct_threads[0].priority = 0;
    pct_threads[0].generation = 0;
    pct_threads[0].semaphore = (sem_t*)malloc(sizeof(sem_t));
    
    sem_init(pct_threads[0].semaphore, 0, 0);
    pthread_key_create(&pct_pthread_key, NULL);
}

void pct_shutdown() {
    if (PCT_DISABLE) return;

    // TODO(Jovanni): go through hashmap and destroy all the sems, actually isn't necessary if I use an arena
    // free the memory the arena is using
    free(program_memory);
    pthread_mutex_destroy(&pct_mutex);
    pthread_key_delete(pct_pthread_key);
}

ThreadContext* pct_get_self() {
    u64 thread_index = PTR_TO_U64(pthread_getspecific(pct_pthread_key));
    ThreadContext* self = &pct_threads[thread_index];
    return self;
}

void pct_step() {
    pct_step_counter += 1;

    ThreadContext* self = pct_get_self();
    for (int i = 0; i < PCT_MAX_THREAD_COUNT; i++) {
        /*
        if (pct_step_counter == pct_priority_modify_points[i]) {
            self->priority = ???; // TODO(Jovanni): I need to reread the paper how they change prio
            pct_wait_for_scheduler(self);
        }
        */
    }
}

bool pct_schedule() {
    ThreadContext* self = pct_get_self();
    ThreadContext* next_to_schedule = NULL;
    for (int i = 0; i < PCT_MAX_THREAD_COUNT; i++) {
        ThreadContext* value = &pct_threads[i];
        if (value == self && pct_thread_count > 1) continue;

        switch (value->execution_state) {
            case PCT_THREAD_NONE: {
                continue;
            } break;

            case PCT_THREAD_WAITING: {
                // noop
            } break;

            case PCT_THREAD_RUNNING: {
                // NOTE(Jovanni): Already a thread running
                return true;
            } break;
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
        if (pct_thread_count > 0) RUNTIME_ASSERT_MSG(false, "REPORT_DEDLOCK\n");
        return false;
    }

    return true;
}

void pct_markthread_done() {
    if (PCT_DISABLE) return;


    ThreadContext* self = pct_get_self();
    pthread_mutex_lock(&pct_mutex);
        self->execution_state = PCT_THREAD_NONE;
        pct_thread_count -= 1;

        pct_schedule();
    pthread_mutex_unlock(&pct_mutex);
}

void pct_wait_for_scheduler() {
    ThreadContext* self = pct_get_self();

    pthread_mutex_lock(&pct_mutex);
        self->execution_state = PCT_THREAD_WAITING;
        if (pct_schedule()) {
            pthread_mutex_unlock(&pct_mutex);
            sem_wait(self->semaphore);
            return;
        }
    pthread_mutex_unlock(&pct_mutex);
}

void* pct_thread_entrypoint(void* arg) {
    FunctionArgument* f = (FunctionArgument*)arg;
    
    pthread_mutex_lock(&pct_mutex);
        pthread_setspecific(pct_pthread_key, U64_TO_PTR(pct_thread_index));
        pct_threads[pct_thread_index].generation = 0;
        pct_threads[pct_thread_index].execution_state = PCT_THREAD_WAITING;
        pct_threads[pct_thread_index].priority = random_range(1, 1000);
        pct_threads[pct_thread_index].semaphore = (sem_t*)malloc(sizeof(sem_t));
        sem_init(pct_threads[pct_thread_index].semaphore, 0, 0);

        u64 thread_index = pct_thread_index;

        pct_thread_index += 1;
        pct_thread_count += 1;
    pthread_mutex_unlock(&pct_mutex);

    sem_wait(pct_threads[thread_index].semaphore);
    void* ret = f->func(f->arg);
    pct_markthread_done();

    return ret;
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    if (PCT_DISABLE) return pthread_create(thread_id, NULL, func, arg);

    RUNTIME_ASSERT_MSG(pct_thread_index < PCT_MAX_THREAD_COUNT, "MAX_THREAD_COUNT: %d has been exceeded\n", PCT_MAX_THREAD_COUNT);

    // TODO(Jovanni): use the arena allocator here so I can just free the arena
    FunctionArgument* f = new FunctionArgument();
    f->func = func;
    f->arg = arg;

    return pthread_create(thread_id, NULL, pct_thread_entrypoint, f);
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (PCT_DISABLE) return pthread_mutex_lock(mutex);

    pct_step();
    pct_wait_for_scheduler();

    // check if you can lock

    return pthread_mutex_lock(mutex);
}

int pct_get_thread_priority() {
    if (PCT_DISABLE) return -1;

    ThreadContext* self = pct_get_self();
    return self ? self->priority : -1;
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    if (PCT_DISABLE) return pthread_mutex_unlock(mutex);

    int ret = pthread_mutex_unlock(mutex);

    pct_step();
    pct_wait_for_scheduler();
    return ret;
}

// TODO(Jovanni):
int pct_sem_wait(sem_t* s) {
    if (PCT_DISABLE) return sem_wait(s);

    pct_step();
    pct_wait_for_scheduler();
    return sem_wait(s);
}

int pct_sem_post(sem_t* s) {
    if (PCT_DISABLE) return sem_post(s);

    int ret = sem_post(s);
    pct_step();
    pct_wait_for_scheduler();
    return ret;
}

int pct_pthread_join(pthread_t thread, void** ret) {
    if (PCT_DISABLE) return pthread_join(thread, ret);

    pthread_mutex_lock(&pct_mutex);
        pct_thread_count -= 1;
    pthread_mutex_unlock(&pct_mutex);

    pct_step();
    pct_wait_for_scheduler();
    int v = pthread_join(thread, ret);

    pthread_mutex_lock(&pct_mutex);
        pct_thread_count += 1;
    pthread_mutex_unlock(&pct_mutex);

    return v;
}