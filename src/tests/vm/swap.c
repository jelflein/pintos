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

//unsigned char data[ONE_MB/2 - 27*4096];
//unsigned char data[ONE_MB];
unsigned char data[ONE_MB*2];
unsigned char data2[ONE_MB*2];

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
    if (data[i] != 42)
    {
      printf("data reading from address %p\n", &data[i]);
      fail ("bad value %d in byte %zu", data[i], i);
    }
    if (data2[i] != 40)
    {
      printf("data2 reading from address %p\n", &data[i]);
      fail ("bad value %d in byte %zu", data[i], i);
    }
  }
  msg ("success, checked until=%'zu", sizeof data);
}

void
test_main (void)
{
  init ();
  pid_t child1;
  pid_t child2;
  pid_t child3;
  pid_t child4;
  pid_t child5;
  CHECK ((child1 = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK ((child2 = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK ((child3 = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK ((child4 = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK (wait (child2) == 22, "wait for swap-child 2");
  copy();
  CHECK ((child5 = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK (wait (child1) == 22, "wait for swap-child 1");
  CHECK (wait (child3) == 22, "wait for swap-child 3");
  CHECK (wait (child5) == 22, "wait for swap-child 5");
  CHECK (wait (child4) == 22, "wait for swap-child 4");

  verify ();
}
