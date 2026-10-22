/*
    File : stress_test.c
    Desc : Edge cases + randomised stress test for HMM.

    This program calls the Hmm* functions directly (it is NOT linked with
    heapWrappers.c), so printf() keeps using the C library allocator and
    the heap checker HmmCheckHeap() can validate HMM's internal state.
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Hmm.h"
#include "HmmInternal.h"

#define CHECK(condition)                                                   \
  do                                                                       \
  {                                                                        \
    if (!(condition))                                                      \
    {                                                                      \
      printf("CHECK FAILED at line %d: %s\n", __LINE__, #condition);       \
      exit(EXIT_FAILURE);                                                  \
    }                                                                      \
  } while (0)

#define CHECK_HEAP() CHECK(HmmCheckHeap() == 0)

/* ------------------------------------------------------------------ */
/* Edge cases                                                          */
/* ------------------------------------------------------------------ */

static size_t heapSize(void)
{
  return (size_t)(HmmHeapBreak() - HmmHeapStart());
}

static void testBasic(void)
{
  void *p;
  void *q;
  int dummy = 0;

  /* size 0 returns a valid, unique pointer */
  p = HmmAlloc(0);
  CHECK(p != NULL);
  CHECK(((uintptr_t)p % HMM_ALIGNMENT) == 0U);
  CHECK(hmmSize(p) >= 1U);
  HmmFree(p);
  CHECK_HEAP();

  /* impossible sizes fail cleanly and leave the heap intact */
  errno = 0;
  CHECK(HmmAlloc((size_t)-1) == NULL);
  CHECK(errno == ENOMEM);
  CHECK(HmmAlloc(HMM_HEAP_MAX) == NULL);
  CHECK(HmmAlloc(HMM_HEAP_MAX - 8U) == NULL);
  CHECK_HEAP();

  /* free(NULL) is a no-op */
  HmmFree(NULL);
  CHECK_HEAP();

  /* freed memory is reused (first fit), same address comes back */
  p = HmmAlloc(100);
  HmmFree(p);
  q = HmmAlloc(100);
  CHECK(p == q);
  HmmFree(q);
  CHECK_HEAP();

  /* hmmSize */
  p = HmmAlloc(100);
  CHECK(hmmSize(p) >= 100U);
  CHECK(hmmSize(NULL) == 0U);
  CHECK(hmmSize(&dummy) == 0U);
  HmmFree(p);

  printf("  basic cases ........................ ok\n");
}

static void testInvalidFree(void)
{
  void *p = HmmAlloc(64);
  int onStack = 0;

  printf("  (two 'HMM: invalid pointer' messages are expected below)\n");
  HmmFree(p);
  HmmFree(p);              /* double free: reported and ignored */
  HmmFree(&onStack);       /* foreign pointer: reported and ignored */
  CHECK_HEAP();

  /* the heap still works after the invalid calls */
  p = HmmAlloc(64);
  CHECK(p != NULL);
  HmmFree(p);
  CHECK_HEAP();

  printf("  double free / foreign pointer ...... ok\n");
}

static void testCoalescing(void)
{
  void *a[3];
  size_t sizeBefore;
  int orders[3][3] = { {0, 1, 2}, {2, 1, 0}, {0, 2, 1} };

  for (int order = 0; order < 3; order++)
  {
    sizeBefore = heapSize();
    for (int i = 0; i < 3; i++)
    {
      a[i] = HmmAlloc(1000);
      CHECK(a[i] != NULL);
    }
    for (int i = 0; i < 3; i++)
    {
      HmmFree(a[orders[order][i]]);
      CHECK_HEAP();
    }
    /* everything merged back: one single free block covers the heap */
    CHECK(HmmHeapTail() != NULL);
    CHECK(HmmHeapTail()->prev == NULL);
    CHECK(HmmHeapTail()->magic == HMM_MAGIC_FREE);
    CHECK(heapSize() == sizeBefore);   /* and the break did not move */
  }

  printf("  coalescing (3 free orders) ......... ok\n");
}

