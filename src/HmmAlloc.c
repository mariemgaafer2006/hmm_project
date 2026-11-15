/*
    File : HmmAlloc.c
    Desc : Heap Memory Manager allocation functions.

    HmmAlloc(size)
      1. Round the size up (multiple of 16, at least 16).
      2. First-fit search in the explicit free list.
      3. Nothing fits -> move the program break up (in 128KB steps),
         either by enlarging a free block at the end of the heap or by
         appending a new block.
      4. Split the block if the remainder can hold a header + minimum
         payload, the remainder goes back to the free list.
      5. Return the payload address (block + header size).
*/

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "Hmm.h"
#include "HmmInternal.h"

/* Converts a user size to the payload size we really reserve.          */
/* Returns -1 if the request can never be satisfied.                    */
static int normalizeSize(size_t size, size_t *payloadSize)
{
  if (size == 0U)
  {
    size = 1U;                         /* malloc(0) returns a valid pointer */
  }

  if (size > (SIZE_MAX - HMM_ALIGNMENT)) /* prevent alignment overflow */
  {
    return -1;
  }

  *payloadSize = HMM_ALIGN_UP(size);
  return 0;
}

/* Moves the program break up by at least minBytes, rounded up to a   */
/* multiple of HMM_BREAK_STEP. If that does not fit in the heap area,   */
/* the exact amount is tried. Returns the old break or NULL.            */
static void *growBreak(size_t minBytes, size_t *grown)
{
  size_t rounded = ((minBytes + HMM_BREAK_STEP - 1U) / HMM_BREAK_STEP) *
                   HMM_BREAK_STEP;
  void *oldBreak = HmmBreakGrow(rounded);

  if (oldBreak != NULL)
  {
    *grown = rounded;
    return oldBreak;
  }

  oldBreak = HmmBreakGrow(minBytes);
  if (oldBreak != NULL)
  {
    *grown = minBytes;
  }
  return oldBreak;
}

/* Makes sure the free list holds a block with at least payloadSize     */
/* bytes by growing the heap. Returns that block or NULL.              */
static block_t *extendHeap(size_t payloadSize)
{
  block_t *tail = HmmHeapTail();
  block_t *block;
  size_t grown;
  void *oldBreak;

  if ((tail != NULL) && (tail->magic == HMM_MAGIC_FREE))
  {
    /* The last block is free but too small: just make it bigger. */
    oldBreak = growBreak(payloadSize - tail->size, &grown);
    if (oldBreak == NULL)
    {
      return NULL;
    }
    tail->size += grown;
    return tail;
  }

  /* Append a brand new block at the old break. */
  oldBreak = growBreak(HMM_HEADER_SIZE + payloadSize, &grown);
  if (oldBreak == NULL)
  {
    return NULL;
  }

  block = (block_t *)oldBreak;
  block->size = grown - HMM_HEADER_SIZE;
  block->magic = HMM_MAGIC_FREE;
  block->prev = tail;
  block->next = NULL;
  if (tail != NULL)
  {
    tail->next = block;
  }
  HmmHeapSetTail(block);
  HmmFreeListInsert(block);
  return block;
}

/* Cuts block (already marked used) to payloadSize bytes if the rest is */
/* big enough to be a block of its own. The rest is released, which    */
/* also merges it with a free block that may follow.                    */
static void splitBlock(block_t *block, size_t payloadSize)
{
  size_t remaining = block->size - payloadSize;
  block_t *rest;

  if (remaining < (HMM_HEADER_SIZE + HMM_MIN_PAYLOAD))
  {
    return;                            /* too small, keep it as slack */
  }

  rest = (block_t *)((char *)block + HMM_HEADER_SIZE + payloadSize);
  rest->size = remaining - HMM_HEADER_SIZE;
  rest->prev = block;
  rest->next = block->next;
  if (block->next != NULL)
  {
    block->next->prev = rest;
  }
  else
  {
    HmmHeapSetTail(rest);
  }
  block->next = rest;
  block->size = payloadSize;

  HmmReleaseBlock(rest);
}

void *HmmAlloc(size_t size)
{
  size_t payloadSize;
  block_t *block;

  if (normalizeSize(size, &payloadSize) != 0)
  {
    errno = ENOMEM;
    return NULL;
  }

  block = HmmFreeListFirstFit(payloadSize);
  if (block == NULL)
  {
    block = extendHeap(payloadSize);
    if (block == NULL)
    {
      errno = ENOMEM;
      return NULL;
    }
  }

  HmmFreeListRemove(block);
  block->magic = HMM_MAGIC_USED;
  splitBlock(block, payloadSize);

  return (char *)block + HMM_HEADER_SIZE;
}

void *hmmCalloc(size_t nmemb, size_t size)
{
  size_t total;
  void *ptr;

  if ((nmemb != 0U) && (size > (SIZE_MAX / nmemb)))
  {
    errno = ENOMEM;                    /* nmemb * size overflows */
    return NULL;
  }

  total = nmemb * size;
  ptr = HmmAlloc(total);
  if (ptr != NULL)
  {
    memset(ptr, 0, total);
  }
  return ptr;
}

void *hmmReAlloc(void *ptr, size_t reqSize)
{
  size_t payloadSize;
  block_t *block;
  block_t *next;
  void *newPtr;

  if (ptr == NULL)
  {
    return HmmAlloc(reqSize);          /* realloc(NULL, n) == malloc(n) */
  }

  if (reqSize == 0U)
  {
    HmmFree(ptr);                      /* same behaviour as glibc */
    return NULL;
  }

  block = HmmBlockFromPtr(ptr);
  if (block == NULL)
  {
    errno = EINVAL;
    return NULL;
  }

  if (normalizeSize(reqSize, &payloadSize) != 0)
  {
    errno = ENOMEM;                    /* original block stays valid */
    return NULL;
  }

  /* 1) The block is already big enough: keep it, give back the extra. */
  if (block->size >= payloadSize)
  {
    splitBlock(block, payloadSize);
    return ptr;
  }

  /* 2) Grow in place by absorbing the free block that follows. */
  next = block->next;
  if ((next != NULL) && (next->magic == HMM_MAGIC_FREE) &&
      ((block->size + HMM_HEADER_SIZE + next->size) >= payloadSize))
  {
    HmmFreeListRemove(next);
    block->size += HMM_HEADER_SIZE + next->size;
    block->next = next->next;
    if (block->next != NULL)
    {
      block->next->prev = block;
    }
    else
    {
      HmmHeapSetTail(block);
    }
    next->magic = 0U;
    splitBlock(block, payloadSize);
    return ptr;
  }

  /* 3) Allocate a new block, copy, free the old one. If the allocation */
  /*    fails the old block is untouched and still valid.                */
  newPtr = HmmAlloc(reqSize);
  if (newPtr == NULL)
  {
    return NULL;
  }

  memcpy(newPtr, ptr, block->size);
  HmmFree(ptr);
  return newPtr;
}

size_t hmmSize(void *ptr)
{
  block_t *block = HmmBlockFromPtr(ptr);

  return (block != NULL) ? block->size : 0U;
}
