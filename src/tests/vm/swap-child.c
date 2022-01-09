/* Reads a 128 kB file into static data and "sorts" the bytes in
   it, using counting sort, a single-pass algorithm.  The sorted
   data is written back to the same file in-place. */

#include <debug.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "../../lib/string.h"

const char *test_name = "swap-child";

#define ONE_MB (1024*1024)
#define TWO_MB (ONE_MB*2)

unsigned char data[TWO_MB];

int
main (int argc UNUSED, char *argv[])
{
  memset(data, 22, sizeof data);
  return 22;
}
