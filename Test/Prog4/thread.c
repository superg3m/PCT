// -----------------------------------------------------------
// NAME : Jovanni Djonaj                                ID: M45770163
// DUE DATE : 01/30/2026
// PROGRAM ASSIGNMENT #4
// FILE NAME : thread.c
// PROGRAM PURPOSE:
// 	The purpose of this program is to test out passing the baton 
//	from the baby threads to the mom threads and so on when its the approiate time.
//  The appropriate time is when there is no more food left.
// -----------------------------------------------------------

#include <stdatomic.h>
#include <sched.h>

#include "thread.h"

int refill_count = 0; 

bool lock = false;
int next = 0;
int baby_id;

char* generate_space_for_baby(int id) {
	char* ret = (char*)malloc((sizeof(char) * id) + 1);
	for (int i = 0; i < id; i++) {
		ret[i] = ' ';
	}
	ret[id] = '\0';
	
	return ret;
}

#define BABY_OUT(id, fmt, ...) OUT("%s"fmt, generate_space_for_baby(id), ##__VA_ARGS__)

void delay_thread() {
	for(int i = 0; i < (rand() % 30) + 1; i++) {
		sched_yield();
	}
}

// NOTE(Jovanni): This must be used with a mutex maybe I should just put the mutex in here?
bool is_empty(ThreadData* data) {
	for (int i = 0; i < data->foodpot_count; i++) {
		if (data->food[i]) return false;
	}

	return true;
}

// NOTE(Jovanni): This must be used with a mutex maybe I should just put the mutex in here?
void check_empty(BabyThreadData* baby_data) {
	ThreadData* data = baby_data->data;
	if (is_empty(data) && !lock) {
		lock = true;
		baby_id = baby_data->baby_id;
		BABY_OUT(baby_data->baby_id, "Baby eagle %d sees all feeding pots are empty and wakes up the mother.\n", baby_id);
		sem_post(&data->wake_mom);
	}
}

void goto_sleep(ThreadData* data) {
	OUT("Mother eagle takes a nap.\n");
	sem_wait(&data->wake_mom);
}

void food_read(ThreadData* data) {
	pthread_mutex_lock(&data->mutex);
		OUT("Mother eagle is awoke by baby eagle %d and starts preparing food.\n", baby_id);
		
		refill_count += 1;
		OUT("Mother eagle says Feeding (%d)\n", refill_count);
		
		for (int i = 0; i < data->foodpot_count; i++) {
			data->food[i] = true;
			sem_post(&data->food_sems[i]);
		}

		lock = false;
	pthread_mutex_unlock(&data->mutex);
}

void* mom_thread(void* arg) {
	MomThreadData* mom_data = (MomThreadData*)arg;
	ThreadData* data = mom_data->data;
	OUT("Mother eagle started.\n");

	while (refill_count < data->max_refill_count) {
		goto_sleep(data);
		delay_thread();
		
		food_read(data);
		delay_thread();
	}

	for (int i = 0; i < data->baby_eagle_count; i++) {
		sem_post(&data->food_sems[i % data->foodpot_count]);
	}
		
	for (int i = 0; i < data->baby_eagle_count; i++) {
		pthread_join(mom_data->baby_thread_ids[i], NULL);
	}

	OUT("Mother eagle retires after serving %d feedings. Game is over!!!\n", refill_count);

	pct_markthread_done();
	return NULL;
}

int ready_to_eat(BabyThreadData* baby_data) {
	pthread_mutex_lock(&baby_data->data->mutex);
		int index = next++ % baby_data->data->foodpot_count;
		BABY_OUT(baby_data->baby_id, "Baby eagle %d is ready to eat!\n", baby_data->baby_id);
		check_empty(baby_data);
	pthread_mutex_unlock(&baby_data->data->mutex);

	sem_wait(&baby_data->data->food_sems[index]);
		
	return index;
}

void finish_eating(BabyThreadData* baby_data, int index) {
	sem_wait(&baby_data->data->foodpot[index]);
		pthread_mutex_lock(&baby_data->data->mutex);

		// NOTE(Jovanni): This acts as a kill switch if the refill_count is >= than the max refill_count
		// and the food is not available then you know you can safely just bale out.
		if ((baby_data->data->food[index] == false) && (refill_count >= baby_data->data->max_refill_count)) {
			pthread_mutex_unlock(&baby_data->data->mutex);
			pthread_exit(0);
		} else {
			pthread_mutex_unlock(&baby_data->data->mutex);
		}
		
		delay_thread();

		pthread_mutex_lock(&baby_data->data->mutex);
			BABY_OUT(baby_data->baby_id, "Baby eagle %d is eating using feeding pot %d.\n", baby_data->baby_id, index + 1);
			baby_data->data->food[index] = false;
			check_empty(baby_data);
		pthread_mutex_unlock(&baby_data->data->mutex);
		
		BABY_OUT(baby_data->baby_id, "Baby eagle %d finishes eating.\n", baby_data->baby_id);
	sem_post(&baby_data->data->foodpot[index]);
}

void* baby_thread(void* arg) {
	BabyThreadData* baby_data = (BabyThreadData*)arg;
	ThreadData* data = baby_data->data;
	BABY_OUT(baby_data->baby_id, "Baby eagle %d started.\n", baby_data->baby_id);

	// NOTE(Jovanni): This is techincally not necessary I just liked when the output
	// did not start until all threads are spun up.
	pthread_mutex_lock(&data->mutex);
		data->baby_started_count += 1;
		if (data->baby_started_count == data->baby_eagle_count) {
			sem_post(&data->wake_main);
		}
	pthread_mutex_unlock(&data->mutex);

	while (refill_count < data->max_refill_count) {
		delay_thread();
		int index = ready_to_eat(baby_data);
		delay_thread();
		finish_eating(baby_data, index);
	}

	pct_markthread_done();
	return NULL;
}