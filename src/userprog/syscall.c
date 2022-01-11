#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/vaddr.h>
#include <list.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <string.h>
#include <vm/page.h>
#include "vm/frame.h"
#include "pagedir.h"

#define MIN(a, b)             \
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

void handler_mmap(struct intr_frame *);

void handler_munmap(struct intr_frame *);

static void handler_exit(int *stack);

void unsync_close_mfile(struct thread *t, struct m_file *m_file);

void close_mfile(struct thread *t, struct m_file *m_file);

struct lock file_sema;

void
syscall_init(void) {
  lock_init(&file_sema);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f) {
  int *stack = f->esp;

  check_ptr(stack);
  struct thread *t = thread_current();

  if (get_user((const uint8_t *) stack) == -1) {
    process_terminate(t, -1, t->program_name);
  }

  t->user_esp = f->esp;

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
    case SYS_MMAP: {
      handler_mmap(f);
      break;
    }
    case SYS_MUNMAP: {
      handler_munmap(f);
      break;
    }
    default:
      printf("invalid system call!\n");
      process_terminate(thread_current(), -1, thread_current()->program_name);
  }
  t->user_esp = NULL;
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

static bool try_readu(const void *src, const size_t s, void *dest) {
  for (unsigned i = 0; i < s; i++) {
    const uint8_t *curent_ptr = ((const uint8_t *) src) + i;
    //check if kernel
    check_ptr((void *) curent_ptr);
    int r = get_user(curent_ptr);
    //check if page fault
    if (r == -1)
      return false;

    ((uint8_t *) dest)[i] = (uint8_t) r;
  }

  return true;
}

/**
 * read a piece of memory from userspace
 * @param src user src
 * @param s length
 * @param dest kernel src
 */
static void readu(const void *src, const size_t s, void *dest) {
  if (!try_readu(src, s, dest)) {
    process_terminate(thread_current(), -1, thread_current()
            ->program_name);
  }
}