static void testBreakSteps(void)
{
  enum { COUNT = 2000 };
  static void *p[COUNT];
  size_t grow0 = HmmBreakGrowCount();
  size_t bytes = 0U;

  for (int i = 0; i < COUNT; i++)
  {
    p[i] = HmmAlloc(100);
    CHECK(p[i] != NULL);
    bytes += HMM_HEADER_SIZE + HMM_ALIGN_UP(100U);
  }

  /* far fewer break moves than allocations: one per 128KB step */
  size_t grows = HmmBreakGrowCount() - grow0;
  CHECK(grows <= (bytes / HMM_BREAK_STEP) + 1U);
  printf("  %d allocations moved the break %zu time(s)\n", COUNT, grows);

  for (int i = 0; i < COUNT; i++)
  {
    HmmFree(p[i]);
  }
  CHECK_HEAP();
  CHECK(heapSize() < HMM_SHRINK_THRESHOLD);   /* the break came back down */

  printf("  break stepping + shrink ............ ok\n");
}

static void testCalloc(void)
{
  unsigned char *p;

  /* dirty a block first so calloc really has to clear it */
  p = HmmAlloc(512);
  memset(p, 0xAB, 512);
  HmmFree(p);

  p = hmmCalloc(64, 8);
  CHECK(p != NULL);
  for (int i = 0; i < 512; i++)
  {
    CHECK(p[i] == 0U);
  }
  HmmFree(p);

  errno = 0;
  CHECK(hmmCalloc((size_t)-1 / 2U, 4U) == NULL);   /* overflow */
  CHECK(errno == ENOMEM);
  p = hmmCalloc(0U, 10U);                          /* valid, like malloc(0) */
  CHECK(p != NULL);
  HmmFree(p);
  CHECK_HEAP();

  printf("  calloc (zeroing, overflow) ......... ok\n");
}

static void testRealloc(void)
{
  unsigned char *p;
  unsigned char *q;
  unsigned char *big;

  /* realloc(NULL, n) acts like malloc */
  p = hmmReAlloc(NULL, 100);
  CHECK(p != NULL);
  memset(p, 0x5A, 100);

  /* shrinking keeps the same pointer and the data */
  q = hmmReAlloc(p, 40);
  CHECK(q == p);
  for (int i = 0; i < 40; i++)
  {
    CHECK(q[i] == 0x5A);
  }
  CHECK_HEAP();

  /* growing keeps the data (in place or moved) */
  q = hmmReAlloc(q, 5000);
  CHECK(q != NULL);
  for (int i = 0; i < 40; i++)
  {
    CHECK(q[i] == 0x5A);
  }
  memset(q, 0x33, 5000);
  CHECK_HEAP();

  /* a failing realloc leaves the original block valid */
  errno = 0;
  big = hmmReAlloc(q, HMM_HEAP_MAX);
  CHECK(big == NULL);
  CHECK(errno == ENOMEM);
  CHECK(hmmSize(q) >= 5000U);
  for (int i = 0; i < 5000; i++)
  {
    CHECK(q[i] == 0x33);
  }
  CHECK_HEAP();

  /* realloc to 0 frees */
  CHECK(hmmReAlloc(q, 0) == NULL);
  CHECK(hmmSize(q) == 0U);
  CHECK_HEAP();

  /* growing into the free block that follows (in place) */
  p = HmmAlloc(100);
  q = HmmAlloc(100);
  HmmFree(q);
  q = hmmReAlloc(p, 150);
  CHECK(q == p);
  HmmFree(q);
  CHECK_HEAP();

  printf("  realloc (shrink, grow, fail, 0) .... ok\n");
}

/* ------------------------------------------------------------------ */
/* Randomised stress test                                              */
/* ------------------------------------------------------------------ */

#define SLOTS        (1500)
#define OPERATIONS   (300000)
#define LIVE_LIMIT   (24U * 1024U * 1024U)

typedef struct
{
  unsigned char *ptr;
  size_t size;
  unsigned char pattern;
} slot_t;

static slot_t slots[SLOTS];
static size_t liveBytes = 0U;
static uint64_t rngState = 0x9E3779B97F4A7C15ULL;

static uint64_t nextRandom(void)
{
  rngState ^= rngState << 13;
  rngState ^= rngState >> 7;
  rngState ^= rngState << 17;
  return rngState;
}

static size_t randomSize(void)
{
  uint64_t r = nextRandom() % 1000U;

  if (r < 700U)  return 1U + (size_t)(nextRandom() % 256U);
  if (r < 950U)  return 257U + (size_t)(nextRandom() % 7936U);
  if (r < 995U)  return 8193U + (size_t)(nextRandom() % 57344U);
  return 65537U + (size_t)(nextRandom() % 300000U);
}

