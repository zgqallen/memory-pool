#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "mmpool.h"

#define TH_NUM 20
MM_POOL *g_static_pool;

#define IDX 4096
#define S_IDX 1

#ifdef GLIBC
MM_POOL* mmpool_init() {return NULL;}
#define mmpool_destroy(a, b)
#define mmpool_malloc(a, b) malloc(b)
#define mmpool_free(a) free(a)
#endif

void *p_malloc_func(void *arg)
{
	int ppid = *(int*)arg;
	int idx;
	unsigned char *addr[IDX];

	printf("thread %d starting...\n", ppid);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool, idx);
		memset(addr[idx], 15, idx);
	}

	//dump_mmpool(g_static_pool);
	sleep(1);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		mmpool_free(addr[idx]);
	}

	//dump_mmpool(g_static_pool);
	sleep(1);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool, idx * 3);
		memset(addr[idx], 15, idx * 3);
	}

	//dump_mmpool(g_static_pool);
	sleep(1);
	for(idx = IDX-1; idx >= S_IDX; idx--)
	{
		mmpool_free(addr[idx]);
	}

	//dump_mmpool(g_static_pool);
	printf("thread %d exit.\n", ppid);
}


int main(int argc, char *argv[])
{
	int idx;
	pthread_t th[TH_NUM];

	g_static_pool = mmpool_init();

	for(idx = 0; idx < TH_NUM; idx++)
	{
		int ppid = idx;
		pthread_create(&th[idx], 0, p_malloc_func, (void*)&ppid);
		sleep(1);
	}
	

	for(idx = 0; idx < TH_NUM; idx++)
	{
		pthread_join(th[idx], NULL);
	}

	mmpool_dump(g_static_pool);
	mmpool_destroy(g_static_pool);
	return 0;
}
