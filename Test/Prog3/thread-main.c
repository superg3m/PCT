// -----------------------------------------------------------
// NAME: Jovanni                                ID: M45770163
// DUE DATE: 01/30/2026
// PROGRAM ASSIGNMENT #3
// FILE NAME: thread-main.c
// PROGRAM PURPOSE:
//  The purpose of this program is to parallelize 
//  the even odd sorting algorithm by spinning up
//  threads and exiting them and then spinning up new ones
//  for every pass.
// -----------------------------------------------------------

#include "thread.h"

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

bool perfrom_pass(pthread_t* threads, ThreadData* data, int* x, int total_count, int pass) {
    int n = (total_count / 2);

    int index = 0;
    for (int i = pass; i < total_count - 1; i += 2) {
        data[index].swap_address_1 = &x[i];
        data[index].swap_index_1 = i;

        data[index].swap_address_2 = &x[i + 1];
        data[index].swap_index_2 = i + 1;

        index += 1;
    }

    for (int i = 0; i < n; i++) {
        pthread_create(&threads[i], NULL, thread_worker_sort, &data[i]);
        OUT("      Thread %d Created\n", *data[i].swap_address_1);
    }

    bool should_swap = false;
    for (int i = 0; i < n; i++) {
        void* return_code = NULL;
        pthread_join(threads[i], &return_code);
        if ((long long)return_code == 1) {
            should_swap = true;
        }

        OUT("      Thread %d Exists\n", *data[i].swap_address_1);
    }

    return should_swap;
}

int even_odd_sort(int* x, int total_count) {
    bool swapped = true;
    
    int thread_count = total_count / 2;
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
    ThreadData* data = (ThreadData*)malloc(sizeof(ThreadData) * thread_count);

    int iteration = 1;
    while (swapped) {
        OUT("Iteration %d:\n", iteration);
        OUT("   Even Pass:\n");
        bool even_swap = perfrom_pass(threads, data, x, total_count, EVEN);

        OUT("   Odd Pass:\n");
        bool odd_swap = perfrom_pass(threads, data, x, total_count, ODD);

        OUT("Result after iteration %d:\n", iteration);
        print_list(x, 0, total_count - 1, false);

        swapped = even_swap || odd_swap;
        if (swapped) {
            iteration += 1;
        }
    }

    free(threads);
    free(data);

    return iteration;
}

int main() {
    int n = 0;
    int success = scanf("%d", &n);
    if (success == EOF) {
        ERR("Expected number of array elements\n");
        return -1;
    }

    int* x = (int*)malloc(sizeof(int) * n);
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

    pct_init();
    print_list(x, 0, n - 1, false);
    
    int iteration = even_odd_sort(x, n);
    OUT("Final result after iteration %d:\n", iteration);
    print_list(x, 0, n - 1, false);

    pct_shutdown();
    free(x);
}