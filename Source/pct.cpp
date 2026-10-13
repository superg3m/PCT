#define PCT_BOOTSTRAP
#include "pct.h"
#include "core.hpp"

// TODO(Jovanni):
// mutex_init and sem_init needs to be tracked
// so I can just say mutex_owner[mutext]
// and I can track the count including inital count
 

#if defined(__APPLE__) || defined(__MACH__)
    #define sem_init(s, pshared, value) (*(s) = dispatch_semaphore_create(value), (int)(*(s) != NULL))
    #define sem_wait(s) dispatch_semaphore_wait(*(s), DISPATCH_TIME_FOREVER)
    #define sem_post(s) dispatch_semaphore_signal(*(s))
#endif

enum PCT_WaitType {
    NONE,
    MUTEX,
    SEMAPHORE,
    JOIN
};

typedef struct ThreadContext {
    bool finished;
    int priority;
    int generation;
    sem_t semaphore;

    PCT_WaitType wait_type;
    void* waiting_on;
} ThreadContext;

struct PThreadKey {
    pthread_t id;

    PThreadKey(pthread_t id) {
        this->id = id;
    }

    bool operator==(PThreadKey other) const {
        return pthread_equal(this->id, other.id);
    }
};

struct FunctionArgument {
    void*(*func)(void*);
    void* arg;
    ThreadContext* ctx;
};

void pct_markthread_done();
void* pct_thread_entrypoint(void* arg);

Arena arena = {};
void* program_memory = nullptr;

pthread_mutex_t pct_mutex;
u64 pct_step_counter = 0;
u64 pct_active_threads = 0;

#define PCT_MAX_THREAD_COUNT 1024
thread_local ThreadContext* self = nullptr;
Hashmap<PThreadKey, ThreadContext*> pct_thread_map = {};
Hashmap<void*, ThreadContext*> pct_mutex_owner = {};  // mutex -> holder
Hashmap<void*, unsigned int>            pct_sem_count   = {};  // sem   -> modeled count

static bool pct_thead_is_safely_runnable(ThreadContext* thread) {
    switch (thread->wait_type) {
        case NONE: {
            return true;
        } break;

        case MUTEX: {
            if (!hashmap_has(&pct_mutex_owner, thread->waiting_on)) {
                return true;
            }

            ThreadContext* owner = hashmap_get(&pct_mutex_owner, thread->waiting_on);
            return owner == nullptr || owner == thread;
        } break;

        case SEMAPHORE: {
            return hashmap_get(&pct_sem_count, thread->waiting_on) > 0;
        } break;

        case JOIN:  {
            return ((ThreadContext*)thread->waiting_on)->finished;
        } break;
    }

    return false;
}

bool PCT_DISABLE = false;
bool PCT_GENERATION = false;
bool PCT_RANDOM_PRIORITY = false;

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

// NOTE(Jovanni): Maybe I should just allocate these on the heap so I don't have to be careful about 
// hashmap reallocations
ThreadContext* pct_allocate_thread_context() {
    ThreadContext* ctx = (ThreadContext*)arena_push(&arena, sizeof(ThreadContext));
    ctx->finished = false;
    ctx->generation = 0; 
    ctx->priority = random_range(1, 1000);
    sem_init(&ctx->semaphore, 0, 0);

    return ctx;
}

void pct_init() {
    srand(time(NULL));

    // TODO(Jovanni): Fix the arena, to have expendable pages so you can just use an arena allocator
    // arena = ARENA_CREATE_EXTENDABLE_PAGES()

    const int ALLOCATION_SIZE = KB(512);
    program_memory = malloc(ALLOCATION_SIZE);
    arena = ARENA_CREATE_FIXED(program_memory, ALLOCATION_SIZE);
    Allocator allocator = arena_to_allocator(&arena);

    // NOTE(Jovanni): Hashmap has a capacity of PCT_MAX_THREAD_COUNT * 4 so I can not have to worry about reallocations
    pct_thread_map = hashmap_create<PThreadKey, ThreadContext*>(allocator, PCT_MAX_THREAD_COUNT * 4);
    pct_mutex_owner = hashmap_create<void*, ThreadContext*>(allocator);
    pct_sem_count = hashmap_create<void*, unsigned int>(allocator);

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
    hashmap_put(&pct_thread_map, PThreadKey(main_thread), ctx);
    pthread_mutex_init(&pct_mutex, NULL);
    self = ctx;
}

void pct_shutdown() {
    if (PCT_DISABLE) return;

    // TODO(Jovanni): go through hashmap and destroy all the sems, actually isn't necessary if I use an arena
    // free the memory the arena is using
    free(program_memory);
    pthread_mutex_destroy(&pct_mutex);
}

int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attr, void*(*func)(void*), void* arg) {
    if (PCT_DISABLE) return pthread_create(thread_id, NULL, func, arg);
    RUNTIME_ASSERT_MSG(pct_active_threads < PCT_MAX_THREAD_COUNT, "MAX_THREAD_COUNT: %d has been exceeded\n", PCT_MAX_THREAD_COUNT);

    // TODO(Jovanni): use the arena allocator here so I can just free the arena
    FunctionArgument* f = (FunctionArgument*)arena_push(&arena, sizeof(FunctionArgument));
    f->func = func;
    f->arg = arg;
    f->ctx = pct_allocate_thread_context();

    pct_active_threads += 1;
    int ret = pthread_create(thread_id, NULL, pct_thread_entrypoint, f);
    hashmap_put(&pct_thread_map, PThreadKey(*thread_id), f->ctx);

    return ret;
}

int pct_get_thread_priority() {
    if (PCT_DISABLE) return -1;
    return self ? self->priority : -1;
}

