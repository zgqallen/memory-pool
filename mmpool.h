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
	struct mm_pool *next;		/* next sub pool */
	void *m_addr;			/* start address for this memory pool */
	unsigned int size;		/* total size of this pool */
	unsigned int free_size;		/* free size of this pool */
	unsigned int free_blocks[FREEMMB_BUCKET_SIZE]; /* bucket free blocks stats */
	MMB_LLE	*free_blocks_list[FREEMMB_BUCKET_SIZE]; /* bucket free blocks list for quick access*/
	pthread_mutex_t m_lock; 	/* mutex to protect memory allocation from the current pool */
	pthread_mutex_t g_lock; 	/* mutex to protect pool list, only for first pool */
}MM_POOL;

#define MM_POOL_LOCK(pool) pthread_mutex_lock(&pool->m_lock)
#define MM_POOL_UNLOCK(pool) pthread_mutex_unlock(&pool->m_lock)
#define MM_POOL_LIST_LOCK(pool) pthread_mutex_lock(&pool->g_lock)
#define MM_POOL_LIST_UNLOCK(pool) pthread_mutex_unlock(&pool->g_lock)

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

#endif// MMPOOL_H