static bool try_writeu(const void *src, const size_t s, void *dest) {
  for (unsigned i = 0; i < s; i++) {
    void *current_ptr = dest + i;
    check_ptr(current_ptr);

    int res = put_user(current_ptr, ((uint8_t *) src)[i]);
    if (!res)
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
static void writeu(const void *src, const size_t s, void *dest) {
  if (!try_writeu(src, s, dest)) {
    process_terminate(thread_current(), -1, thread_current()
            ->program_name);
  }
}

/**
 * compute length of string in user space
 * @param string user src
 */
static size_t strlenu(const char *string) {
  const char *p = string;

  ASSERT (string != NULL);

  int current_byte;
  do {
    current_byte = get_user((const uint8_t *) p);
    if (current_byte == -1) {
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }
  } while ((char) current_byte != '\0' && p++);

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

  if (cmd_line_ptr == NULL) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  size_t cmd_length = strlenu((const char *) cmd_line_ptr);
  if (cmd_length == 0) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  char cmd_line[cmd_length + 1];
  readu(cmd_line_ptr, sizeof cmd_line, cmd_line);

  // allocate child result early to avoid out-of-memory problems
  struct child_result *cr = malloc(sizeof(struct child_result));
  if (cr == NULL) {
    syscall_ret_value(-1, f);
    return;
  }

  lock_acquire(&file_sema);
  tid_t pid = process_execute(cmd_line);

  if (pid == -1) {
    lock_release(&file_sema);
    syscall_ret_value(-1, f);
    return;
  }

  cr->pid = pid;
  cr->exit_code = 999;
  cr->has_load_failed = false;
  list_push_back(&thread_current()->terminated_children, &cr->elem);

  struct thread *child_thread = thread_from_tid(pid);
  //sema sleep until loaded
  sema_down(&child_thread->process_load_sema);
  lock_release(&file_sema);
  //check if thread failed
  // the thread might be gone by now
  enum intr_level il = intr_get_level();
  intr_disable();
  child_thread = thread_from_tid(pid);
  if (child_thread == NULL) {
    // thread already terminated. Look for its data in the parents list
    struct child_result *terminated_child =
            thread_terminated_child_from_tid(pid, thread_current());

    ASSERT(terminated_child != NULL);
    syscall_ret_value(terminated_child->has_load_failed ? -1 : pid, f);

    if (terminated_child->has_load_failed) {
      list_remove(&terminated_child->elem);
      free(terminated_child);
    }
  } else {
    syscall_ret_value(pid, f);
  }
  intr_set_level(il);
}

void handler_fs_create(struct intr_frame *f) {
  int *stack = f->esp;

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if (file_name_size == 0) {
    f->eax = 0;
    return;
  }
  //args
  char file[file_name_size + 1];
  off_t initial_size;

  readu(file_name_pointer, sizeof file, file);
  readu((const void *) (stack + 2), sizeof(initial_size), &initial_size);

  //lock
  lock_acquire(&file_sema);
  f->eax = filesys_create(file, initial_size);
  lock_release(&file_sema);
}

void handler_fs_remove(struct intr_frame *f) {
  int *stack = f->esp;

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL) {
    f->eax = 0;
    return;
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if (file_name_size == 0) {
    f->eax = 0;
    return;
  }
  //args
  char file[file_name_size + 1];
  readu(file_name_pointer, sizeof file, file);

  //lock
  lock_acquire(&file_sema);
  f->eax = filesys_remove(file);
  lock_release(&file_sema);
}

static struct file_descriptor *
find_file_descriptor(int fd, struct thread *thread) {
  struct list_elem *e;
  ASSERT(thread == thread_current() || intr_get_level() == INTR_OFF);

  for (e = list_begin(&thread->file_descriptors);
       e != list_end(&thread->file_descriptors);
       e = list_next(e)) {
    struct file_descriptor *entry = list_entry(e,
                                               struct file_descriptor,
                                               list_elem);

    if (entry->descriptor_id == fd) {
      return entry;
    }
  }
  return NULL;
}

static struct m_file *
find_mfile(int map_id, struct thread *thread) {
  struct list_elem *e;
  ASSERT(thread == thread_current() || intr_get_level() == INTR_OFF);

  for (e = list_begin(&thread->mapped_files);
       e != list_end(&thread->mapped_files);
       e = list_next(e)) {
    struct m_file *entry = list_entry(e,
                                      struct m_file, list_elem);

    if (entry->id == map_id) {
      return entry;
    }
  }
  return NULL;
}

void handler_fs_open(struct intr_frame *f) {
  int *stack = f->esp;

  const char *file_name_pointer;
  readu(stack + 1, sizeof file_name_pointer, &file_name_pointer);

  if (file_name_pointer == NULL) {
    process_terminate(thread_current(), -1, thread_current()->program_name);
    return;
  }

  size_t file_name_size = strlenu((const char *) file_name_pointer);
  if (file_name_size == 0) {
    f->eax = -1;
    return;
  }
  //args
  char file[file_name_size + 1];
  off_t initial_size;

  readu(file_name_pointer, sizeof file, file);
  readu((const void *) (stack + 2), sizeof(initial_size), &initial_size);

  //lock
  lock_acquire(&file_sema);
  struct file *file_pointer = filesys_open(file);
  lock_release(&file_sema);

  //fail open failed
  if (file_pointer == 0) {
    f->eax = -1;
    return;
  }

  struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));
  if (fd == NULL) {
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

  //args
  int fd_id = (int) (*(stack + 1));

  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);

  struct thread *t = thread_current();

  //lock
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0) {
    f->eax = -1;
    lock_release(&file_sema);
    return;
  }

  f->eax = file_length(fd->f);
  lock_release(&file_sema);
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
    for (unsigned j = 0; j < actual_size; j++) {
      unsigned char in = input_getc();
      buffer[j] = in;
    }

    f->eax = actual_size;
    //return size...
    return;
  }

  struct thread *cur = thread_current();

  //lock
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, cur);
  if (fd == NULL) {
    lock_release(&file_sema);
    f->eax = -1;
    return;
  }

  size_t read_so_far = 0;
  for (; read_so_far < size;) {
    off_t chunk_size = MIN((size_t)PGSIZE, size - read_so_far);
    chunk_size = file_read(fd->f, cur->syscall_temp_buffer, (off_t) chunk_size);

    if (chunk_size == 0) {
      break;
    }

    lock_release(&file_sema);
    if (!try_writeu(cur->syscall_temp_buffer, chunk_size, buffer + read_so_far)) {
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }
    lock_acquire(&file_sema);

    read_so_far += chunk_size;
  }

  f->eax = read_so_far;

  //unlock
  lock_release(&file_sema);
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
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, cur);
  if (fd == NULL) {
    lock_release(&file_sema);
    f->eax = -1;
    return;
  }

  size_t written_so_far = 0;
  for (; written_so_far < size;) {
    size_t chunk_size = MIN((size_t)PGSIZE, size - written_so_far);

    lock_release(&file_sema);
    if (!try_readu(buffer + written_so_far, chunk_size, cur->syscall_temp_buffer)) {
      process_terminate(thread_current(), -1, thread_current()
              ->program_name);
    }
    lock_acquire(&file_sema);

    chunk_size = file_write(fd->f, cur->syscall_temp_buffer, (off_t) chunk_size);

    if (chunk_size == 0) {
      break;
    }

    written_so_far += chunk_size;
  }

  f->eax = written_so_far;

  //unlock
  lock_release(&file_sema);
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
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, thread_current());
  if (fd == NULL) {
    lock_release(&file_sema);
    return;
  }

  struct file *file = fd->f;
  file->pos = (int) position;

  //unlock
  lock_release(&file_sema);
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

  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, thread_current());
  lock_release(&file_sema);

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
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0) {
    lock_release(&file_sema);
    return;
  }


  file_close(fd->f);
  //remove from list
  list_remove(&(fd->list_elem));
  lock_release(&file_sema);

  free(fd);
}

