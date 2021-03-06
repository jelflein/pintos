#include "frame.h"
#include "../threads/palloc.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../threads/loader.h"
#include "../threads/vaddr.h"
#include "../lib/debug.h"
#include "../threads/thread.h"
#include "../userprog/pagedir.h"
#include "../threads/interrupt.h"
#include "swap.h"
#include "page.h"
#include "../filesys/file.h"
#include "../userprog/syscall.h"

struct lock lock;
struct frame_entry *frame_table = NULL;

uint32_t num_frames_available = 0;
uint32_t num_frames_total = 0;

int32_t index_of_smallest_score = -1;

#define ONE_MB (1024*1024)
#define TABLE_INDEX(addr) (vtop(addr - ONE_MB) >> 12)
#define ADDR_FROM_TABLE_INDEX(idx) (ptov(idx << 12) + ONE_MB)

void do_swapping(void);

void frametable_lock()
{
  lock_acquire(&lock);
}
void frametable_unlock()
{
  lock_release(&lock);
}


static bool entry_is_empty(struct frame_entry entry)
{
  return entry.thread == NULL;
}


static struct frame_entry get_entry_to_evict(uint32_t *idx);

static struct frame_entry get_entry_to_evict(uint32_t *idx)
{
  enum intr_level il = intr_disable();
  if (index_of_smallest_score != -1)
  {
    *idx = index_of_smallest_score;
    struct frame_entry entry = frame_table[index_of_smallest_score];
    // mark as empty entry
    frame_table[index_of_smallest_score].thread = NULL;
    index_of_smallest_score = -1;
    intr_set_level(il);
    return entry;
  }


  int32_t smallest_index = -1;
  uint8_t smallest_score = UINT8_MAX;
  for (uint32_t i = 0; i < num_frames_total; i++)
  {
    struct frame_entry entry = frame_table[i];
    if (!entry_is_empty(entry) && entry.pinned == false)
    {
      if (entry.eviction_score < smallest_score)
      {
        smallest_score = entry.eviction_score;
        smallest_index = (int32_t)i;
      }
    }
  }

  struct frame_entry fe = frame_table[smallest_index];
  frame_table[smallest_index].thread = NULL;
  frame_table[smallest_index].page = 0;
  frame_table[smallest_index].eviction_score = 0;
  frame_table[smallest_index].pinned = false;
  *idx = smallest_index;
  intr_set_level(il);
  return fe;
}

void *
allocate_frame(struct thread *t, enum palloc_flags fgs, uint32_t page_addr)
{
  lock_acquire(&lock);
  //if swapping
  if (num_frames_available <= 1)
  {
    do_swapping();
  }

  //allocate
  void *u_frame = palloc_get_page(PAL_USER | fgs);

  ASSERT(u_frame != NULL)

  ASSERT(TABLE_INDEX(u_frame) <= num_frames_total);

//  printf("allocating frame %u\n", TABLE_INDEX(u_frame));

  ASSERT(ADDR_FROM_TABLE_INDEX(TABLE_INDEX(u_frame)) == u_frame);

  ASSERT(t != NULL);

  ASSERT(entry_is_empty(frame_table[TABLE_INDEX(u_frame)]));

  struct frame_entry e = {
    .page = page_addr,
    .thread = t,
    .eviction_score = 50,
    .pinned = false
  };
  frame_table[TABLE_INDEX(u_frame)] = e;

  num_frames_available--;

  lock_release(&lock);

  return u_frame;
}

