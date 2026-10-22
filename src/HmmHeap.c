/*
    File : HmmHeap.c
    Desc : Simulated heap area, simulated program break, heap state
           (tail block + explicit free list) and the heap checker.
*/

#include <stdint.h>

#include "HmmInternal.h"

/* ------------------------------------------------------------------ */
/* Simulated heap and program break                                    */
/* ------------------------------------------------------------------ */

/* The "heap": a static array that lives in the .bss segment, so no     */
/* physical memory is used until a page is touched (like a real heap). */
static char heapArea[HMM_HEAP_MAX] __attribute__((aligned(HMM_ALIGNMENT)));

/* Simulated program break: number of bytes of heapArea currently in    */
/* use. heapArea + programBreak is the first address beyond the heap.   */
static size_t programBreak = 0U;

static size_t growCount   = 0U;
static size_t shrinkCount = 0U;

/* Heap state. The heap is a chain of blocks (next/prev) that tiles     */
/* heapArea[0 .. programBreak) with no gaps.                            */
static block_t *heapTail = NULL;
static block_t *freeHead = NULL;

void *HmmBreakGrow(size_t bytes)
{
  char *oldBreak;

  if (bytes > (HMM_HEAP_MAX - programBreak))
  {
    return NULL;                       /* heap area exhausted */
  }

  oldBreak = heapArea + programBreak;
  programBreak += bytes;
  growCount++;
  return oldBreak;
}

int HmmBreakShrink(size_t bytes)
{
  if (bytes > programBreak)
  {
    return -1;                         /* cannot go below the heap start */
  }

  programBreak -= bytes;
  shrinkCount++;
  return 0;
}

char *HmmHeapStart(void)
{
  return heapArea;
}

char *HmmHeapBreak(void)
{
  return heapArea + programBreak;
}

size_t HmmBreakGrowCount(void)
{
  return growCount;
}

size_t HmmBreakShrinkCount(void)
{
  return shrinkCount;
}

/* ------------------------------------------------------------------ */
/* Tail block                                                          */
/* ------------------------------------------------------------------ */

block_t *HmmHeapTail(void)
{
  return heapTail;
}

void HmmHeapSetTail(block_t *block)
{
  heapTail = block;
}

/* ------------------------------------------------------------------ */
/* Explicit free list (doubly linked, links stored in the payload)     */
/* ------------------------------------------------------------------ */

static freeLinks_t *linksOf(block_t *block)
{
  return (freeLinks_t *)((char *)block + HMM_HEADER_SIZE);
}

void HmmFreeListInsert(block_t *block)
{
  freeLinks_t *links = linksOf(block);

  links->prevFree = NULL;
  links->nextFree = freeHead;
  if (freeHead != NULL)
  {
    linksOf(freeHead)->prevFree = block;
  }
  freeHead = block;
}

void HmmFreeListRemove(block_t *block)
{
  freeLinks_t *links = linksOf(block);

  if (links->prevFree != NULL)
  {
    linksOf(links->prevFree)->nextFree = links->nextFree;
  }
  else
  {
    freeHead = links->nextFree;
  }

  if (links->nextFree != NULL)
  {
    linksOf(links->nextFree)->prevFree = links->prevFree;
  }
}

block_t *HmmFreeListFirstFit(size_t payloadSize)
{
  block_t *cursor = freeHead;

  while (cursor != NULL)
  {
    if (cursor->size >= payloadSize)
    {
      return cursor;
    }
    cursor = linksOf(cursor)->nextFree;
  }

  return NULL;
}

/* ------------------------------------------------------------------ */
/* Pointer validation                                                  */
/* ------------------------------------------------------------------ */

block_t *HmmBlockFromPtr(void *ptr)
{
  uintptr_t address = (uintptr_t)ptr;
  uintptr_t lowest  = (uintptr_t)heapArea + HMM_HEADER_SIZE;
  uintptr_t highest = (uintptr_t)heapArea + programBreak;
  block_t *block;

  if ((address < lowest) || (address >= highest) ||
      ((address % HMM_ALIGNMENT) != 0U))
  {
    return NULL;                       /* not inside our heap */
  }

  block = (block_t *)(address - HMM_HEADER_SIZE);

  if (block->magic != HMM_MAGIC_USED)
  {
    return NULL;                       /* freed already, or not a block start */
  }

  if (block->size > (size_t)(highest - address))
  {
    return NULL;                       /* corrupted header */
  }

  return block;
}

/* ------------------------------------------------------------------ */
/* Heap checker (used by the tests)                                    */
/* ------------------------------------------------------------------ */

int HmmCheckHeap(void)
{
  char *cursor = heapArea;
  char *end    = heapArea + programBreak;
  block_t *previous = NULL;
  size_t freeInHeap = 0U;
  size_t freeInList = 0U;
  block_t *listPrev = NULL;
  block_t *entry;

  if (programBreak == 0U)
  {
    return ((heapTail == NULL) && (freeHead == NULL)) ? 0 : -1;
  }

  while (cursor < end)
  {
    block_t *block = (block_t *)cursor;
    char *following;

    if (block->prev != previous)                                   return -2;
    if ((block->magic != HMM_MAGIC_USED) &&
        (block->magic != HMM_MAGIC_FREE))                          return -3;
    if (((block->size % HMM_ALIGNMENT) != 0U) ||
        (block->size < HMM_MIN_PAYLOAD))                           return -4;
    if ((block->magic == HMM_MAGIC_FREE) && (previous != NULL) &&
        (previous->magic == HMM_MAGIC_FREE))                       return -5;

    if (block->magic == HMM_MAGIC_FREE)
    {
      freeInHeap++;
    }

    if (block->size > (size_t)(end - cursor) - HMM_HEADER_SIZE)    return -6;
    following = cursor + HMM_HEADER_SIZE + block->size;

    if (following == end)
    {
      if ((heapTail != block) || (block->next != NULL))            return -7;
    }
    else if (block->next != (block_t *)following)                  return -8;

    previous = block;
    cursor = following;
  }

  entry = freeHead;
  while (entry != NULL)
  {
    if (entry->magic != HMM_MAGIC_FREE)                            return -9;
    if (linksOf(entry)->prevFree != listPrev)                      return -10;
    freeInList++;
    if (freeInList > freeInHeap)                                   return -11;
    listPrev = entry;
    entry = linksOf(entry)->nextFree;
  }

  return (freeInList == freeInHeap) ? 0 : -12;
}