void handler_mmap(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int fd_id;
  readu((const void *) (stack + 1), sizeof(fd_id), &fd_id);

  void *addr;
  readu((const void *) (stack + 2), sizeof(addr), &addr);

  //Address == 0
  if (addr == 0 || addr >= PHYS_BASE) {
    f->eax = -1;
    return;
  }

  //wrong fd_id
  if (fd_id == 0 || fd_id == 1) {
    f->eax = -1;
    return;
  }

  //not paged alinged
  if (((uint32_t) addr) % PGSIZE != 0) {
    f->eax = -1;
    return;
  }

  struct thread *t = thread_current();

  //lock
  lock_acquire(&file_sema);
  struct file_descriptor *fd = find_file_descriptor(fd_id, t);

  if (fd == 0) {
    f->eax = -1;
    lock_release(&file_sema);
    return;
  }

  off_t filesize = file_length(fd->f);

  //check is not zero
  if (filesize == 0) {
    f->eax = -1;
    lock_release(&file_sema);
    return;
  }

  //check kernel access
  if ((uint32_t) addr + (uint32_t) filesize >= (uint32_t)PHYS_BASE)
  {
    f->eax = -1;
    lock_release(&file_sema);
    return;
  }

  //check overlaping
  if (spt_file_overlaping((uint32_t) addr, filesize, t->tid)) {
    f->eax = -1;
    lock_release(&file_sema);
    return;
  }

  struct file *reopend_file = file_reopen(fd->f);
  lock_release(&file_sema);

  if (reopend_file == NULL) {
    f->eax = -1;
    return;
  }

  struct m_file *m_file = malloc(sizeof(struct m_file));
  if (m_file == NULL) {
    f->eax = -1;
    return;
  }

  int last_id = 0;

  struct list *mapped_list = &thread_current()->mapped_files;
  if (!list_empty(mapped_list)) {
    int id = list_entry(list_back(mapped_list),
                        struct m_file,
                        list_elem)->id;
    last_id = id + 1;
  }

  m_file->id = last_id;
  m_file->file = reopend_file;
  m_file->vaddr = (uint32_t) addr;

  for (int i = 0; i < file_length(reopend_file); i += PGSIZE) {
    spt_entry_mapped_file((uint32_t) addr + i, t->tid, true, reopend_file,
                          i, MIN(file_length(reopend_file) - i, PGSIZE), true);
  }

  list_push_back(&t->mapped_files, &m_file->list_elem);

  f->eax = m_file->id;
}

/*
 * impl. remove_page
 * impl. remove_page from suplt table.
 */
void handler_munmap(struct intr_frame *f) {
  int *stack = f->esp;

  //args
  int map_id;
  readu((const void *) (stack + 1), sizeof(map_id), &map_id);

  struct thread *t = thread_current();

  //lock
  lock_acquire(&file_sema);
  struct m_file *m_file = find_mfile(map_id, t);

  if (m_file == 0) {
    lock_release(&file_sema);
    return;
  }

  lock_release(&file_sema);

  //remove from supplemental table, page table. Flush if necessary
  close_mfile(t, m_file);

  list_remove(&(m_file->list_elem));

  free(m_file);
}

void close_mfile(struct thread *t, struct m_file *m_file) {
  //flush
  uint32_t mapped_file_vaddr = m_file->vaddr;
  tid_t pid = t->tid;

  fs_lock();
  off_t fileLength = file_length(m_file->file);
  fs_unlock();
  frametable_lock();
  for (int i = 0; i < fileLength; i += PGSIZE) {
    struct spt_entry *entry_ptr = spt_get_entry(t,
            (uint32_t)mapped_file_vaddr + i, pid);
    struct spt_entry entry_copy = *entry_ptr;

    ASSERT(entry_ptr != NULL);

    if (entry_copy.spe_status == frame_from_file) {
      // page is actually mapped, has been accessed at least once.
      void *page_vaddr = (void *) mapped_file_vaddr + i;
      void *kaddr = pagedir_get_page(t->pagedir, page_vaddr);
      if (pagedir_is_dirty(t->pagedir, page_vaddr)) {
        pagedir_set_dirty(t->pagedir, page_vaddr, false);
        set_pinned(kaddr);
        frametable_unlock();
        // page has been written to
        fs_lock();
        file_seek(m_file->file, (int) entry_copy.file_offset);
        file_write(m_file->file, (void *) kaddr, (int) entry_copy.read_bytes);
        fs_unlock();

        frametable_lock();
      }
    }
    else if (entry_copy.spe_status == mapped_file){
      // all good, no need to write back
    }
    else {
      ASSERT(0);
    }

    spt_remove_entry(mapped_file_vaddr + i, t);
  }
  frametable_unlock();

  fs_lock();
  file_close(m_file->file);
  fs_unlock();
}

void fs_lock()
{
  lock_acquire(&file_sema);
}

void fs_unlock()
{
  lock_release(&file_sema);
}

bool fs_lock_held_by_current_thread()
{
  return lock_held_by_current_thread(&file_sema);
}
