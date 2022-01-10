/* Reads a 128 kB file into static data and "sorts" the bytes in
   it, using counting sort, a single-pass algorithm.  The sorted
   data is written back to the same file in-place. */

#include <debug.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "../../lib/string.h"
#include "../../lib/stdint.h"
#include "../lib.h"

const char *test_name = "swap-child";

#define CHUNK_SIZE (126 * 512)
#define CHUNK_CNT 16                            /* Number of chunks. */
#define DATA_SIZE (CHUNK_CNT * CHUNK_SIZE)      /* Buffer size. */

unsigned char data[CHUNK_SIZE];

int
main (int argc UNUSED, char *argv[])
{
  //Test buffer with pseudo value
  memset(data, 22, sizeof data);
  for (uint32_t i = 0; i < sizeof data; i++)
  {
    if (data[i] != 22)
    {
      printf("reading from address %p\n", &data[i]);
      fail ("child bad value %d in byte %zu", data[i], i);
    }
  }

  int handle;
  size_t size;
  size_t i;

  int child_id = atoi(argv[1]);
  //open file
  CHECK ((handle = open ("buffer")) > 1, "open buffer");

  //read file
  seek(handle, (child_id - 1) * CHUNK_SIZE);
  size = read (handle, data, CHUNK_SIZE);
  CHECK(size == CHUNK_SIZE, "read less than chunk size");
  for (i = 0; i < size; i++)
  {
    if(data[i] != 42)
    {
      printf("42 != reading from address %p\n", &data[i]);
      fail ("42 != child bad value %d in byte %zu", data[i], i);
    }

    data[i] = child_id;
  }

  seek(handle, (child_id - 1) * CHUNK_SIZE);
  write (handle, data, size);
  close (handle);

  return 123;
}