#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include <vm/page.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/frame.h"
#include "syscall.h"

static thread_func start_process NO_RETURN;

static bool load(const char *file_name, void (**eip)(void), void **esp,
                 const char *arg_line);


#define NUM_ARGS_LIMIT 32
#define ARG_SIZE_LIMIT 32
#define PROGRAM_NAME_SIZE_LIMIT 64
struct process_arguments {
    char file_name[PROGRAM_NAME_SIZE_LIMIT];
    char args[ARG_SIZE_LIMIT][NUM_ARGS_LIMIT];
};

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute(const char *file_name) {
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  struct thread *t = thread_current();
  struct dir *cwd = t->working_directory ?
                    dir_reopen(t->working_directory) : dir_open_root();

  tid = thread_create_options(file_name, PRI_DEFAULT, start_process, fn_copy,cwd);
  if (tid == TID_ERROR)
    palloc_free_page(fn_copy);

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process(void *cmdline_ptr) {
  const char *cmdline = (const char *) cmdline_ptr;

  char *file_name = palloc_get_page(0);
  if (file_name == NULL)
    while (true);

  char *file_name_end_position = strchr(cmdline, ' ');
  if (file_name_end_position == NULL) {
    file_name_end_position = (char *) cmdline + strlen(cmdline);
  }
  unsigned file_name_size = file_name_end_position - cmdline;
  strlcpy(&file_name[0], cmdline, file_name_size + 1);
  file_name[file_name_size] = '\0';

  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load(&file_name[0], &if_.eip, &if_.esp, cmdline);

  palloc_free_page(cmdline_ptr);
  palloc_free_page(file_name);


  /*If load failed, note result in parent list*/
  if (!success)
  {
    struct thread *t = thread_current();

    struct thread *parent = thread_from_tid(t->parent);
    if (parent)
    {
      struct child_result *cr = thread_terminated_child_from_tid(t->tid, parent);
      ASSERT(cr != NULL);
      cr->exit_code = -1;
      cr->has_load_failed = true;
    }
  }

  // wake up a thread possibly waiting for our startup
  sema_up(&thread_current()->process_load_sema);

  /* If load failed, quit. */
  if (!success)
  {
    process_terminate(thread_current(), -1, thread_current()
    ->program_name);
  }


  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait(tid_t tid) {

  struct thread *t = thread_from_tid(tid);
  struct thread *current = thread_current();

  if (t != NULL && t->parent == current->tid)
  {
    // wait for process to exit
    sema_down(&t->wait_sema);
  }

  // thread should be gone by now
  // thread either doesn't exist or already terminated
  struct child_result *terminated_child =
          thread_terminated_child_from_tid(tid, current);
  if (terminated_child != NULL)
  {
    list_remove(&terminated_child->elem);
    int ec = terminated_child->exit_code;
    free(terminated_child);
    return ec;
  }
  return -1;
}

/* Free the current process's resources. */
void
process_exit(void) {
  struct thread *cur = thread_current();
  uint32_t *pd;

  //close all fd and free pages etc.
  while (!list_empty(&cur->file_descriptors)) {
    struct list_elem *e = list_pop_front(&cur->file_descriptors);
    struct file_descriptor *entry = list_entry(e, struct file_descriptor,
            list_elem);

    if (entry->is_directory)
      dir_close(entry->d);
    else
      file_close(entry->f);
    free(entry);
  }

  //free all child metadata entries
  while (!list_empty(&cur->terminated_children)) {
    struct list_elem *e = list_pop_front(&cur->terminated_children);
    struct child_result *entry = list_entry(e, struct child_result, elem);
    free(entry);
  }

  //free all mapped files, flush if necessary
  while (!list_empty(&cur->mapped_files)) {
    struct list_elem *e = list_pop_front(&cur->mapped_files);
    struct m_file *entry = list_entry(e, struct m_file, list_elem);
    close_mfile(cur, entry);
    free(entry);
  }

  // destroy supplemental page table of process
  // this also clears the page table, frame table, swap
  if (!cur->is_main_thread)
    spt_destroy(&cur->spt);

  // close CWD
  if (cur->working_directory)
    dir_close(cur->working_directory);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate(void) {
  struct thread *t = thread_current();

  /* Activate thread's page tables. */
  pagedir_activate(t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack(void **esp, const char *cmdline);

static bool validate_segment(const struct Elf32_Phdr *, struct file *);

static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load(const char *file_name, void (**eip)(void), void **esp, const char
*arg_line) {
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  //const char *file_name =

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create();
  if (t->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  bool is_dir;
  file = filesys_open(file_name, &is_dir);
  if (file == NULL || is_dir) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof(struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
               Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                          - read_bytes);
          } else {
            /* Entirely zero.
               Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void *) mem_page,
                            read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp, arg_line))
    goto done;

  /* Start address. */
  *eip = (void (*)(void)) ehdr.e_entry;

  success = true;

  done:
  /* We arrive here whether the load is successful or not. */
  if (success) {
    // if loaded successfully, save file pointer and protect
    // will close at process exit
    thread_current()->exec_file = file;
    file_deny_write(file);
  } else {
    file_close(file);
  }

  return success;
}

/* load() helpers. */

static bool install_page(void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
             uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs(upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  off_t file_offset = ofs;

  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    if (page_read_bytes == 0)
    {
      if (!spt_entry_empty((uint32_t)upage, thread_current()->tid, writable,
                           zeroes))
      {
        return false;
      }
    }
    else {
      /* Add the page to the process's address space. */

      if (!spt_entry_mapped_file((uint32_t)upage, thread_current()->tid, writable,
                                 file, file_offset, page_read_bytes,
                                 !writable)) {
        return false;
      }
      file_offset += (int)page_read_bytes;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

static unsigned countParams(const char *str) {
  bool space_before = false;
  unsigned ret = 0;

  //echo
  //echo
  //echo x
  for (unsigned i = 0; i < strlen(str); i++) {
    char c = str[i];
    if (c == ' ' && !space_before) {
      ret++;
    }

    if (i == strlen(str) - 1 && c != ' ') {
      ret++;
    }

    if (c == ' ') {
      space_before = true;
    } else
      space_before = false;
  }

  return ret;
}

/**
 * We know that this method is not 100% exact, but for 99% of the use-cases
 * it will work.
 * @param argc
 * @param size_of_cmd_line
 * @return
 */
static bool
overflow(unsigned int argc, unsigned int size_of_cmd_line, unsigned int align) {
  //return address (4), argc (4), argv ptr (4), null sentinel (4)
  //in bits
  unsigned int size = 16;
  size += align;
  // argv address table entries
  size += argc * 4;
  //not optimal for double spaces
  size += size_of_cmd_line + 1;

  return size > 4096;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp, const char *arg_line) {

  if (!spt_entry(thread_current(), ((uint32_t) PHYS_BASE) - PGSIZE, thread_current()
                         ->tid, 0, true,
                 zeroes)) {
    return false;
  }


  *esp = PHYS_BASE;
  //daten drauf
  char *current_stack_bottom = PHYS_BASE - 4;

  char *t;
  char *remainder = (char *) arg_line;

  //char *c;
  unsigned argc = countParams(arg_line);
  remainder = (char *) arg_line;

  if (overflow(argc, 0, 0)) return false;

  unsigned int arg_line_size = strlen(arg_line);
  //calc. the start of the address space (argv)
  void *address_space = current_stack_bottom - arg_line_size - 1;
  // word-align
  unsigned int align = ((unsigned) address_space) % 4;
  address_space -= align;
  // write terminating sentinel of argv
  char **argv = address_space;
  // make space for address table sentinel
  argv--;
  *argv = 0;
  argv--;
  // write addresses of the arguments here

  char **address_table = argv;
  address_table -= argc - 1;

  unsigned int current_data_size = 0;
  while ((t = strtok_r(remainder, " ", &remainder)) != NULL) {
    //strlen without \0
    unsigned arg_size = strlen(t);

    current_data_size += arg_size + 1;
    if (overflow(argc, current_data_size, align)) return false;

    char *string_address = current_stack_bottom - arg_size - 1;
    //strlcpy add \0 to length
    strlcpy(string_address, t, arg_size + 1);
    current_stack_bottom -= arg_size + 1;

    *address_table = string_address;
    address_table++;
    argv--;
  }
  // argv on the stack (make sure it points to argv[0]
  //point to argument table
  *argv = (char *) (argv + 1);
  argv -= 1;
  //argc on the stack
  *argv = (char *) argc;
  argv -= 1;
  //return address of the stack
  *argv = 0;
  *esp = argv;

  return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable) {
  struct thread *t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pagedir, upage) == NULL
          && pagedir_set_page(t->pagedir, upage, kpage, writable));
}


NO_RETURN void
process_terminate(struct thread *t, int status_code, const char *cmd_line)
{
  printf("%s: exit(%d)\n", cmd_line, status_code);

  if (t->exec_file != NULL)
    file_close(t->exec_file);

  if (t->parent != 0)
  {
    struct thread *parent = thread_from_tid(t->parent);
    if (parent)
    {
      struct child_result *cr = thread_terminated_child_from_tid(t->tid,
                                                                 parent);
      if (cr != NULL)
        cr->exit_code = status_code;
    }
  }

  sema_up(&t->wait_sema);

  thread_exit();
}