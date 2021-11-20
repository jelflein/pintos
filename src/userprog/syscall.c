#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/vaddr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *stack = f->esp;
  switch (*stack) {

    case SYS_WRITE: {
      char *buffer = *(char **) (stack + 2);
      unsigned size = *(stack + 3);
      int fd = *(stack + 1);
      if (fd != 1) {
        printf("system call!\n");
        thread_exit();
      }
      if (buffer >= (char *) PHYS_BASE) {
        thread_exit();
      }
      //writing to STDOUT
      int a = (int) size;
      while (a >= 100) {
        putbuf(buffer, 100);
        buffer = buffer + 100;
        a -= 100;
      }
      putbuf(buffer, a);
      f->eax = (int) size;
      break;
    }
    default:
      printf ("system call!\n");
      thread_exit();
      break;
  }
}
