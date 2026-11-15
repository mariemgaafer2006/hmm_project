#!/bin/sh
# Runs the three scenarios of free_and_sbrk and checks the expected result:
# only the last one (freeing the blocks at the END of the heap) may lower
# the real program break.

APP=./build/free_and_sbrk
status=0

check_shrink() {
  # $1 = description, $2 = "yes"/"no" (shrink expected), rest = arguments
  description=$1
  expected=$2
  shift 2
  echo "=== $description: free_and_sbrk $*"
  out=$($APP "$@") || { echo "FAILED to run"; status=1; return; }
  echo "$out"
  shrinks=$(echo "$out" | tail -n 1 | sed 's/.*shrink calls \([0-9]*\).*/\1/')
  if [ "$expected" = "yes" ] && [ "$shrinks" -gt 0 ]; then
    echo "-> OK: break was lowered"
  elif [ "$expected" = "no" ] && [ "$shrinks" -eq 0 ]; then
    echo "-> OK: break was not lowered"
  else
    echo "-> FAILED (shrink calls = $shrinks, expected: $expected)"
    status=1
  fi
  echo
}

check_shrink "free every 2nd block"        no  1000 10240 2 1 1000
check_shrink "free all but the last block" no  1000 10240 1 1 999
check_shrink "free the blocks at the end"  yes 1000 10240 1 500 1000
check_shrink "free everything"             yes 1000 10240 1 1 1000

exit $status
