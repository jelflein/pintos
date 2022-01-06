//
// Created by pintos on 15.12.21.
//

#include <userprog/pagedir.h>
#include "page.h"
#include "../lib/user/syscall.h"
#include "../lib/stdio.h"
#include "../threads/malloc.h"
#include "../threads/vaddr.h"
#include "frame.h"
#include <threads/thread.h>

struct hash spt;

/* Computes and returns the hash value for hash element E, given
   auxiliary data AUX. */
unsigned spt_entry_hash (const struct hash_elem *e, void *aux UNUSED)
{
  return hash_entry(e, struct spt_entry, elem)->vaddr ^ hash_entry(e, struct
          spt_entry, elem)->pid;
}

/* Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
bool spt_entries_hash_less (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux UNUSED)
{
  ASSERT (a != NULL);
  ASSERT (b != NULL);
  /*
   *   return hash_entry (a, struct vm_entry, elem)->vaddr
    < hash_entry (b, struct vm_entry, elem)->vaddr;
   */

  return spt_entry_hash(a, aux) < spt_entry_hash(b, aux);
}

void spt_init()
{
  hash_init(&spt, spt_entry_hash, spt_entries_hash_less, NULL);
}





//
//        v_page_id | pid | status | mapped file??
//
//spte->upage = upage;
//spte->kpage = NULL;
//spte->status = FROM_FILESYS;
//spte->dirty = false;
//spte->file = file;
//spte->file_offset = offset;
//spte->read_bytes = read_bytes;
//spte->zero_bytes = zero_bytes;
//spte->writable = writable;


struct spt_entry *_spt_entry(uint32_t vaddr, pid_t pid, uint32_t paddr, bool
        writable, enum
        spe_status spe_status)
{
  struct spt_entry *e = malloc(sizeof(struct spt_entry));

  if (e == NULL)
    return NULL;

  e->pid = pid;
  e->vaddr = vaddr;
  e->spe_status = spe_status;
  e->writable = writable;

  hash_insert(&spt, &e->elem);

  return e;
}


bool spt_entry_empty(uint32_t vaddr, pid_t pid, bool writable, enum spe_status
        spe_status) {
  return spt_entry(vaddr, pid, 0, writable, spe_status);
}

bool spt_entry(uint32_t vaddr, pid_t pid, uint32_t frame_addr, bool writable,
               enum
spe_status spe_status)
{
  return _spt_entry(vaddr, pid, frame_addr, writable, spe_status) != NULL;
}


bool spt_entry_mapped_file(uint32_t vaddr, pid_t pid,
                           bool writable, struct file *mapped_f,
                           size_t file_offset, size_t file_read_size)
{
  struct spt_entry *entry = _spt_entry(vaddr, pid, 0, writable, mapped_file);

  if (entry == NULL)
    return false;

  entry->file = mapped_f;
  entry->file_offset = file_offset;
  entry->read_bytes = file_read_size;

  return true;
}


struct spt_entry *spt_get_entry(uint32_t vaddr, pid_t pid)
{
  struct spt_entry find_entry = {
      .vaddr = vaddr,
      .pid = pid
  };

  struct hash_elem *elem = hash_find(&spt, &find_entry.elem);
  if (elem == NULL)
    return NULL;

  return hash_entry(elem, struct spt_entry, elem);
}

void spt_remove_entry(uint32_t vaddr, struct thread *t)
{
  struct spt_entry *e = spt_get_entry(vaddr, t->tid);
  ASSERT(e != NULL);

  if (e->spe_status == frame)
  {
    // only need to free a frame if we allocated one.
    void *paddr = pagedir_get_page(t->pagedir, (void *)vaddr);
    pagedir_clear_page(t->pagedir, (void *)vaddr);
    free_frame((void *)paddr);
  }

  hash_delete(&spt, &e->elem);
}

bool spt_file_overlaping(uint32_t addr, off_t file_size, pid_t pid) {
    for (unsigned int i = 0; i < (uint32_t) file_size; i += PGSIZE) {
        if (spt_get_entry(addr + i , pid) != NULL) return true;
    }

    return false;
}
