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

struct hash cache;
struct block *fs_device;
struct semaphore shutdown_sema;

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

static _Noreturn void thread_flush(void);

void init_cache()
{
  hash_init(&cache, cache_entry_hash, cache_entries_hash_less, NULL);

  thread_create("fs-flush", 0, thread_flush, "system");

  fs_device = block_get_role (BLOCK_FILESYS);

  sema_init(&shutdown_sema, 0);
}

struct cache_entry *cache_get_entry(block_sector_t sector)
{
  struct cache_entry find_entry = {
          .sector = sector
  };

  struct hash_elem *elem = hash_find(&cache, &find_entry.elem);

  if (elem == NULL)
    return NULL;

  return hash_entry(elem, struct cache_entry, elem);
}

static bool cache_has_space_available()
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
}

static void cache_evict_some_entry()
{
  ASSERT(hash_size(&cache) > 0);

  struct hash_iterator iter;
  hash_first(&iter, &cache);

  block_sector_t sector = 0;

  while (hash_next(&iter))
  {
    struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
            elem);
    // TODO: Implement smart heuristic for eviction
    if (!e->pinned && e->sector > sector) sector = e->sector;
  }

  struct cache_entry *entry_to_evict = cache_get_entry(sector);
  if (entry_to_evict->dirty)
  {
    printf("Buffercache: evict entry to disk\n");
    block_write(fs_device, entry_to_evict->sector, entry_to_evict->data);
  }
  else
  {
    printf("Buffercache: evict undirty entry\n");
  }
  _delete_entry(entry_to_evict);
}

static void write_cache_to_disk()
{
  struct hash_iterator iter;
  hash_first(&iter, &cache);

  while (hash_next(&iter))
  {
    struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                       elem);

    ASSERT(!e->pinned)
    if (e->dirty)
    {
      printf("Buffercache: flushing sector %u to disk\n", e->sector);
      e->pinned = true;
      e->dirty = false;
      block_write(fs_device, e->sector, e->data);
      e->pinned = false;
    }
  }
}



void cache_shutdown(void)
{
  sema_down(&shutdown_sema);
}

static _Noreturn void thread_flush(void)
{
  while (true)
  {
  //  printf("sleep\n");
    timer_sleep(100);
//printf("flush\n");

    write_cache_to_disk();

    if (!list_empty(&shutdown_sema.waiters))
    {
      printf("sema up\n");
      sema_up(&shutdown_sema);
    }
  }
}

void
cache_block_read (struct block *block, block_sector_t sector, void *buffer)
{
  printf("read sector %u from ", sector);
  // Unterscheiden ob es drin ist oder nicht
  struct cache_entry *c_entry = cache_get_entry(sector);

  if (c_entry == NULL)
  {
    if (!cache_has_space_available())
    {
      cache_evict_some_entry();
    }

    printf("disk\n");

    struct cache_entry *e = cache_create_entry(sector);
    ASSERT(e != NULL);
    block_read(block, sector, e->data);
    memcpy(buffer, e->data, BLOCK_SECTOR_SIZE);
  }
  else
  {
    printf("cache\n");
    c_entry->accessed = true;
    memcpy(buffer, c_entry->data, BLOCK_SECTOR_SIZE);
  }
}

void
cache_block_write (struct block *block, block_sector_t sector, const void
        *buffer)
{
  printf("write sector %u\n", sector);

  struct cache_entry *c_entry = cache_get_entry(sector);

  if (c_entry == NULL)
  {
    if (!cache_has_space_available())
    {
      cache_evict_some_entry();
    }

    c_entry = cache_create_entry(sector);
    ASSERT(c_entry != NULL);

    memcpy(c_entry->data, buffer, BLOCK_SECTOR_SIZE);

    c_entry->dirty = true;
    c_entry->accessed = true;
  }
  else
  {
    c_entry->accessed = true;
    c_entry->dirty = true;

    memcpy(c_entry->data, buffer, BLOCK_SECTOR_SIZE);
  }
}

