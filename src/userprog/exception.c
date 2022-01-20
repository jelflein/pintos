#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <threads/vaddr.h>
#include <vm/page.h>
#include <vm/frame.h>
#include <threads/palloc.h>
#include <filesys/file.h>
#include <vm/swap.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/kernel/debug.h"
#include "process.h"
#include "pagedir.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

#define ABS(x) (((x) < 0) ? -(x) : (x))

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f)
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  struct thread *t = thread_current();
  bool is_syscall = t->user_esp != NULL && !user;

  if ((user || is_syscall) && !not_present)
  {
    // user or kernel (might be syscall) tried to write to RO page
    process_terminate(t, -1, t->program_name);
  }

  uint32_t page_vaddr = (uint32_t)fault_addr / PGSIZE * PGSIZE;
  d_printf("vaddr %p\n", (void *) page_vaddr);

  frametable_lock();
  struct spt_entry *spt_entry = spt_get_entry(thread_current(), page_vaddr, t
          ->tid);

  void* stack_pointer = f->esp;
  if (is_syscall) stack_pointer = t->user_esp;

  if (spt_entry == NULL
    && (
      // PUSH, CALL or PUSHA instruction may fault 4 or 32 bytes above the stack
      ((stack_pointer > fault_addr) && ((uint32_t)stack_pointer - (uint32_t)fault_addr <= 32))
      // faults inside the stack
      || fault_addr > stack_pointer
      )
    && fault_addr < PHYS_BASE)
  {
    uint32_t stack_start = ((uint32_t) PHYS_BASE) - PGSIZE;
    uint32_t stack_page_number = (stack_start - page_vaddr) /
            (uint32_t) PGSIZE;
    const uint32_t STACK_PAGE_LIMIT = 192;
    if (stack_page_number > STACK_PAGE_LIMIT)
    {
      printf("Stack overflow, limit of %u pages enforced\n", STACK_PAGE_LIMIT);
      process_terminate(t, -1, t->program_name);
    }
    // grow stack logic
    //create spt entry
    bool success = spt_entry_empty(page_vaddr, t->tid ,true, zeroes);
    if (!success) process_terminate(t, -1, t->program_name);

    spt_entry = spt_get_entry(thread_current(), page_vaddr, t->tid);
  }

  if (spt_entry != NULL) {
    // SPT entry but no mapping in page table exists yet
    frametable_unlock();
    void *frame_pointer = allocate_frame(t, PAL_ZERO, page_vaddr);
    frametable_lock();

    ASSERT(frame_pointer != NULL);
    ASSERT(page_vaddr != 0);
    //Install page
    bool success = pagedir_set_page(t->pagedir, (void *) page_vaddr,
                                    frame_pointer,
                                    spt_entry->writable);
    ASSERT(success);


    if (spt_entry->spe_status == mapped_file || spt_entry->spe_status == mapped_file_nowriteback) {
      d_printf("m_file %p \n", (void *) spt_entry->vaddr);
      // read contents from file into newly allocated frame
      file_seek(spt_entry->file, (int)spt_entry->file_offset);
      // this may block and run another thread in the meantime
      file_read(spt_entry->file, frame_pointer, (int)spt_entry->read_bytes);

      spt_entry->spe_status = spt_entry->spe_status ==
              mapped_file_nowriteback ? frame : frame_from_file;
//      printf("mapping in from file to user vaddr %p of process "
//             "\"%s\" at frame %p\n", (void*)page_vaddr, t->name, frame_pointer);
    }
    else if (spt_entry->spe_status == zeroes)
    {
//      printf("mapped zero page at vaddr %p of process "
//             "\"%s\" at frame %p\n", (void*)page_vaddr, t->name, frame_pointer);
      spt_entry->spe_status = frame;
    }
    else if (spt_entry->spe_status == swap)
    {
      // read in from swap
      size_t swap_slot = spt_entry->swap_slot;
//      printf("swapping in from slot %u to user vaddr %p of process "
//             "\"%s\" at frame %p\n",
//             swap_slot, (void*)page_vaddr, t->name, frame_pointer);
      swap_to_frame(spt_entry->swap_slot, frame_pointer);
      spt_entry->swap_slot = 0;
      spt_entry->spe_status = frame;
    }
    else {
      printf("Tried to read from page %p (process \"%s\") %p\n", (void*)
      page_vaddr, t->name, t->pagedir);
      printf("Unhandled spe_status %u\n", spt_entry->spe_status);
      ASSERT(0);
    }
    frametable_unlock();
    return;
  }

  if (!user) {
    // They also assume that you've modified page_fault() so that a page fault
    // in the kernel merely sets eax to 0xffffffff and copies its former value
    // into eip.
    f->eip = (void (*)(void))f->eax;
    f->eax = 0xFFFFFFFF;
    return;
  }
  else {
    printf ("Page fault at %p: %s error %s page in %s context.\n",
            fault_addr,
            not_present ? "not present" : "rights violation",
            write ? "writing" : "reading",
            user ? "user" : "kernel");
    // user program caused page fault
    process_terminate(t, -1, t->program_name);
  }

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}

