#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/vaddr.h>
#include <list.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <string.h>

#define MIN(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b;       \
})

static void syscall_handler(struct intr_frame *);

static void readu(const void *src, size_t s, void *dest);

static int get_user(const uint8_t *uaddr);

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

void handler_wait(struct intr_frame *);

void handler_fs_close(struct intr_frame *);

static void handler_exit(int *stack);

struct semaphore file_sema;

void
syscall_init(void) {
  sema_init(&file_sema, 1);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f) {
  int *stack = f->esp;

  check_ptr(stack);

  if (get_user((const uint8_t *) stack) == -1) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  switch (*stack) {
    case SYS_EXIT: {
      handler_exit(stack);
      break;
    }
    case SYS_WAIT: {
      handler_wait(f);
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
    case SYS_WRITE: {
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
      printf("invalid system call!\n");
      process_terminate(thread_current(), -1, thread_current()->program_name);
  }
}

static void handler_exit(int *stack) {/*
 * Only print is userthred/programm
 * wait
 */
  struct thread *t = thread_current();
  const char *cmdline = &t->program_name[0];

  int status_code;
  readu((const void *) (stack + 1), sizeof(status_code), &status_code);

  process_terminate(t, status_code, cmdline);
}

void handler_wait(struct intr_frame *f) {
  int *stack = f->esp;

  int pid;
  readu((const void *) (stack + 1), sizeof(pid), &pid);

  f->eax = process_wait(pid);
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

static bool try_readu(const void *src, const size_t s, void *dest)
{
  for (unsigned i = 0; i < s; i++)
  {
    const uint8_t *curent_ptr = ((const uint8_t *) src) + i;
    //check if kernel
    check_ptr((void *)curent_ptr);
    int r = get_user(curent_ptr);
    //check if page fault
    if (r == -1)
      return false;

    ((uint8_t *)dest)[i] = (uint8_t)r;
  }

  return true;
}

/**
 * read a piece of memory from userspace
 * @param src user src
 * @param s length
 * @param dest kernel src
 */
static void readu(const void *src, const size_t s, void *dest)
{
  if (!try_readu(src, s, dest))
  {
    process_terminate(thread_current(), -1, thread_current()
                  ->program_name);
  }
}


static bool try_writeu(const void *src, const size_t s, void *dest)
{
  for (unsigned i = 0; i < s; i++)
  {
    void *current_ptr = dest + i;
    check_ptr(current_ptr);

    int res = put_user(current_ptr, ((uint8_t *)src)[i]);
    if (res == -1)
      return false;
  }
  return true;
}


/**
 * write a piece of memory to user space safely
 * @param src 
 * @param s 
 * @param dest 
 */
static void writeu(const void *src, const size_t s, void *dest)
{
  if (!try_writeu(src, s, dest))
  {
    process_terminate(thread_current(), -1, thread_current()
              ->program_name);
  }
}

/**
 * compute length of string in user space
 * @param string user src
 */
static size_t strlenu(const char *string)
{
  const char *p = string;

  ASSERT (string != NULL);

  int current_byte;
  do {
    current_byte = get_user((const uint8_t *) p);
    if (current_byte == -1) {
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }
  }
  while ((char)current_byte != '\0' && p++);

  return p - string;
}

void handler_halt(void) {
  shutdown_power_off();
}

void check_ptr(void *p) {
  if (p >= PHYS_BASE) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }
}

static void syscall_ret_value(int ret, struct intr_frame *f) {
  f->eax = ret;
}

void handler_exec(struct intr_frame *f) {
  int *stack = f->esp;

  const char *cmd_line_ptr;
  readu(stack + 1, sizeof cmd_line_ptr, &cmd_line_ptr);

  if (cmd_line_ptr == NULL)
  {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  size_t cmd_length = strlenu((const char *)cmd_line_ptr);
  if (cmd_length == 0)
  {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  char cmd_line[cmd_length+1];
  readu(cmd_line_ptr, sizeof cmd_line, cmd_line);

  sema_down(&file_sema);
  tid_t pid = process_execute(cmd_line);

  if (pid == -1) {
    sema_up(&file_sema);
    syscall_ret_value(-1, f);
    return;
  }

  struct thread *t = thread_from_tid(pid);
  //sema sleep until loaded
  sema_down(&t->process_load_sema);
  sema_up(&file_sema);
  //check if thread failed
  // the thread might be gone by now
  enum intr_level il = intr_get_level();
  intr_disable();
  t = thread_from_tid(pid);
  if (t == NULL)
  {
    // thread already terminated. Look for its data in the parents list
    struct child_result *terminated_child =
            thread_terminated_child_from_tid(pid, thread_current());

    ASSERT(terminated_child != NULL);
    syscall_ret_value(terminated_child->has_load_failed ? -1 : pid, f);

    if (terminated_child->has_load_failed)
    {
      list_remove(&terminated_child->elem);
      palloc_free_page(terminated_child);
    }
  }
  else if (t->has_load_failed)
  {
    syscall_ret_value(-1, f);
  }
  else {
    syscall_ret_value(pid, f);
  }
  intr_set_level(il);
}

void handler_fs_create(struct intr_frame *f) {
  int *stack = f->esp;

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL)
  {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if(file_name_size == 0)
  {
    f->eax = 0;
    return;
  }
  //args
  char file[file_name_size+1];
  off_t initial_size;

  readu(file_name_pointer, sizeof file, file);
  readu((const void *) (stack + 2), sizeof(initial_size), &initial_size);

  //lock
  sema_down(&file_sema);
  f->eax = filesys_create(file, initial_size);
  sema_up(&file_sema);
}

void handler_fs_remove(struct intr_frame *f) {
  int *stack = f->esp;

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL)
  {
    f->eax = 0;
    return;
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if(file_name_size == 0)
  {
    f->eax = 0;
    return;
  }
  //args
  char file[file_name_size+1];
  readu(file_name_pointer, sizeof file, file);

  //lock
  sema_down(&file_sema);
  f->eax = filesys_remove(file);
  sema_up(&file_sema);
}

static struct file_descriptor *
find_file_descriptor(int fd, struct thread *thread) {
  struct list_elem *e;
  ASSERT( thread == thread_current() || intr_get_level() == INTR_OFF);

  for (e = list_begin(&thread->file_descriptors);
    e != list_end(&thread->file_descriptors);
    e = list_next(e))
  {
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

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL)
  {
    process_terminate(thread_current(), -1, thread_current()->program_name);
    return;
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if (file_name_size == 0)
  {
    f->eax = -1;
    return;
  }
  //args
  char file[file_name_size+1];
  off_t initial_size;

  readu(file_name_pointer, sizeof file, file);
  readu((const void *) (stack + 2), sizeof(initial_size), &initial_size);

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

static uint8_t kbuffer[8192];

void handler_fs_filesize(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id = (int) (*(stack + 1));

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);

  struct thread *t = thread_current();

  //lock
  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0)
  {
    f->eax = -1;
    sema_up(&file_sema);
    return;
  }

  f->eax = file_length(fd->f);
  sema_up(&file_sema);
}

void handler_fs_read(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id;
  unsigned char *buffer;
  unsigned int size;

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);
  readu((const void *) (stack + 2), sizeof(buffer), &buffer);
  readu((const void *) (stack + 3), sizeof(size), &size);

  if (fd_id == 0) { // stdin
    unsigned actual_size = MIN(size, 32u);
    for (unsigned j = 0; j < actual_size; j++)
    {
      unsigned char in = input_getc();
      buffer[j] = in;
    }

    f->eax = actual_size;
    //return size...
    return;
  }

  struct thread *cur = thread_current();

  //lock
  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, cur);
  if (fd == NULL)
  {
    sema_up(&file_sema);
    f->eax = -1;
    return;
  }

  size_t read_so_far = 0;
  for (;read_so_far < size;)
  {
    off_t chunk_size = MIN(sizeof kbuffer, size - read_so_far);
    chunk_size = file_read(fd->f, kbuffer, (off_t)chunk_size);

    if (chunk_size == 0)
    {
      break;
    }

    if (!try_writeu(kbuffer + read_so_far, chunk_size, buffer))
    {
      sema_up(&file_sema);
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }

    read_so_far += chunk_size;
  }

  f->eax = read_so_far;

  //unlock
  sema_up(&file_sema);
}

void handler_fs_write(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id;
  const char *buffer;
  unsigned int size;

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);
  readu((const void *) (stack + 2), sizeof(buffer), &buffer);
  readu((const void *) (stack + 3), sizeof(size), &size);

  if (fd_id == 1) {
    unsigned actual_size = MIN(size, 4096u);
    putbuf(buffer, actual_size);
    f->eax = actual_size;
    //return size...
    return;
  }

  struct thread *cur = thread_current();

  //lock
  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, cur);
  if (fd == NULL)
  {
    sema_up(&file_sema);
    f->eax = -1;
    return;
  }

  size_t written_so_far = 0;
  for (;written_so_far < size;)
  {
    size_t chunk_size = MIN(sizeof kbuffer, size - written_so_far);

    if (!try_readu(buffer + written_so_far, chunk_size, kbuffer))
    {
      sema_up(&file_sema);
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }

    chunk_size = file_write(fd->f, kbuffer, (off_t)chunk_size);

    if (chunk_size == 0)
    {
      break;
    }

    written_so_far += chunk_size;
  }

  f->eax = written_so_far;

  //unlock
  sema_up(&file_sema);
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
  int *stack = f->esp;

  //args
  int fd_id;
  unsigned int position;

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);
  readu((const void *) (stack + 2), sizeof(position), &position);

  //lock
  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, thread_current());
  if (fd == NULL)
  {
    sema_up(&file_sema);
    return;
  }

  struct file *file = fd->f;
  file->pos = (int)position;

  //unlock
  sema_up(&file_sema);
}

/*
 * Returns the position of the next byte to be read or written in open file fd,
 * expressed in bytes from the beginning of the file.
 */
void handler_fs_tell(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id;

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);

  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, thread_current());
  sema_up(&file_sema);

  if (fd == NULL) {
    f->eax = 0;
    return;
  }

  f->eax = fd->f->pos;
}

void handler_fs_close(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id;
  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);

  struct thread *t = thread_current();

  //lock
  sema_down(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0)
  {
    sema_up(&file_sema);
    return;
  }


  file_close(fd->f);
  //remove from list
  list_remove(&(fd->list_elem));
  sema_up(&file_sema);

  palloc_free_page(fd);
}
