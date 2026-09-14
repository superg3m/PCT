// -----------------------------------------------------------
// NAME : Jovanni Djonaj                                ID: M45770163
// DUE DATE : 01/30/2026
// PROGRAM ASSIGNMENT #4
// FILE NAME : thread-main.c
// PROGRAM PURPOSE:
// 	The purpose of this program is to spin up all the threads namely: 
// 	mom_thread and m number of baby eagle threads.
// -----------------------------------------------------------

#include "thread.h"

pthread_mutex_t write_mutex;

// ./prog4 8 15 12
int main(int argc, char** argv) {
	pct_init();
	pthread_mutex_init(&write_mutex, NULL);
	if (argc < 4) {
		ERR("Usage: ./prog4 m n t\n");
		pct_shutdown();
		return -1;
	}

	ThreadData data = {0};
	data.foodpot_count = atoi(argv[1]);
	data.baby_eagle_count = atoi(argv[2]);
	data.max_refill_count = atoi(argv[3]);
	data.baby_started_count = 0;
	pthread_mutex_init(&data.mutex, NULL);
	sem_init(&data.wake_mom, 0, 0);
	sem_init(&data.wake_main, 0, 0);

	data.food = (bool*)malloc(sizeof(bool) * data.foodpot_count);
	data.foodpot = (sem_t*)malloc(sizeof(sem_t) * data.foodpot_count);
	data.food_sems = (sem_t*)malloc(sizeof(sem_t) * data.foodpot_count);
	for (int i = 0; i < data.foodpot_count; i++) {
		sem_init(&data.foodpot[i], 0, 1);
		sem_init(&data.food_sems[i], 0, 0);
		data.food[i] = false;
	}

	OUT("MAIN: There are %d baby eagles, %d feeding pots, and %d feedings\n", data.baby_eagle_count, data.foodpot_count, data.max_refill_count);
	OUT("MAIN: Game starts!!!!!\n");
	
	pthread_t* baby_thread_ids = (pthread_t*)malloc(sizeof(pthread_t) * data.baby_eagle_count);
	BabyThreadData* baby_data = (BabyThreadData*)malloc(sizeof(BabyThreadData) * data.baby_eagle_count);
	for (int i = 0; i < data.baby_eagle_count; i++) {
		baby_data[i].baby_id = i + 1;
		baby_data[i].data = &data;
		pthread_create(&baby_thread_ids[i], NULL, baby_thread, (void*)&baby_data[i]);
	}

	sem_wait(&data.wake_main);

	pthread_t mom_thread_id;
	MomThreadData mom_data;
	mom_data.baby_thread_ids = baby_thread_ids;
	mom_data.data = &data;
	pthread_create(&mom_thread_id, NULL, mom_thread, (void*)&mom_data);
	pthread_join(mom_thread_id, NULL);
	pct_shutdown();

	pthread_mutex_destroy(&write_mutex);
	pthread_mutex_destroy(&data.mutex);

	sem_destroy(&data.wake_mom);
	sem_destroy(&data.wake_main);
	for (int i = 0; i < data.foodpot_count; i++) {
		//sem_destroy(&data.foodpot[i]);
		//sem_destroy(&data.food_sems[i]);
	}

	free(data.food);
	free(data.foodpot);
	free(data.food_sems);

	free(baby_thread_ids);
	free(baby_data);

	return 0;
}