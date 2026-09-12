#include <stdbool.h>
#include "../../Source/pct.h"
#include <stdatomic.h>

#define EVEN 0
#define ODD 1

// NOTE(Jovanni): I realize I actually have very little understand about what should be volatile
// and if atomic makes sense for the bools. 
typedef struct ThreadData {
    int* swap_address_1;
    int* swap_address_2;
    int pass;

    int* thread_count;
    atomic_int* done_count;
    sem_t* start_sem;
    sem_t* done_sem;
    sem_t main_sem;

    volatile atomic_bool in_done; // finished all processing break from loop!
    volatile atomic_bool out_swap;
} ThreadData;

void* thread_worker_sort(void* args);