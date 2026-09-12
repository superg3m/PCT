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
    while (atomic_load(&td->in_done) == false) {
        sem_wait(&td->start_sem[td->pass]);
        if (atomic_load(&td->in_done) == true) {
            break;
        }

        td->out_swap = false;
        if (*td->swap_address_1 > *td->swap_address_2) {
            swap(td->swap_address_1, td->swap_address_2);
            td->out_swap = true;
        }

        atomic_fetch_add(&td->done_count[td->pass], 1);
        if (atomic_load(&td->done_count[td->pass]) == td->thread_count[td->pass]) {
            sem_post(&td->main_sem);
        }

        sem_wait(&td->done_sem[td->pass]);
    }

    pct_markthread_done();
    return 0;
}