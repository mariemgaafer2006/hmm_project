/*
    File : HmmFree.c
    Desc : Heap Memory Manager free function.

    HmmFree(ptr)
      1. ptr == NULL                -> nothing to do.
      2. Validate ptr, find header  -> invalid / double free is reported
                                       and ignored (the heap stays intact).
      3. Mark the block free, merge it with the free block after it and
         the free block before it (coalescing).
      4. If the merged block is the last block of the heap and big enough,
         lower the real program break.
*/

#include <unistd.h>

#include "Hmm.h"
#include "HmmInternal.h"

/* printf/malloc must not be used inside the allocator (the wrappers    */
/* would call us back), so errors are reported with write().            */
static void reportError(const char *message, size_t length)
{
  ssize_t written = write(STDERR_FILENO, message, length);

  (void)written;                       /* nothing more can be done on failure */
}

/* Lowers the program break if the last block of the heap is a big free */
/* block. One STEP is kept so repeated alloc/free do not move the break */
/* back and forth.                                                      */
static void trimHeapEnd(block_t *block)
{
  size_t span;
  size_t release;

  if (block != HmmHeapTail())
  {
    return;
  }

  span = HMM_HEADER_SIZE + block->size;
  if (span < HMM_SHRINK_THRESHOLD)
  {
    return;
  }

  release = span - HMM_BREAK_STEP;
  if (HmmBreakShrink(release) == 0)
  {
    block->size -= release;
  }
  /* on failure the block simply stays larger: the heap is still valid */
}

void HmmReleaseBlock(block_t *block)
{
  block_t *next = block->next;
  block_t *prev = block->prev;

  block->magic = HMM_MAGIC_FREE;

  /* Merge with the following block if it is free. */
  if ((next != NULL) && (next->magic == HMM_MAGIC_FREE))
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
    next->magic = 0U;                  /* this header no longer exists */
  }

  /* Merge with the previous block if it is free (it is already in the  */
  /* free list, so it only grows).                                       */
  if ((prev != NULL) && (prev->magic == HMM_MAGIC_FREE))
  {
    prev->size += HMM_HEADER_SIZE + block->size;
    prev->next = block->next;
    if (prev->next != NULL)
    {
      prev->next->prev = prev;
    }
    else
    {
      HmmHeapSetTail(prev);
    }
    block->magic = 0U;
    block = prev;
  }
  else
  {
    HmmFreeListInsert(block);
  }

  trimHeapEnd(block);
}

void HmmFree(void *ptr)
{
  static const char message[] = "HMM: invalid pointer or double free ignored\n";
  block_t *block;

  if (ptr == NULL)
  {
    return;
  }

  block = HmmBlockFromPtr(ptr);
  if (block == NULL)
  {
    reportError(message, sizeof(message) - 1U);
    return;
  }

  HmmReleaseBlock(block);
}
