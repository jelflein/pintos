#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/vaddr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler(struct intr_frame *);

void handler_write(struct intr_frame *);

void handler_halt(void);

void handler_exec(struct intr_frame *);

void check_ptr(void *p);

void handler_fs_create(struct intr_frame *);

void handler_fs_remove(struct intr_frame *);

void handler_fs_open(struct intr_frame *);

void handler_fs_filesize(struct intr_frame *);

void handler_fs_read(struct intr_frame *);

void handler_fs_write(struct intr_frame *);

void handler_fs_seek(struct intr_frame *);

void handler_fs_tell(struct intr_frame *);

void handler_fs_close(struct intr_frame *);


static void handler_exit(const int *stack);

struct semaphore file_sema;

void
syscall_init(void) {
  sema_init(&file_sema, 1);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f UNUSED) {
  int *stack = f->esp;
  switch (*stack) {

    case SYS_WRITE: {
      handler_write(f);
      break;
    }
    case SYS_EXIT: {
      handler_exit(stack);
      break;
    }
    case SYS_HALT: {
      handler_halt();
      break;
    }
    case SYS_EXEC: {
      handler_exec(f);
      break;
    }
    case SYS_CREATE: {
      handler_fs_create(f);
      break;
    }
    case SYS_REMOVE: {
      handler_fs_remove(f);
      break;
    }
    case SYS_OPEN: {
      handler_fs_open(f);
      break;
    }
    case SYS_FILESIZE: {
      handler_fs_filesize(f);
      break;
    }
    case SYS_READ: {
      handler_fs_read(f);
      break;
    }
    case 1337: {//SYS_WRITE: {
      handler_fs_write(f);
      break;
    }
    case SYS_SEEK: {
      handler_fs_seek(f);
      break;
    }
    case SYS_TELL: {
      handler_fs_tell(f);
      break;
    }
    case SYS_CLOSE: {
      handler_fs_close(f);
      break;
    }
    default:
      printf("system call!\n");
      thread_exit();
  }
}

static void handler_exit(const int *stack) {/*
 * Only print is userthred/programm
 * wait
 */
  struct thread *t = thread_current();
  const char *cmdline = &t->program_name[0];

  check_ptr(stack + 4);
  int status_code = *(stack + 4);
  printf("%s: exit(%d)\n", cmdline, status_code);

  thread_exit();
}

/*
 * The following two functions are taken from the course website 3.1.5
 * */
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user(const uint8_t *uaddr) {
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
  : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user(uint8_t *udst, uint8_t byte) {
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
  : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


void handler_write(struct intr_frame *f) {
  int *stack = f->esp;
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
  while (a >= 64) {
    putbuf(buffer, 64);
    buffer = buffer + 64;
    a -= 64;
  }
  putbuf(buffer, a);
  f->eax = (int) size;
}


void handler_halt(void) {
  shutdown_power_off();
}

void check_ptr(void *p) {
  if (p >= PHYS_BASE) {
    printf("Invalid access\n");
    thread_exit();
  }
}

static void syscall_ret_value(int ret, struct intr_frame *f) {
  f->eax = ret;
}

void handler_exec(struct intr_frame *f) {
  int *stack = f->esp;
  char *cmd_line = (char *) (*(stack + 4));

  check_ptr(cmd_line);

  tid_t pid = process_execute(cmd_line);

  if (pid == -1) {
    syscall_ret_value(-1, f);
    return;
  }

  struct thread *t = thread_from_tid(pid);
  //sema sleep until loaded
  sema_down(&t->process_load_sema);
  //check if thread failed
  if (t->has_load_failed) {
    syscall_ret_value(-1, f);
    return;
  }

  syscall_ret_value(pid, f);
}

void handler_fs_create(struct intr_frame *f) {
  int *stack = f->esp;

  //check args
  check_ptr(stack + 4);
  check_ptr(stack + 8);

  //args
  const char *file = (const char *) (*(stack + 4));
  off_t initial_size = (off_t) (*(stack + 8));

  //lock
  sema_down(&file_sema);
  f->eax = filesys_create(file, initial_size);
  sema_up(&file_sema);
}

void handler_fs_remove(struct intr_frame *f) {
  int *stack = f->esp;

  //check args
  check_ptr(stack + 4);

  //args
  const char *file = (const char *) (*(stack + 4));

  //lock
  sema_down(&file_sema);
  f->eax = filesys_remove(file);
  sema_up(&file_sema);
}

struct file_descriptor *
find_file_descriptor(int fd, struct thread *thread) {
  struct list_elem *e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&thread->file_descriptors);
  e != list_end(&thread->file_descriptors);
       e = list_next(e)) {
    struct file_descriptor *entry = list_entry(e,
                                  struct file_descriptor, list_elem);

    if (entry->descriptor_id == fd)
    {
      return entry;
    }
  }
  return NULL;
}

