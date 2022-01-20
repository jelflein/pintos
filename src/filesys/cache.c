//
// Created by pintos on 19.01.22.
//

#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "../lib/debug.h"
#include "../lib/kernel/hash.h"
#include "../devices/block.h"
#include "../threads/malloc.h"
#include "../devices/timer.h"
#include "../threads/thread.h"

#define CACHE_ENTRIES 64

static struct hash cache;
static struct block *fs_device;
static struct semaphore shutdown_sema;

static struct lock eviction_lock;

/* Computes and returns the hash value for hash element E, given
   auxiliary data AUX. */
static unsigned cache_entry_hash (const struct hash_elem *e, void *aux UNUSED)
{
  return hash_entry(e, struct cache_entry, elem)->sector;
}

/* Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
static bool cache_entries_hash_less (const struct hash_elem *a,
                                   const struct hash_elem *b,
                                   void *aux UNUSED)
{
  ASSERT (a != NULL);
  ASSERT (b != NULL);

  return cache_entry_hash(a, aux) < cache_entry_hash(b, aux);
}

static _Noreturn void thread_flush(void * aux UNUSED);

void init_cache()
{
  hash_init(&cache, cache_entry_hash, cache_entries_hash_less, NULL);

  thread_create("fs-flush", 0, thread_flush, "system");

  fs_device = block_get_role (BLOCK_FILESYS);

  sema_init(&shutdown_sema, 0);

  lock_init(&eviction_lock);
}

static struct cache_entry *cache_get_entry(block_sector_t sector)
{
  struct cache_entry find_entry = {
          .sector = sector
  };

  struct hash_elem *elem = hash_find(&cache, &find_entry.elem);

  if (elem == NULL)
    return NULL;

  return hash_entry(elem, struct cache_entry, elem);
}

static bool cache_has_space_available(void)
{
  return hash_size(&cache) < CACHE_ENTRIES;
}

static struct cache_entry *cache_create_entry(block_sector_t sector)
{
  ASSERT(cache_has_space_available());

  struct cache_entry *e = malloc(sizeof(struct cache_entry));

  if (e == NULL)
    return NULL;

  e->sector = sector;
  e->dirty = false;
  e->accessed = false;
  e->pinned = false;
  lock_init(&e->lock);

  if (hash_insert(&cache, &e->elem) != NULL)
  {
    // this item already exists
    free(e);

    ASSERT(0);
  }

  return e;
}

static void _delete_entry(struct cache_entry *e)
{
  struct hash_elem *he = hash_delete(&cache, &e->elem);
  ASSERT(he != NULL);
  free(e);
}

static void cache_evict_some_entry(void)
{
  ASSERT(hash_size(&cache) > 0);

  struct hash_iterator iter;
  hash_first(&iter, &cache);

  struct cache_entry *lowest_c_entry = NULL;

  while (hash_next(&iter))
  {
    struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
            elem);

    // TODO: Be smart about evicting undirty entries first
    if (!e->pinned && (lowest_c_entry == NULL
      || e->lru_timestamp < lowest_c_entry->lru_timestamp))
    {
      lowest_c_entry = e;
    }
  }

  ASSERT(lowest_c_entry != NULL && "Have to find one entry to evict");

  struct cache_entry *lowest_c_entry_copy = malloc(sizeof(struct cache_entry));
  memcpy(lowest_c_entry_copy, lowest_c_entry, sizeof(struct cache_entry));
  _delete_entry(lowest_c_entry);

  if (lowest_c_entry_copy->dirty)
  {
    d_printf("Buffercache: evict entry to disk\n");
    block_write(fs_device, lowest_c_entry_copy->sector, lowest_c_entry_copy
    ->data);
  }
  else {
    d_printf("Buffercache: evict undirty entry\n");
  }
  free(lowest_c_entry_copy);
}

static void write_cache_to_disk(void)
{
  struct hash_iterator iter;
  hash_first(&iter, &cache);

  struct cache_entry **flush_entries = malloc(CACHE_ENTRIES * sizeof(void *));
  memset(flush_entries, 0, CACHE_ENTRIES * sizeof(struct
          cache_entry));

  uint8_t i = 0;
  while (hash_next(&iter))
  {
    struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                       elem);

    ASSERT(!e->pinned)
    if (e->dirty)
    {
      d_printf("Buffercache: flushing sector %u to disk\n", e->sector);
      e->pinned = true;
      e->dirty = false;
      lock_acquire(&e->lock);

      flush_entries[i] = e;
      i++;
    }
  }

  for (int j = 0; j < i; j++)
  {
    struct cache_entry *e = flush_entries[j];
    block_write(fs_device, e->sector, e->data);
    lock_release(&e->lock);
    e->pinned = false;
  }
}



void cache_shutdown(void)
{
  sema_down(&shutdown_sema);
}

static _Noreturn void thread_flush(void * aux UNUSED)
{
  while (true)
  {
    timer_sleep(100);

    write_cache_to_disk();

    if (!list_empty(&shutdown_sema.waiters))
    {
      d_printf("sema up\n");
      sema_up(&shutdown_sema);
    }
  }
}

void
cache_block_read (struct block *block, block_sector_t sector, void *buffer)
{
  cache_block_read_chunk(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_block_write (struct block *block, block_sector_t sector, const void
        *buffer) {
  cache_block_write_chunk(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_block_write_chunk (struct block *block, block_sector_t sector, const
  void *buffer, uint32_t chunk_size, uint32_t sector_ofs) {
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  d_printf("write sector %u\n", sector);

  struct cache_entry *c_entry = cache_get_entry(sector);

  //Not in Cache
  if (c_entry == NULL) {
    bool acquired_evict_lock = false;

    if (!cache_has_space_available())
    {
      d_printf("a lock write\n");
      lock_acquire(&eviction_lock);
      acquired_evict_lock = true;
      cache_evict_some_entry();
    }

    c_entry = cache_create_entry(sector);
    ASSERT(c_entry != NULL);
    c_entry->pinned = true;

    if(acquired_evict_lock) {
      d_printf("re lock write\n");
      lock_release(&eviction_lock);
    }

    if (sector_ofs != 0 || chunk_size < BLOCK_SECTOR_SIZE)
    {
      // read whole sector in first
      d_printf("a lock write\n");
      lock_acquire(&c_entry->lock);
      d_printf("in lock write\n");
      block_read(block, sector, c_entry->data);
      d_printf("release lock write\n");
      lock_release(&c_entry->lock);
    }

    //push changes to cache
    memcpy(c_entry->data + sector_ofs, buffer, chunk_size);

    c_entry->dirty = true;
    c_entry->accessed = true;
    c_entry->lru_timestamp = (uint32_t) get_time_since_start();
    c_entry->pinned = false;
  } else {
    c_entry->accessed = true;
    c_entry->dirty = true;
    c_entry->lru_timestamp = (uint32_t) get_time_since_start();

    //assumption c_entry->data is complete loaded
    memcpy(c_entry->data + sector_ofs, buffer, chunk_size);
  }
}

void
cache_block_read_chunk(struct block *block, block_sector_t sector, void
*buffer, uint32_t chunk_size, uint32_t sector_ofs)
{
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  d_printf("read sector %u from ", sector);
  // Unterscheiden ob es drin ist oder nicht
  struct cache_entry *c_entry = cache_get_entry(sector);

  if (c_entry == NULL)
  {
    bool acquired_evict_lock = false;

    if (!cache_has_space_available())
    {
      d_printf("a lock read\n");
      lock_acquire(&eviction_lock);
      acquired_evict_lock = true;
      cache_evict_some_entry();
    }

    d_printf("disk\n");

    c_entry = cache_create_entry(sector);
    ASSERT(c_entry != NULL);
    c_entry->pinned = true;

    if(acquired_evict_lock) {
      d_printf("release lock read\n");
      lock_release(&eviction_lock);
    }

    c_entry->lru_timestamp = (uint32_t)get_time_since_start();

    d_printf("a lock read\n");
    lock_acquire(&c_entry->lock);
    d_printf("in lock read\n");
    block_read(block, sector, c_entry->data);
    d_printf("release lock read\n");
    lock_release(&c_entry->lock);

    memcpy(buffer, c_entry->data + sector_ofs, chunk_size);

    c_entry->pinned = false;
  }
  else
  {
    d_printf("cache\n");

    c_entry->accessed = true;
    c_entry->lru_timestamp = (uint32_t)get_time_since_start();

    memcpy(buffer, c_entry->data + sector_ofs, chunk_size);
  }
}

