// NOTE(Jovanni): One very interesting thing about enforcing only one thread and removing the parallelism component is
// deterministic results given a seed and no lock contention!

typedef enum ThreadExecutionState {
    PCT_THREAD_NONE,
    PCT_THREAD_READY,
    PCT_THREAD_RUNNING,
    PCT_THREAD_WAITING
} ThreadExecutionState;

typedef struct ThreadContext {
    int priority;
    ThreadExecutionState execution_state;
    sem_t* semaphore;
    // void* waiting_on; NOTE(Jovanni): Maybe per thread we could keep track of what we are waiting on instead of union find
} ThreadContext;

u64 pct_step_counter = 0;
u64 pct_active_thread_count = 0;
u64 pct_thread_index = 0;

#define PCT_MAX_THREAD_COUNT 1024 + 1
u64 pct_priority_change_points[PCT_MAX_THREAD_COUNT];

// NOTE(Jovanni): i'm super curious if we can use union find to track when theres a cycle
// in the resource mangment, each thread would be a node and each unique resource would be a node.
// when a thread owns a resource you can union that node and thread as long as its not apart of another component/thread
// int parents[PCT_MAX_THREAD_COUNT]
// int rank[PCT_MAX_THREAD_COUNT]
// pct_lock_owner -> What thread context owns this lock so I can efficently look it up using union find

// NOTE(Jovanni):
// we have to maintain the state of the locks so a thread never calls lock unless it can get past it
// maps pthread_mutex_t* -> owner thread index (0 means unlocked, 0 is nullspace)
// TODO(Jovanni): union find

void pct_init() {
    // TODO(Jovanni): Just make the change pct_priority_change_points unique values
    // will get rid of some headache later.
    for (int i = 0; i < PCT_MAX_THREAD_COUNT; i++) {
        pct_priority_change_points[i] = random_range(1, k);
    }
}

void pct_schedule() {
    ThreadContext* best = NULL;
    for (t in pct_threads) {
        if (t->execution_state != PCT_THREAD_READY) continue;
        if (!best || t->priority > best->priority) best = t;
    }

    if (!best) {
        // NOTE(Jovanni): I think this means that everything is finished or its a deadlock
        if (pct_active_thread_count > 0) pct_report_deadlock();
        return;
    }

    best->execution_state = PCT_THREAD_RUNNING;
    sem_post(best->semaphore);
}

void pct_wait_for_scheduler(ThreadContext* self) {
    pct_schedule();
    sem_wait(self->semaphore);
}

void pct_step(ThreadContext* self) {
    pct_step_counter += 1;

    for (int i = 1; i < PCT_MAX_THREAD_COUNT; i++) {
        if (pct_step_counter == pct_priority_change_points[i]) {
            // NOTE(Jovanni): from the paper, the i-th change point sets priority to i
            // NOTE(Jovanni): this is lower than every initial priority, which are all >= d
            self->priority = i + 1;

            // NOTE(Jovanni): priority changed, so re-pick the highest priority thread
            self->execution_state = PCT_THREAD_READY;
            pct_wait_for_scheduler(self);
        }
    }
}

int pct_pthread_create(thread_id, attr, func, arg) {
    ThreadContext ctx = {0};
    // ctx.priority = random_range(?, ?); // NOTE(Jovanni): I need to reread the paper
    ctx.execution_state = PCT_THREAD_READY;
    ctx.semaphore = new_semaphore(0);

    pct_threads[pct_thread_index++] = ctx;
    pct_active_thread_count += 1;
    pthread_create(thread_id, NULL, func, arg);

    // NOTE(Jovanni): I could send this thread to an shared entry point would make it lot easier to control
    // pthread_create(thread_id, NULL, pct_thread_entry, Arg{func, arg});
}

void pct_thread_exit() {
    ThreadContext* self = pct_find_self()
    self->execution_state = PCT_THREAD_NONE;
    pct_active_thread_count -= 1;

    // NOTE(Jovanni): anyone joining on us can proceed now
    for (t in pct_threads) {
        if (t->waiting_on == self) {
            // NOTE(Jovanni): Doesn't this mean you exited while holding some resource like a mutex?
            // If that is the case we can report that!

            // NOTE(Jovanni): We could also resolve it outselves in an artificial way
            // t->waiting_on = NULL;
            // t->execution_state = PCT_THREAD_READY;
        }
    }

    pct_schedule();
}

int pct_pthread_mutex_lock(pthread_mutex_t* mutex) {
    ThreadContext* self = pct_get_self();
    pct_step(self);

    // TODO(Jovanni): Here we need to if i'm trying to lock a mutex that is already locked, if I am report deadlock?

    pthread_mutex_lock(mutex);
}

int pct_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    ThreadContext* self = pct_get_self();
    pct_step(self);
    pthread_mutex_unlock(mutex);

    
    self->execution_state = PCT_THREAD_READY;
    pct_wait_for_scheduler(self);
}

int pct_pthread_join(pthread_t target, void** ret) {
    ThreadContext* self = pct_get_self();
    pct_step(self);

    // TODO(Jovanni): Some magic in here

    return pthread_join(target, ret);
}