void* pct_thread_entrypoint(void* arg) {
    FunctionArgument* f = (FunctionArgument*)arg;
    self = f->ctx;
    sem_wait(&self->semaphore);
    // NOTE(Jovanni): VERY IMPORTANT
    // I'm 99% sure you can guarentee everyhting past this point is essencially single threaded
    // meaning you don't have to use locks

    void* ret = f->func(f->arg);
    pct_markthread_done();

    return ret;
}

void pct_step() {
    pct_step_counter += 1;

    
    // TODO(Jovanni): For not this will just be a random event
    if (self->priority == pct_step_counter) {
        self->priority = random_range(1, 1000);
        pct_step_counter = 0;
    }

    for (int i = 0; i < PCT_MAX_THREAD_COUNT; i++) {
        /*
        if (pct_step_counter == pct_priority_modify_points[i]) {
            self->priority = ???; // TODO(Jovanni): I need to reread the paper how they change prio
            pct_wait_for_scheduler(self);
        }
        */
    }
}

ThreadContext* pct_pick_next() {
    ThreadContext* next_to_schedule = nullptr;
    for (HashmapEntry<PThreadKey, ThreadContext*>& e : pct_thread_map) {
        ThreadContext* value = e.value;
        if (value->finished || !pct_thead_is_safely_runnable(value)) continue;

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

    return next_to_schedule;
}

void pct_pass_the_torch(const char* function, const char* file, int line) {
    pct_step();
    ThreadContext* next = pct_pick_next();
    if (!next) {
        MACRO_RUNTIME_ASSERT_MSG(false, function, file, line, "REPORT_DEADLOCK\n");
    }

    next->generation += 1;
    sem_post(&next->semaphore);
    sem_wait(&self->semaphore);
}

void pct_markthread_done() {
    if (PCT_DISABLE) return;
    self->finished = true;
    pct_active_threads--;

    for (HashmapEntry<void*, ThreadContext*>& e : pct_mutex_owner) {
        RUNTIME_ASSERT_MSG(e.value != self, "Thread exited holding a lock");
    }

    ThreadContext* next = pct_pick_next();
    if (next) {
        sem_post(&next->semaphore);
    } else if (pct_active_threads > 0) {
        RUNTIME_ASSERT_MSG(false, "REPORT_DEADLOCK\n");
    }
}


int pct_pthread_mutex_lock(pthread_mutex_t* mutex, const char* function, const char* file, int line) {
    if (PCT_DISABLE) return pthread_mutex_lock(mutex);
    self->wait_type = MUTEX;
    self->waiting_on = (void*)mutex;

    pct_step();
    while (hashmap_has(&pct_mutex_owner, (void*)mutex) && hashmap_get(&pct_mutex_owner, (void*)mutex) != nullptr) {
        pct_pass_the_torch(function, file, line);
    }

    hashmap_put(&pct_mutex_owner, (void*)mutex, self);
    return pthread_mutex_lock(mutex);
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex, const char* function, const char* file, int line) {
    if (PCT_DISABLE) return pthread_mutex_unlock(mutex);

    pct_step();
    pct_pass_the_torch(function, file, line);

    ThreadContext* owner = hashmap_get(&pct_mutex_owner, (void*)mutex);
    RUNTIME_ASSERT_MSG(self == owner, "Tried to unlock a mutex that I did not own!\n");

    int ret = pthread_mutex_unlock(mutex);
    hashmap_put(&pct_mutex_owner, (void*)mutex, (ThreadContext*)nullptr);
    self->wait_type = NONE;
    self->waiting_on = nullptr;

    return ret;
}

// TODO(Jovanni):
int pct_sem_wait(sem_t* s, const char* function, const char* file, int line) {
    if (PCT_DISABLE) return sem_wait(s);
    self->wait_type = SEMAPHORE;
    self->waiting_on = (void*)s;

    RUNTIME_ASSERT_MSG(hashmap_has(&pct_sem_count, (void*)s), "Using an non-initalized semaphore\n");

    pct_step();
    pct_pass_the_torch(function, file, line);

    unsigned int value = hashmap_get(&pct_sem_count, (void*)s);
    hashmap_put(&pct_sem_count, (void*)s, value - 1);
    return sem_wait(s);
}

int pct_sem_post(sem_t* s, const char* function, const char* file, int line) {
    if (PCT_DISABLE) return sem_wait(s);
    RUNTIME_ASSERT_MSG(hashmap_has(&pct_sem_count, (void*)s), "Using an non-initalized semaphore\n");

    pct_step();
    pct_pass_the_torch(function, file, line);

    int ret = sem_post(s);
    self->wait_type = NONE;
    self->waiting_on = nullptr;
    unsigned int value = hashmap_get(&pct_sem_count, (void*)s);
    hashmap_put(&pct_sem_count, (void*)s, value + 1);
    return ret;
}

int pct_pthread_join(pthread_t thread, void** ret, const char* function, const char* file, int line) {
    if (PCT_DISABLE) return pthread_join(thread, ret);
    ThreadContext* waiting_on = hashmap_get(&pct_thread_map, PThreadKey(thread));
    self->wait_type = JOIN;
    self->waiting_on = waiting_on;

    pct_step();
    while (!waiting_on->finished) {
        pct_pass_the_torch(function, file, line);
    }

    int result = pthread_join(thread, ret);
    hashmap_remove(&pct_thread_map, PThreadKey(thread));
    sem_destroy(&waiting_on->semaphore);

    self->wait_type = NONE;
    self->waiting_on = nullptr;
    return result;
}

int pct_sem_init(sem_t* s, int pshared, unsigned int value) {
    hashmap_put(&pct_sem_count, (void*)s, value);
    return sem_init(s, pshared, value);
}