/* Generates about 1 MB of random data that is then divided into
   16 chunks.  A separate subprocess sorts each chunk in
   sequence.  Then we merge the chunks and verify that the result
   is what it should be. */

#include <syscall.h>
#include <string.h>
#include "tests/arc4.h"
#include "tests/lib.h"
#include "tests/main.h"
#include "../lib.h"
#include "../../lib/stdio.h"

#define ONE_MB (1024*1024)
#define TWO_MB (ONE_MB*2)


#define CHUNK_SIZE (126 * 512)
#define CHUNK_CNT 16                            /* Number of chunks. */
#define DATA_SIZE (CHUNK_CNT * CHUNK_SIZE)      /* Buffer size. */
//unsigned char data[ONE_MB/2 - 27*4096];
//unsigned char data[ONE_MB];
unsigned char data[DATA_SIZE];
unsigned char data2[DATA_SIZE];

/* Initialize buf1 with random data,
   then count the number of instances of each value within it. */
static void
init (void)
{
  msg ("init");

  memset(data, 42, sizeof data);
}

static void copy(void)
{
  for (uint32_t i = 0; i < sizeof data; i++)
  {
    data2[i] = data[i] - 2;
  }
}

static void
verify (void)
{
  for (uint32_t i = 0; i < sizeof data; i++)
  {
    if (data[i] != (i / CHUNK_SIZE) + 1)
    {
      printf("data reading from address %p\n", &data[i]);
      fail ("bad value %d in byte %zu", data[i], i);
    }
    if (data2[i] != (i / CHUNK_SIZE) + 1 - 2)
    {
      printf("data2 reading from address %p\n", &data[i]);
      fail ("bad value %d in byte %zu", data[i], i);
    }
  }
  msg ("success, checked until=%'zu", sizeof data);
}

void launch_children()
{
  size_t i;

  create ("buffer", CHUNK_SIZE);
  for (i = 0; i < CHUNK_CNT; i++)
  {
    pid_t child;
    int handle;

    msg ("sort chunk %zu", i);

    /* Write this chunk to a file. */
    CHECK ((handle = open ("buffer")) > 1, "open \"buffer\"");
    write (handle, data + CHUNK_SIZE * i, CHUNK_SIZE);
    close (handle);

    /* Sort with subprocess. */
    char invocation[30];
    snprintf(invocation, sizeof invocation, "swap-child-f %lu", i+1);
    CHECK ((child = exec (invocation)) != -1, "asdfasdfasdfadsf");
    CHECK (wait (child) == 123, "wait for swap-child-f");

    /* Read chunk back from file. */
    CHECK ((handle = open ("buffer")) > 1, "open \"buffer\"");
    read (handle, data + CHUNK_SIZE * i, CHUNK_SIZE);
    close (handle);
  }
}

void
test_main (void)
{
  init ();

  launch_children();

  copy();

  verify ();
}
