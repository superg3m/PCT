#include "thread.h"
#include <stdio.h>

static void swap(int* a, int* b) {
    int temp = 0;
    temp = *a;
    *a = *b;
    *b = temp;
}

void* thread_worker_sort(void* args) {
    ThreadData* td = (ThreadData*)args;
    
    bool swapped = false;
    OUT("      Thread %d compares x[%d] = %d and x[%d] = %d\n", 
        *td->swap_address_1, 
        td->swap_index_1, *td->swap_address_1,
        td->swap_index_2, *td->swap_address_2
    );

    if (*td->swap_address_1 > *td->swap_address_2) {
        swap(td->swap_address_1, td->swap_address_2);
        OUT("      Thread %d swaps x[%d] = %d and x[%d] = %d\n", 
            *td->swap_address_1, 
            td->swap_index_1, *td->swap_address_1,
            td->swap_index_2, *td->swap_address_2
        );

        swapped = true;
    }

    pct_markthread_done();
    return (void*)swapped;
}