void handler_fs_open(struct intr_frame *f) {
  int *stack = f->esp;

  //check args
  check_ptr(stack + 4);

  //args
  const char *file = (const char *) (*(stack + 4));

  //lock
  sema_down(&file_sema);
  struct file *file_pointer = filesys_open(file);
  sema_up(&file_sema);

  //fail open failed
  if (file_pointer == 0)
  {
    f->eax = -1;
    return;
  }

  struct file_descriptor *fd = palloc_get_page(0);
  if (fd == NULL)
  {
    f->eax = -1;
    return;
  }

  int last_id = 3;

  struct list *fd_list = &thread_current()->file_descriptors;
  if (!list_empty(fd_list)) {
    int id = list_entry(list_back(fd_list),
                        struct file_descriptor,
                          list_elem)->descriptor_id;
    last_id = id + 1;
  }

  fd->descriptor_id = last_id;
  fd->f = file_pointer;

  struct thread *t = thread_current();
  list_push_back(&t->file_descriptors, &fd->list_elem);

  f->eax = fd->descriptor_id;
}

void handler_fs_filesize(struct intr_frame *f) {
  int *stack = f->esp;

  //check args
  check_ptr(stack + 4);

  //args
  int fd_id = (int) (*(stack + 4));

  struct thread *t = thread_current();
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);
  if (fd == 0)
  {
    //TODO rausfinden 0 oder -1?
    f->eax = 0;
    return;
  }

  //lock
  sema_down(&file_sema);
  f->eax = file_length(fd->f);
  sema_up(&file_sema);
}

void handler_fs_read(struct intr_frame *f) {

}

//Denying writes to exec.
//Det. if exec
void handler_fs_write(struct intr_frame *f) {


  bool is_exec = false;

  if (is_exec)
  {
    f->eax = 0;
    return;
  }
}

/*
Changes the next byte to be read or written in open file fd to position, expressed in bytes from the beginning of the file.
(Thus, a position of 0 is the file's start.)
A seek past the current end of a file is not an error. A later read obtains 0 bytes, indicating end of file.
A later write extends the file, filling any unwritten gap with zeros. (However, in Pintos files have a fixed length
until project 4 is complete, so writes past end of file will return an error.)
These semantics are implemented in the file system and do not require any special effort in system call implementation.
 */
void handler_fs_seek(struct intr_frame *f) {

}

/*
 * Returns the position of the next byte to be read or written in open file fd,
 * expressed in bytes from the beginning of the file.
 */
void handler_fs_tell(struct intr_frame *f) {

}

//TODO close all files in process
void handler_fs_close(struct intr_frame *f) {
  int *stack = f->esp;

  //check args
  check_ptr(stack + 4);

  //args
  int fd_id = (int) (*(stack + 4));

  struct thread *t = thread_current();
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0) return;

  //lock
  sema_down(&file_sema);
  file_close(fd->f);
  //remove from list
  list_remove(&(fd->list_elem));
  sema_up(&file_sema);

  palloc_free_page(fd);
}
