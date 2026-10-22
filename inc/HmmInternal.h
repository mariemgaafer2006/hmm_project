/*
    File : HmmInternal.h
    Desc : Internal definitions shared by the HMM source files
           (block header, simulated program break, free list helpers).
           Not part of the public API, applications include Hmm.h only.
*/

#ifndef HMM_INTERNAL_H
#define HMM_INTERNAL_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Tunable parameters                                                  */
/* ------------------------------------------------------------------ */

/* Size of the simulated heap area (the maximum value of the break).    */
#define HMM_HEAP_MAX          (64U * 1024U * 1024U)

/* The simulated break only moves in multiples of this step, so a lot   */
/* of small allocations cause one "break call" instead of many.         */
#define HMM_BREAK_STEP        (128U * 1024U)

/* The break is lowered only when the free block at the end of the heap */
/* spans at least this many bytes (hysteresis, avoids grow/shrink       */
/* ping-pong around the same point).                                    */
#define HMM_SHRINK_THRESHOLD  (2U * HMM_BREAK_STEP)

/* Every payload address is a multiple of this value.                   */
#define HMM_ALIGNMENT         (16U)

/* Smallest payload: a free block stores its two free-list pointers in  */
/* the payload, so the payload must be able to hold them.               */
#define HMM_MIN_PAYLOAD       (16U)

#define HMM_ALIGN_UP(x) \
  (((x) + (HMM_ALIGNMENT - 1U)) & ~(size_t)(HMM_ALIGNMENT - 1U))

/* Values stored in the header: they mark the block state and let us    */
/* reject pointers that were not returned by HmmAlloc (or were freed).  */
#define HMM_MAGIC_USED        ((size_t)0xA110C8EDA110C8EDULL)
#define HMM_MAGIC_FREE        ((size_t)0xF4EEF4EEF4EEF4EEULL)

/* ------------------------------------------------------------------ */
/* Block layout                                                        */
/* ------------------------------------------------------------------ */

/* Header placed immediately before the payload of every block.        */
typedef struct block
{
  size_t size;           /* payload size in bytes (multiple of 16)      */
  size_t magic;          /* HMM_MAGIC_USED or HMM_MAGIC_FREE            */
  struct block *next;    /* physically next block (higher address)      */
  struct block *prev;    /* physically previous block (lower address)   */
} block_t;

/* Stored inside the payload of FREE blocks only (explicit free list). */
typedef struct freeLinks
{
  block_t *nextFree;
  block_t *prevFree;
} freeLinks_t;

#define HMM_HEADER_SIZE (sizeof(block_t))

_Static_assert((sizeof(block_t) % HMM_ALIGNMENT) == 0U,
               "block header size must keep payloads aligned");
_Static_assert(sizeof(freeLinks_t) <= HMM_MIN_PAYLOAD,
               "free list links must fit in the smallest payload");
_Static_assert((HMM_BREAK_STEP % HMM_ALIGNMENT) == 0U,
               "break step must be a multiple of the alignment");

/* ------------------------------------------------------------------ */
/* Simulated program break (HmmHeap.c)                                 */
/* ------------------------------------------------------------------ */

/* Like sbrk(+bytes): returns the old break, or NULL if the heap area  */
/* is exhausted (the break is left unchanged).                          */
void *HmmBreakGrow(size_t bytes);

/* Like sbrk(-bytes): returns 0 on success, -1 on failure.             */
int HmmBreakShrink(size_t bytes);

char *HmmHeapStart(void);   /* lowest heap address                      */
char *HmmHeapBreak(void);   /* current simulated program break          */

/* Number of times the break was moved (statistics / testing).         */
size_t HmmBreakGrowCount(void);
size_t HmmBreakShrinkCount(void);

/* ------------------------------------------------------------------ */
/* Heap state and free list (HmmHeap.c)                                */
/* ------------------------------------------------------------------ */

block_t *HmmHeapTail(void);              /* last block of the heap      */
void     HmmHeapSetTail(block_t *block);

void     HmmFreeListInsert(block_t *block);
void     HmmFreeListRemove(block_t *block);
block_t *HmmFreeListFirstFit(size_t payloadSize);

/* Returns the header for a pointer given by HmmAlloc, or NULL if the  */
/* pointer is not a live allocation (foreign, freed, or corrupted).    */
block_t *HmmBlockFromPtr(void *ptr);

/* Consistency checker used by the tests: 0 = heap is healthy.         */
int HmmCheckHeap(void);

/* ------------------------------------------------------------------ */
/* Release logic (HmmFree.c)                                           */
/* ------------------------------------------------------------------ */

/* Marks a block free, merges it with free neighbours, puts it in the  */
/* free list and lowers the break if the heap end became large enough. */
void HmmReleaseBlock(block_t *block);

#endif /* HMM_INTERNAL_H */