void do_swapping() {
  uint32_t fe_index;
  struct frame_entry fe = get_entry_to_evict(&fe_index);

  ASSERT(fe.thread != NULL);
  struct spt_entry *se = spt_get_entry(fe.thread, fe.page, fe.thread->tid);
  ASSERT(se != NULL);

  uint32_t *pagedir_swapped_process = fe.thread->pagedir;

//    printf("evicting frame %u (page %p from process \"%s\")%p\n", fe_index,
//           (void*)fe.page, fe.thread->name, pagedir_swapped_process);
  if (se->spe_status == frame_from_file)
  {
    // write to file, throw out frame
    // flush to file
    void *kernel_addr = pagedir_get_page(pagedir_swapped_process, (void*)se->vaddr);
    pagedir_clear_page(pagedir_swapped_process, (void *)se->vaddr);
    se->spe_status = mapped_file;
    if (se->writable)
    {
      bool was_already_locked = fs_lock_held_by_current_thread();
      if (!was_already_locked) fs_lock();

      file_seek(se->file, (int) se->file_offset);
      file_write(se->file, kernel_addr, (int) se->read_bytes);

      if (!was_already_locked) fs_unlock();
    }


    free_frame(kernel_addr);
  }
  else if (se->writable && se->spe_status == frame)
  {
    // write to swap
    void *kernel_addr = pagedir_get_page(pagedir_swapped_process, (void*)se->vaddr);
    pagedir_clear_page(pagedir_swapped_process, (void *)se->vaddr);
    se->spe_status = swap;
    uint32_t swap_id = frame_to_swap(kernel_addr);
    se->swap_slot = swap_id;
//    printf("wrote to swap slot %u\n", swap_id);
    free_frame(kernel_addr);
  }
  else {
    ASSERT(0);
  }
}

void free_frame(void *frame)
{
  struct frame_entry e = {
    .page = 0,
    .thread = NULL,
    .eviction_score = 0,
    .pinned = false
  };

  frame_table[TABLE_INDEX(frame)] = e;

  palloc_free_page(frame);

  num_frames_available++;
}


static uint32_t divide_round_up(uint32_t a, uint32_t b)
{
  return (a + b - 1) / b;
}

void frame_table_init(uint32_t num_user_frames, uint32_t num_total_frames)
{
  lock_init (&lock);

  num_frames_available = num_user_frames;
  num_frames_total = num_total_frames;
  // we only keep track of user frames, but due to our addressing strategy,
  // the table still needs to be large enough to hold all frames
  uint32_t table_size_bytes = sizeof(struct frame_entry) * num_frames_total;

  frame_table = palloc_get_multiple(PAL_ZERO | PAL_ASSERT,
                                    divide_round_up(table_size_bytes, PGSIZE));
}

void set_pinned(void* frame)
{
  uint32_t index = TABLE_INDEX(frame);
  struct frame_entry fe = frame_table[index];

  ASSERT(!entry_is_empty(fe));
  fe.pinned = true;

  if ((int32_t)index == index_of_smallest_score)
    compute_eviction_score();
}

void unpin(void* frame)
{
  uint32_t index = TABLE_INDEX(frame);
  struct frame_entry fe = frame_table[index];

  ASSERT(!entry_is_empty(fe));
  fe.pinned = false;
}

void compute_eviction_score()
{
  int32_t smallest_index = -1;
  uint8_t smallest_score = UINT8_MAX;
  for (uint32_t i = 0; i < num_frames_total; i++)
  {
    struct frame_entry entry = frame_table[i];
    if (!entry_is_empty(entry) && entry.pinned == false)
    {
      void *kernel_addr = ADDR_FROM_TABLE_INDEX(i);
      // check user address and kernel address
      if (pagedir_is_accessed(entry.thread->pagedir, (void*)entry.page)
      ||  pagedir_is_accessed(entry.thread->pagedir, kernel_addr))
      {
        pagedir_set_accessed(entry.thread->pagedir, (void*)entry.page, false);
        pagedir_set_accessed(entry.thread->pagedir, kernel_addr, false);

        if (entry.eviction_score < UINT8_MAX)
          entry.eviction_score++;
      }
      else
      {
        if (entry.eviction_score > 0)
        {
          entry.eviction_score--;
        }
      }
      if (entry.eviction_score < smallest_score)
      {
        smallest_score = entry.eviction_score;
        smallest_index = (int32_t)i;
      }
    }
  }

  index_of_smallest_score = smallest_index;
}