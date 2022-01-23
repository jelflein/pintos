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

static struct list read_ahead_queue;
static block_sector_t read_ahead_block = UINT32_MAX;
static struct semaphore read_ahead_sema;
static struct semaphore read_ahead_queue_not_empty_sema;

struct read_ahead_entry {
    struct list_elem list_elem;
    block_sector_t sector;
};

static void enqueue_read_ahead_sector(block_sector_t sector);

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
static _Noreturn void thread_read_ahead(void * aux UNUSED);

void init_cache()
{
  hash_init(&cache, cache_entry_hash, cache_entries_hash_less, NULL);

  thread_create("fs-flush", 0, thread_flush, "system");
//  thread_create("fs-read-ahead", 0, thread_read_ahead, "system");

  fs_device = block_get_role (BLOCK_FILESYS);

  sema_init(&shutdown_sema, 0);

  lock_init(&eviction_lock);

  list_init(&read_ahead_queue);

  sema_init(&read_ahead_sema, 0);
  sema_init(&read_ahead_queue_not_empty_sema, 0);
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
  return hash_size(&cache) <= CACHE_ENTRIES;
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

//d_printf("hash insert\n");
  if (hash_insert(&cache, &e->elem) != NULL)
  {
    ASSERT(0);
  }

  return e;
}

static void _delete_entry(struct cache_entry *e)
{
  //d_printf("remove entry %u\n", e->sector);
  struct hash_elem *he = hash_delete(&cache, &e->elem);
  ASSERT(he != NULL);
}

static void cache_evict_some_entry(void)
{
  while (true)
  {
    ASSERT(hash_size(&cache) > 0);

    struct hash_iterator iter;
    hash_first(&iter, &cache);

    struct cache_entry *lowest_c_entry = NULL;

    while (hash_next(&iter)) {
      struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                         elem);

      // TODO: Be smart about evicting undirty entries first
      if (!e->pinned && (lowest_c_entry == NULL
                         || e->lru_timestamp < lowest_c_entry->lru_timestamp)) {
        lowest_c_entry = e;
      }
    }

    ASSERT(lowest_c_entry != NULL && "Have to find one entry to evict");

    lock_acquire(&lowest_c_entry->lock);
    if (lowest_c_entry->pinned) {
      lock_release(&lowest_c_entry->lock);
      // restart
      continue;
    }

    _delete_entry(lowest_c_entry);

    if (lowest_c_entry->dirty) {
      //d_printf("Buffercache: evict sector %u to disk\n",
//               lowest_c_entry->sector);
      block_write(fs_device, lowest_c_entry->sector, lowest_c_entry->data);
    } else {
      //d_printf("Buffercache: evict undirty entry\n");
    }

    //d_printf("delte entry %u\n", lowest_c_entry->sector);
    lock_release(&lowest_c_entry->lock);
    free(lowest_c_entry);
    break;
  }
}

static void print_buffer(uint8_t *b, uint32_t length)
{
  for (uint32_t i = 0; i < length; i++)
  {
    printf("%02X", b[i]);
  }
}

static void write_cache_to_disk(void)
{
  for (int i = 0; i < CACHE_ENTRIES; i++) {
    struct hash_iterator iter;
    hash_first(&iter, &cache);

    bool at_least_one_dirty = false;
    while (hash_next(&iter)) {
      struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                         elem);

      if (e->dirty && !e->pinned) {
        at_least_one_dirty = true;
        //d_printf("Buffercache: flushing sector %u to disk\n", e->sector);
        e->pinned = true;
        e->dirty = false;
        lock_acquire(&e->lock);
        block_write(fs_device, e->sector, e->data);
        lock_release(&e->lock);
        e->pinned = false;
        // we cannot continue iterating after blocking for a while, iterator
        // invalid
        break;
      }
    }

    if (!at_least_one_dirty) break;
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
      //d_printf("sema up\n");
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
  void *buffer, const uint32_t chunk_size, const uint32_t sector_ofs) {
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  //d_printf("write sector %u (%u..%u)\n", sector, sector_ofs, sector_ofs + chunk_size);

  struct cache_entry *c_entry = cache_get_entry(sector);

  do
  {
    //Evict
    while (c_entry == NULL && !cache_has_space_available())
    {
      //d_printf("a lock write\n");
      lock_acquire(&eviction_lock);
      cache_evict_some_entry();

      c_entry = cache_get_entry(sector);
      lock_release(&eviction_lock);
    }

    if (c_entry == NULL)
    {
      c_entry = cache_create_entry(sector);

      ASSERT(c_entry != NULL);
      c_entry->pinned = true;

      if (sector_ofs != 0 || chunk_size < BLOCK_SECTOR_SIZE)
      {
        // read whole sector in first
        //d_printf("a lock write\n");
        lock_acquire(&c_entry->lock);
        ASSERT(cache_get_entry(sector) != NULL);
        //d_printf("in lock write\n");
        block_read(block, sector, c_entry->data);
        //d_printf("release lock write\n");
        lock_release(&c_entry->lock);
      }

      //push changes to cache
      lock_acquire(&c_entry->lock);
      ASSERT(cache_get_entry(sector) != NULL);

      memcpy(c_entry->data + sector_ofs, buffer, chunk_size);
      lock_release(&c_entry->lock);

      c_entry->dirty = true;
      c_entry->accessed = true;
      c_entry->lru_timestamp = (uint32_t) get_time_since_start();
      c_entry->pinned = false;
    }
    else
    {
      lock_acquire(&c_entry->lock);
      c_entry = cache_get_entry(sector);

      if (c_entry != NULL)
      {
        // write to cached entry

        c_entry->accessed = true;
        c_entry->dirty = true;
        c_entry->lru_timestamp = (uint32_t) get_time_since_start();

        //assumption c_entry->data is complete loaded
        memcpy(c_entry->data + sector_ofs, buffer, chunk_size);
        lock_release(&c_entry->lock);
      }
    }
  } while (c_entry == NULL);
}