static void verifyPattern(const slot_t *slot, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (slot->ptr[i] != slot->pattern)
    {
      printf("DATA CORRUPTION: byte %zu of a %zu byte block\n", i, slot->size);
      exit(EXIT_FAILURE);
    }
  }
}

static void verifyAll(void)
{
  for (int i = 0; i < SLOTS; i++)
  {
    if (slots[i].ptr != NULL)
    {
      verifyPattern(&slots[i], slots[i].size);
    }
  }
}

static void stressTest(void)
{
  size_t allocs = 0U, frees = 0U, reallocs = 0U, callocs = 0U;
  size_t maxHeap = 0U;

  for (long op = 0; op < OPERATIONS; op++)
  {
    slot_t *slot = &slots[nextRandom() % SLOTS];
    uint64_t choice = nextRandom() % 100U;

    if (slot->ptr == NULL)
    {
      size_t size = randomSize();
      int useCalloc = (choice < 15U);

      if ((liveBytes + size) > LIVE_LIMIT)
      {
        continue;
      }

      slot->ptr = useCalloc ? hmmCalloc(1U, size) : HmmAlloc(size);
      CHECK(slot->ptr != NULL);
      CHECK(((uintptr_t)slot->ptr % HMM_ALIGNMENT) == 0U);
      CHECK(hmmSize(slot->ptr) >= size);

      slot->size = size;
      slot->pattern = (unsigned char)(1U + (nextRandom() % 255U));
      if (useCalloc)
      {
        for (size_t i = 0; i < size; i++)
        {
          CHECK(slot->ptr[i] == 0U);
        }
        callocs++;
      }
      memset(slot->ptr, slot->pattern, size);
      liveBytes += size;
      allocs++;
    }
    else if (choice < 55U)
    {
      verifyPattern(slot, slot->size);
      HmmFree(slot->ptr);
      liveBytes -= slot->size;
      slot->ptr = NULL;
      frees++;
    }
    else
    {
      size_t newSize = randomSize();
      size_t keep = (newSize < slot->size) ? newSize : slot->size;
      unsigned char *newPtr;

      if ((liveBytes - slot->size + newSize) > LIVE_LIMIT)
      {
        continue;
      }

      newPtr = hmmReAlloc(slot->ptr, newSize);
      CHECK(newPtr != NULL);
      slot->ptr = newPtr;
      verifyPattern(slot, keep);       /* old data must survive */
      memset(slot->ptr, slot->pattern, newSize);
      liveBytes = liveBytes - slot->size + newSize;
      slot->size = newSize;
      reallocs++;
    }

    if ((op % 500) == 0)
    {
      CHECK_HEAP();
    }
    if ((op % 20000) == 0)
    {
      verifyAll();
    }
    if (heapSize() > maxHeap)
    {
      maxHeap = heapSize();
    }
  }

  verifyAll();
  CHECK_HEAP();

  for (int i = 0; i < SLOTS; i++)
  {
    if (slots[i].ptr != NULL)
    {
      HmmFree(slots[i].ptr);
      slots[i].ptr = NULL;
    }
  }
  CHECK_HEAP();

  /* everything merged into one free block and the break came back down */
  CHECK(HmmHeapTail()->prev == NULL);
  CHECK(HmmHeapTail()->magic == HMM_MAGIC_FREE);
  CHECK(heapSize() < HMM_SHRINK_THRESHOLD);

  printf("  stress: %zu allocs (%zu calloc), %zu frees, %zu reallocs\n",
         allocs, callocs, frees, reallocs);
  printf("  stress: max heap %zu KB, break grow calls %zu, shrink calls %zu\n",
         maxHeap / 1024U, HmmBreakGrowCount(), HmmBreakShrinkCount());
  printf("  random stress test ................. ok\n");
}

int main(void)
{
  printf("HMM edge cases and stress test\n");

  testBasic();
  testInvalidFree();
  testCoalescing();
  testBreakSteps();
  testCalloc();
  testRealloc();
  stressTest();

  printf("ALL TESTS PASSED\n");
  return EXIT_SUCCESS;
}
