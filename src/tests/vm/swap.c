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

unsigned char data[ONE_MB/2];

/* Initialize buf1 with random data,
   then count the number of instances of each value within it. */
static void
init (void)
{
  msg ("init");

  memset(data, 42, sizeof data);
}

static void
verify (void)
{
  for (uint32_t i = 0; i < sizeof data; i++)
  {
    if (data[i] != 42)
    {
      fail ("bad value %d in byte %zu", data[i], i);
    }
  }
  msg ("success, checked until=%'zu", sizeof data);
}

void
test_main (void)
{
  init ();
  pid_t child;
  CHECK ((child = exec ("swap-child")) != -1,
         "exec \"swap-child\"");
  CHECK (wait (child) == 22, "wait for swap-child");
  verify ();
}