static void
cache_block_read_chunk_readahead(struct block *block, block_sector_t sector,
        void *buffer, uint32_t chunk_size, uint32_t sector_ofs, bool
        is_readahead)
{
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  //d_printf("read sector %u\n", sector);

  if (sector == read_ahead_block && !is_readahead)
  {
    sema_down(&read_ahead_sema);
  }

  // Unterscheiden ob es drin ist oder nicht
  struct cache_entry *c_entry = cache_get_entry(sector);
  bool acquired_evict_lock = false;

  bool is_first_try = true;
  while (c_entry == NULL && !cache_has_space_available())
  {
    //d_printf("evicting for read sector %u\n", sector);
    if (is_first_try) lock_acquire(&eviction_lock);
    is_first_try = false;
    acquired_evict_lock = true;
    cache_evict_some_entry();

    c_entry = cache_get_entry(sector);
  }

  if (c_entry == NULL)
  {
    c_entry = cache_create_entry(sector);
    ASSERT(c_entry != NULL);
    c_entry->pinned = true;

    if (acquired_evict_lock) {
      lock_release(&eviction_lock);
    }

    c_entry->lru_timestamp = (uint32_t)get_time_since_start();

    lock_acquire(&c_entry->lock);
    ASSERT(cache_get_entry(sector) != NULL);
    //d_printf("read sector %u from disk\n", sector);
    block_read(block, sector, c_entry->data);


    if (!is_readahead) {
      memcpy(buffer, c_entry->data + sector_ofs, chunk_size);
    }

    lock_release(&c_entry->lock);
    c_entry->pinned = false;

    if (!is_readahead)
    {
      if (sector + 1 < block_size(fs_device))
      {
        enqueue_read_ahead_sector(sector + 1);
      }
    }
  }
  else
  {
    if (acquired_evict_lock) {
      lock_release(&eviction_lock);
    }

    //ASSERT(!is_readahead);
    if (is_readahead) return;
    //d_printf("read sector %u from cache (%u..%u)\n", sector, sector_ofs,
    //         sector_ofs + chunk_size);

    c_entry->pinned = true;
    ASSERT(cache_get_entry(sector) != NULL);
    ASSERT(cache_get_entry(sector) == c_entry);
    lock_acquire(&c_entry->lock);
    ASSERT(cache_get_entry(sector) != NULL);

    c_entry->accessed = true;
    c_entry->lru_timestamp = (uint32_t)get_time_since_start();

    memcpy(buffer, c_entry->data + sector_ofs, chunk_size);
    c_entry->pinned = false;

    lock_release(&c_entry->lock);
  }
}


void
cache_block_read_chunk(struct block *block, block_sector_t sector, void
*buffer, uint32_t chunk_size, uint32_t sector_ofs)
{
  cache_block_read_chunk_readahead(block, sector, buffer, chunk_size,
                                   sector_ofs, false);
}


static bool is_in_queue(block_sector_t sector)
{
  struct list_elem *e;
  struct list *l = &read_ahead_queue;

  for (e = list_begin(l); e != list_end(l);
       e = list_next(e)) {
    struct read_ahead_entry *rh = list_entry(e, struct read_ahead_entry,
                                                 list_elem);

    if (rh->sector == sector)
    {
      return true;
    }
  }

  return false;
}

static void enqueue_read_ahead_sector(block_sector_t sector) {
  if (cache_get_entry(sector) != NULL) return;
  if (is_in_queue(sector)) return;

  struct read_ahead_entry *e = malloc(sizeof(struct read_ahead_entry));
  ASSERT(e != NULL);
  e->sector = sector;

  bool list_was_empty = list_empty(&read_ahead_queue);
  list_push_back(&read_ahead_queue, &e->list_elem);

  if (list_was_empty) {
    //d_printf("waking read ahead thread list size %u\n", list_size
    //(&read_ahead_queue));
    //d_printf("waking up read ahead thread\n");
    sema_up(&read_ahead_queue_not_empty_sema);
  }
}

static _Noreturn void thread_read_ahead(void *aux UNUSED)
{
  while (true)
  {
    if (list_empty(&read_ahead_queue))
    {
      //d_printf("read ahead thread waiting for work\n");
      ASSERT(read_ahead_queue_not_empty_sema.value == 0);
      sema_down(&read_ahead_queue_not_empty_sema);
      //d_printf("read ahead thread woken up. list size %u\n", list_size
      //(&read_ahead_queue));
    }

    struct list_elem *e = list_front(&read_ahead_queue);
    struct read_ahead_entry *entry = list_entry(e, struct read_ahead_entry,
                                                list_elem);

    // don't load it if we already have it
    //d_printf("read-ahead %u\n", entry->sector);
    if (cache_get_entry(entry->sector) != NULL)
    {
      goto cleanup;

    }

    read_ahead_block = entry->sector;
    //start read
    cache_block_read_chunk_readahead(fs_device, read_ahead_block, NULL,
                                     BLOCK_SECTOR_SIZE, 0, true);

    //after read
    for (uint32_t i = 0; i < list_size(&read_ahead_sema.waiters); ++i) {
      sema_up(&read_ahead_sema);
    }

    read_ahead_block = UINT32_MAX;

    cleanup:
    list_pop_front(&read_ahead_queue);
    free(entry);
  }
}