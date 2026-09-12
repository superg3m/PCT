// -----------------------------------------------------------
// NAME : Jovanni ID: M45770163
// DUE DATE : 01/30/2026
// PROGRAM ASSIGNMENT #2
// FILE NAME : main.c
// PROGRAM PURPOSE:
//  The purpose of this program is to parallelize the even odd sorting algorithm
//  DO MORE HERE
// -----------------------------------------------------------

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "thread.h"

#define ArrayCount(arr) (sizeof(arr) / sizeof(arr[0]))

#define BUFFER_SIZE 0x800
#define OUT(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); write(STDOUT_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); } while(false)
#define ERR(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); write(STDERR_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); } while(false)
// NOTE(Jovanni): OUT() and ERR() are nice helper macros that avoid variable shadowing by using #__LINE__
// it also stack allocates a character array.

// NOTE(Jovanni): Helper function to append to a string from my personal library
void str_append(char* str, int* str_length_out, int str_capacity, const char* to_insert, int to_insert_length) {
    assert(str);
    assert(to_insert);
    assert(str_length_out);

    int new_length = *str_length_out + to_insert_length;
    if (new_length >= str_capacity) {
        ERR("ckg_str_insert: str_capacity is %d but new valid cstring length is %d + %d + 1(null_term) = %d\n", str_capacity, *str_length_out, to_insert_length, new_length + 1);
        assert(false);
    }
    
    char* copy_dest_ptr = (char*)(str + *str_length_out);
    memcpy(copy_dest_ptr, to_insert, to_insert_length);

    *str_length_out += to_insert_length;
}

void print_list(int* arr, int low, int high, bool include_indent) {
    char top_buffer[BUFFER_SIZE];

    int buffer_length = 0;

    if (include_indent) {
        str_append(top_buffer, &buffer_length, BUFFER_SIZE, "   ", sizeof("   ") - 1);
    }

    for (int i = low; i < high + 1; i++) {
        char temp_buffer[BUFFER_SIZE];
        memset(temp_buffer, 0, BUFFER_SIZE); 

        int temp_length = sprintf(temp_buffer, "   %d", arr[i]);
        str_append(top_buffer, &buffer_length, BUFFER_SIZE, temp_buffer, temp_length);
    }
    top_buffer[buffer_length] = '\0';

    OUT("%s\n", top_buffer);
}

void evenOddSort(int* x, int n) {
    bool swapped = true;
    int pass = EVEN;

    int thread_count[2];
    thread_count[EVEN] = (n + 1) / 2;
    thread_count[ODD] = thread_count[EVEN] - 1;

    int total_thread_count = thread_count[EVEN] + thread_count[ODD];

    sem_t start_sem[2];
    sem_init(&start_sem[EVEN], 0, 0);
    sem_init(&start_sem[ODD], 0, 0);

    sem_t done_sem[2];
    sem_init(&done_sem[EVEN], 0, 0);
    sem_init(&done_sem[ODD], 0, 0);

    sem_t main_sem;
    sem_init(&main_sem, 0, 0);

    atomic_int done_count[2];
    done_count[EVEN] = 0;
    done_count[ODD] = 0;

    pthread_t* threads = malloc(sizeof(pthread_t) * total_thread_count);
    ThreadData* data = malloc(sizeof(ThreadData) * total_thread_count);
    
    for (int i = 0; i < total_thread_count; i++) {
        data[i].swap_address_1 = &x[i];
        data[i].swap_address_2 = &x[i + 1];
        data[i].in_done = false;

        data[i].thread_count = thread_count;
        data[i].start_sem = start_sem;
        data[i].done_sem = done_sem;
        data[i].done_count = done_count;
        data[i].main_sem = main_sem;

        data[i].pass = ((i % 2) == 0) ? EVEN : ODD;    
        data[i].out_swap = false;

        pthread_create(&threads[i], NULL, thread_worker_sort, &data[i]);
    }

    while (swapped) {
        for (int i = 0; i < thread_count[pass]; i++) {
            sem_post(&start_sem[pass]);
        }

        while (done_count[pass] != thread_count[pass]) {
            sem_wait(&main_sem);
        }
        atomic_store(&done_count[pass], 0);
        
        for (int i = 0; i < thread_count[pass]; i++) {
            sem_post(&done_sem[pass]);
        }
        
        bool should_swap = false;
        for (int i = 0; i < total_thread_count; i++) {
            if (data[i].out_swap == true) { 
                should_swap = true;
            }

            data[i].out_swap = false;
        }

        swapped = should_swap;
        pass = !pass;
    }

    for (int i = 0; i < total_thread_count; i++) {
        int pass = (i % 2) == 0 ? EVEN : ODD;
        atomic_store(&data[i].in_done, true);

        // NOTE(Jovanni): Double signalling because its possible for a signal to get wasted
        // when the semaphore is less than zero.
        sem_post(&start_sem[pass]);
        sem_post(&start_sem[pass]);
        sem_post(&done_sem[pass]);
        sem_post(&done_sem[pass]);
    }

    for (int i = 0; i < total_thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(data);
}

int main() {
    int n = 0;
    int success = scanf("%d", &n);
    if (success == EOF) {
        ERR("Expected number of array elements\n");
        return -1;
    }

    int* x = malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) {
        success = scanf("%d", &x[i]);
        if (success == EOF) {
            ERR("%d numbers expected, but only %d given\n", n, i);
            return -1;
        }
    }

    OUT(
        "Concurrent Even-Odd Sort\n\n"
        "Number of input data = %d\n"
        "Input array:\n",
        n
    );

    print_list(x, 0, n - 1, true);
    
    pct_init();
    
    evenOddSort(x, n);
    printf("[ ");
    for (int i = 0; i < n; i++) {
        printf("%d ", x[i]);
    }
    printf("]\n");
    
    pct_shutdown();
    // free(x);
}