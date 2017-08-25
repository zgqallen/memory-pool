#ifndef _MMPOOL_H
#define _MMPOOL_H

#include <pthread.h>
#include <stddef.h>

typedef struct mm_block
{
	void *pool;		/* pointer to pool belongs to, used for free */
	struct mm_block *prev;	/* prev block for quick merging */
	unsigned int size;	/* size of this block */
	int flags;		/* flag for this block */
	int padding[2];		/* padding to 32 bytes */
#define MMB_IN_USE 0x01		/* indicates block is in used */
	unsigned char align_base;/* start address for real data */
}MM_BLOCK;

#define MM_BLOCK_HEAD_SIZE offsetof(MM_BLOCK, align_base)

typedef struct mmb_lle
{
	struct mmb_lle *prev;
	struct mmb_lle *next;
	struct mmb_lle *tail;
	MM_BLOCK *mmb;
}MMB_LLE;

#define FREEMMB_BUCKET_SIZE 1025
typedef struct mm_pool
{
	struct mm_pool *main_pool;	/* link to main pool */
	int idx;			/* index map to location in pool_array */
	void *m_addr;			/* start address for this memory pool */
	unsigned int size;		/* total size of this pool */
	unsigned int free_size;		/* free size of this pool */
	unsigned int free_blocks[FREEMMB_BUCKET_SIZE]; /* bucket free blocks stats */
	MMB_LLE	*free_blocks_list[FREEMMB_BUCKET_SIZE]; /* bucket free blocks list for quick access*/
	pthread_mutex_t m_lock; 	/* mutex to protect memory allocation from the current pool */
	struct pool_meta *meta; 	/* only for first main pool */
}MM_POOL;

#define MAX_POOL_NUM 1024		/* assume the pool size not exceed 65G */
#define MAX_COUNTER_SIZE 10
typedef struct pool_meta
{
	MM_POOL *pool_array[MAX_POOL_NUM]; /* Pool array for all allocated pools. */
	int pool_weight[MAX_POOL_NUM];	   /* Weight for each pool based on freeblocks */
	int pool_len;			   /* Total number of current alloacted pools */
	pthread_rwlock_t g_lock;           /* rwlock to protect pool meta. */
	unsigned long long counter[MAX_COUNTER_SIZE];	   /* conter for internal error checking */
#define BLK_LIST_INS	 0
#define BLK_LIST_DEL	 1
#define POOL_PICK	 2
#define POOL_GET_MMB	 3
#define POOL_ALL_SIZE	 4
#define POOL_NUM	 5
#define POOL_ALLOC_SIZE  6
}POOL_META;

#define MM_POOL_LOCK(pool) pthread_mutex_lock(&pool->m_lock)
#define MM_POOL_UNLOCK(pool) pthread_mutex_unlock(&pool->m_lock)
#define MM_POOL_G_RDLOCK(pool) pthread_rwlock_rdlock(&pool->meta->g_lock)
#define MM_POOL_G_WRLOCK(pool) pthread_rwlock_wrlock(&pool->meta->g_lock)
#define MM_POOL_G_UNLOCK(pool) pthread_rwlock_unlock(&pool->meta->g_lock)

#ifndef ATOMIC_INC
#define ATOMIC_INC(ptr) __sync_add_and_fetch(ptr, 1) 
#endif
#ifndef ATOMIC_DEC
#define ATOMIC_DEC(ptr) __sync_sub_and_fetch(ptr, 1)
#endif
#ifndef ATOMIC_ADD
#define ATOMIC_ADD(ptr, incr) __sync_add_and_fetch(ptr, incr)
#endif
#ifndef ATOMIC_SUB
#define ATOMIC_SUB(ptr, incr) __sync_sub_and_fetch(ptr, incr)
#endif
#ifndef ATOMIC_INC_BIGINT
#define ATOMIC_INC_BIGINT(ptr) __sync_add_and_fetch(ptr, 1)
#endif


/*
** MMPOOL_INIT
** Purpose:
**	Initialize a shareable memory pool for memory allocation.
**
** Parameters:
**	None
** 
** Returns:
**	The pointer of memory pool entry.
*/
MM_POOL *mmpool_init(void);


/*
** MMPOOL_DESTROY
** Purpose:
**      Destroy a shareable memory pool and release the memory to OS.
**
** Parameters:
**      MM_POOL *pool
**		the entry of the memory pool.
** 
** Returns:
**      None
*/
void mmpool_destroy(MM_POOL *pool);

/*
** MMPOOL_MALLOC
** Purpose:
**      Allocate specific size of memory from the memory pool.
**
** Parameters:
**      MM_POOL *pool
**              the entry of the memory pool.
**	unsigned int size
**		specific size of memory to be allocated, the memory was in size
**	align with 32 bytes internally, so more size of memory will be alloacted.
** 
** Returns:
**      The pointer of the alloacted memory.
*/
void *mmpool_malloc(MM_POOL *pool, unsigned int size);

/*
** MMPOOL_FREE
** Purpose:
**      Release the memory to the memory pool.
**
** Parameters:
**      void *addr
**              pointer of the memory to be freeed.
** 
** Returns:
**      The pointer of the alloacted memory.
*/
void mmpool_free(void *addr);

/*
** MMPOOL_DUMP
** Purpose:
**      Dump the stats of the memory pool.
**
** Parameters:
**      void *addr
**              pointer of the memory to be freeed.
** 
** Returns:
**      The pointer of the alloacted memory.
*/
void mmpool_dump(MM_POOL *pool);

void mmpool_dump_counter(MM_POOL *g_pool);

#endif
