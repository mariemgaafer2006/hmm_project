/*
    File : free_and_sbrk.c
    Desc : Test if free() lowers the (simulated) program break.

    This is the experiment of Listing 7-1 in "The Linux Programming
    Interface" (same command line, same idea), written for HMM: instead of
    sbrk(0) it prints the SIMULATED program break of the allocator.

    Usage: free_and_sbrk num-allocs block-size [step [min [max]]]

    Try:   free_and_sbrk 1000 10240 2 1 1000   (break must NOT be lowered)
           free_and_sbrk 1000 10240 1 1 999    (break must NOT be lowered)
           free_and_sbrk 1000 10240 1 500 1000 (break must be lowered)
*/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HmmInternal.h"

#define MAX_ALLOCS 100000

static char *ptrs[MAX_ALLOCS];

static int parseInt(const char *text, const char *name)
{
  char *endPtr;
  long value;

  errno = 0;
  value = strtol(text, &endPtr, 0);
  if ((errno != 0) || (*endPtr != '\0') || (value <= 0) || (value > INT_MAX))
  {
    fprintf(stderr, "invalid %s: %s\n", name, text);
    exit(EXIT_FAILURE);
  }

  return (int)value;
}

static void printBreak(const char *label)
{
  printf("%-32s %p  (heap size %zu bytes, grow calls %zu, shrink calls %zu)\n",
         label, (void *)HmmHeapBreak(),
         (size_t)(HmmHeapBreak() - HmmHeapStart()),
         HmmBreakGrowCount(), HmmBreakShrinkCount());
}

int main(int argc, char *argv[])
{
  int freeStep;
  int freeMin;
  int freeMax;
  int blockSize;
  int numAllocs;
  int index;

  printf("\n");

  if ((argc < 3) || (strcmp(argv[1], "--help") == 0))
  {
    fprintf(stderr, "%s num-allocs block-size [step [min [max]]]\n", argv[0]);
    return EXIT_FAILURE;
  }

  numAllocs = parseInt(argv[1], "num-allocs");
  if (numAllocs > MAX_ALLOCS)
  {
    fprintf(stderr, "num-allocs > %d\n", MAX_ALLOCS);
    return EXIT_FAILURE;
  }

  blockSize = parseInt(argv[2], "block-size");
  freeStep = (argc > 3) ? parseInt(argv[3], "step") : 1;
  freeMin  = (argc > 4) ? parseInt(argv[4], "min") : 1;
  freeMax  = (argc > 5) ? parseInt(argv[5], "max") : numAllocs;

  if (freeMax > numAllocs)
  {
    fprintf(stderr, "free-max > num-allocs\n");
    return EXIT_FAILURE;
  }

  printBreak("Initial program break:");

  printf("Allocating %d*%d bytes\n", numAllocs, blockSize);
  for (index = 0; index < numAllocs; index++)
  {
    ptrs[index] = malloc((size_t)blockSize);
    if (ptrs[index] == NULL)
    {
      fprintf(stderr, "malloc failed for block %d\n", index);
      return EXIT_FAILURE;
    }
  }

  printBreak("Program break is now:");

  printf("Freeing blocks from %d to %d in steps of %d\n",
         freeMin, freeMax, freeStep);
  for (index = freeMin - 1; index < freeMax; index += freeStep)
  {
    free(ptrs[index]);
  }

  printBreak("After free(), program break is:");

  if (HmmCheckHeap() != 0)
  {
    fprintf(stderr, "heap check FAILED\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
