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
#include "../vm/page.h"
#include "../vm/frame.h"

#define CACHE_ENTRIES 64

static volatile uint16_t cache_size = 0;

static struct hash cache;
static struct block *fs_device;
static struct semaphore shutdown_sema;

static struct lock eviction_lock;
static struct lock read_ahead_queue_lock;

static struct condition is_empty;

static struct list read_ahead_queue;
static struct semaphore read_ahead_sema;
static struct semaphore read_ahead_queue_not_empty_sema;

struct read_ahead_entry {
  struct list_elem list_elem;
  block_sector_t sector;
};

static void enqueue_read_ahead_sector(block_sector_t sector);

/* Computes and returns the hash value for hash element E, given
   auxiliary data AUX. */
static unsigned cache_entry_hash(const struct hash_elem *e, void *aux UNUSED) {
  return hash_entry(e, struct cache_entry, elem)->sector;
}

/* Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
static bool cache_entries_hash_less(const struct hash_elem *a,
                                    const struct hash_elem *b,
                                    void *aux UNUSED) {
  ASSERT (a != NULL);
  ASSERT (b != NULL);

  return cache_entry_hash(a, aux) < cache_entry_hash(b, aux);
}

static _Noreturn void thread_flush(void *aux UNUSED);
static _Noreturn void thread_read_ahead(void *aux UNUSED);


void init_cache() {
  fs_device = block_get_role(BLOCK_FILESYS);

  list_init(&read_ahead_queue);
  hash_init(&cache, cache_entry_hash, cache_entries_hash_less, NULL);

  lock_init(&eviction_lock);
  lock_init(&read_ahead_queue_lock);

  cond_init(&is_empty);

  sema_init(&shutdown_sema, 0);
  sema_init(&read_ahead_sema, 0);
  sema_init(&read_ahead_queue_not_empty_sema, 0);

  thread_create("fs-flush", 0, thread_flush, "system");
  thread_create("fs-read-ahead", 0, thread_read_ahead, "system");
}

static struct cache_entry *cache_get_entry(block_sector_t sector) {
  struct cache_entry find_entry = {
          .sector = sector
  };

  struct hash_elem *elem = hash_find(&cache, &find_entry.elem);

  if (elem == NULL)
    return NULL;

  return hash_entry(elem, struct cache_entry, elem);
}

static bool cache_has_space_available(void) {
  return cache_size < CACHE_ENTRIES;
}

static struct cache_entry *cache_create_entry(block_sector_t sector, bool evict, bool is_read_head) {
  d_printf("100 %u %u %u\n", sector, evict, is_read_head);

  ASSERT(cache_has_space_available() || evict || is_read_head);

  struct cache_entry *e = calloc(1, sizeof(struct cache_entry));

  if (e == NULL) {
    ASSERT(0);
  }

  e->sector = sector;
  e->dirty = false;
  e->accessed = false;
  e->pinned = 0;
  e->is_read_head = is_read_head;
  e->is_evcting = false;
  e->lru_timestamp = get_time_since_start();

  cond_init(&e->read_ahead_waiting);
  lock_init(&e->lock);

  if (hash_insert(&cache, &e->elem) != NULL) {
    e = cache_get_entry(sector);
    ASSERT(0);
  }

  ASSERT(cache_has_space_available() || evict || is_read_head);

  if (!is_read_head) {
    d_printf("+++");
    cache_size++;
  }

  ASSERT(cache_size <= CACHE_ENTRIES || is_read_head || evict );

  return e;
}

static void _delete_entry(struct cache_entry *e) {
  d_printf("delete %u", e->sector);
  struct hash_elem *he = hash_delete(&cache, &e->elem);
  ASSERT(he != NULL);
}

static void _delete_elem(struct hash_elem *e) {
  struct hash_elem *he = hash_delete(&cache, e);
  ASSERT(he != NULL);
}

static struct cache_entry *cache_evict_some_entry(block_sector_t sector, bool create_new) {
  ASSERT(hash_size(&cache) > 0);

  if (cache_has_space_available()) return NULL;

  lock_acquire(&eviction_lock);

  do {
    d_printf("evict \n");
    struct hash_iterator iter;
    hash_first(&iter, &cache);

    struct cache_entry *lowest_c_entry = NULL;

    while (hash_next(&iter)) {
      struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                         elem);

      ASSERT(e != NULL);

      // TODO: Be smart about evicting undirty entries first
      if (e->pinned == 0 && (lowest_c_entry == NULL
                         || e->lru_timestamp < lowest_c_entry->lru_timestamp)
                         && !e->is_read_head) {
        lowest_c_entry = e;
      }
    }

    ASSERT(lowest_c_entry != NULL && "Have to find one entry to evict");
    d_printf("evict sleep\n");
    lock_acquire(&lowest_c_entry->lock);
    d_printf("evict re\n");

    ASSERT(lowest_c_entry != NULL);

    if (lowest_c_entry->pinned > 0 || lowest_c_entry->is_read_head) {
      lock_release(&lowest_c_entry->lock);
      // restart
      continue;
    }

    lowest_c_entry->is_evcting = true;

    if (lowest_c_entry->dirty) {
      //d_printf("Buffercache: evict sector %u to disk\n",
//               lowest_c_entry->sector);
      lowest_c_entry->dirty = false;
      lowest_c_entry->lru_timestamp = get_time_since_start();

      block_write(fs_device, lowest_c_entry->sector, lowest_c_entry->data);

      if (lowest_c_entry->pinned > 0 || lowest_c_entry->is_read_head) {
        lock_release(&lowest_c_entry->lock);
        // restart
        continue;
      }
    }

    if (!lowest_c_entry->is_evcting)
      {
      d_printf("evcting recovery\n");
      continue;
    }

    lock_release(&lowest_c_entry->lock);
    lock_release(&eviction_lock);

    d_printf("cache size %u %u\n", cache_size, lowest_c_entry->is_read_head);
    _delete_entry(lowest_c_entry);
    d_printf("cache size %u\n", cache_size);

    ASSERT(!cache_get_entry(lowest_c_entry->sector));


    d_printf("finsih evicut %u\n", lowest_c_entry->sector);

    if(create_new) {
      d_printf("214 create %u %u\n", sector, cache_size);

      struct cache_entry *c = cache_create_entry(sector, true, false);
      cache_size--;
      d_printf("---");

      ASSERT(cache_size <= CACHE_ENTRIES);
      free(lowest_c_entry);

      return c;
    }

    free(lowest_c_entry);

    cache_size--;
    ASSERT(cache_size <= CACHE_ENTRIES);
    return NULL;
  } while (true);
}

static void print_buffer(uint8_t *b, uint32_t length) {
  for (uint32_t i = 0; i < length; i++) {
    printf("%02X", b[i]);
  }
}

static void write_cache_to_disk(void) {
  struct cache_entry* data = calloc(64, sizeof(struct cache_entry));
  ASSERT(data != NULL);

  struct hash_iterator iter;

  bool at_least_one_dirty = false;
  hash_first (&iter, &cache);

  int j = 0;
  while (hash_next(&iter)) {
    struct cache_entry *e = hash_entry(hash_cur(&iter), struct cache_entry,
                                       elem);

    if (e->dirty && e->pinned == 0 && !e->is_read_head) {
      at_least_one_dirty = true;
      e->dirty = false;
      data[j] = *e;
      j++;
    }
  }

  if (!at_least_one_dirty) return;

  for (int i = 0; i < j; i++) {
    struct cache_entry e = data[i];

    block_write(fs_device, e.sector, e.data);
  }

  free(data);
}

void cache_shutdown(void) {
  sema_down(&shutdown_sema);
}

static _Noreturn void thread_flush(void *aux UNUSED) {
  while (true) {
    timer_sleep(100);

    write_cache_to_disk();

    if (!list_empty(&shutdown_sema.waiters)) {
      //d_printf("sema up\n");
      sema_up(&shutdown_sema);
    }
  }
}

void
cache_block_read(struct block *block, block_sector_t sector, void *buffer) {
  cache_block_read_chunk(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_block_write(struct block *block, block_sector_t sector, const void
*buffer) {
  cache_block_write_chunk(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_block_write_chunk(struct block *block, block_sector_t sector, const
void *buffer, const uint32_t chunk_size, const uint32_t sector_ofs) {
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  //d_printf("write sector %u (%u..%u)\n", sector, sector_ofs, sector_ofs + chunk_size);

  struct cache_entry *c_entry = cache_get_entry(sector);

  if (c_entry == NULL) {
    if (!cache_has_space_available()) c_entry = cache_evict_some_entry(sector, true);
    else {
      d_printf("298 create %u\n", sector);
      c_entry = cache_create_entry(sector, false, false);
    }

    ASSERT(c_entry != NULL);
    c_entry->pinned++;

    if (sector_ofs != 0 || chunk_size < BLOCK_SECTOR_SIZE) {
      // read whole sector in first
      //d_printf("a lock write\n");
      d_printf("301 wating %u\n", c_entry->sector);
      lock_acquire(&c_entry->lock);
      d_printf("303 re %u\n", c_entry->sector);

      ASSERT(cache_get_entry(sector) != NULL);
      block_read(block, sector, c_entry->data);

      lock_release(&c_entry->lock);
    }

    d_printf("309 wating %u\n", c_entry->sector);
    lock_acquire(&c_entry->lock);
    d_printf("311 re %u\n", c_entry->sector);

    ASSERT(cache_get_entry(sector) != NULL);
    ASSERT(c_entry != NULL);

    memcpy(c_entry->data + sector_ofs, buffer, chunk_size);

    c_entry->dirty = true;
    c_entry->accessed = true;
    c_entry->lru_timestamp = (uint32_t) get_time_since_start();
    c_entry->pinned--;

    d_printf("324 re %u\n", c_entry->sector);
    lock_release(&c_entry->lock);

    return;
  }

  c_entry->pinned++;

  //in cache
  d_printf("327 wating %u\n", c_entry->sector);
  lock_acquire(&c_entry->lock);
  d_printf("329 re %u\n", c_entry->sector);


  if (c_entry->is_read_head) {
    d_printf("330 wait %u\n", c_entry->sector);
    cond_wait(&c_entry->read_ahead_waiting, &c_entry->lock);
    d_printf("333 realse %u\n", c_entry->sector);
  }

  c_entry = cache_get_entry(sector);

  if (c_entry == NULL) {
    if (!cache_has_space_available()) {
      ASSERT(0);
      c_entry = cache_evict_some_entry(sector, true);
    } else {
      d_printf("352 create %u\n",sector);
      c_entry = cache_create_entry(sector, false, false);
    }
  }

  c_entry->accessed = true;
  c_entry->dirty = true;

  c_entry->lru_timestamp = (uint32_t) get_time_since_start();

  //assumption c_entry->data is complete loaded
  memcpy(c_entry->data + sector_ofs, buffer, chunk_size);

  c_entry->pinned--;

  d_printf("357 wating %u\n", c_entry->sector);
  lock_release(&c_entry->lock);
}

static void
cache_block_read_chunk_readahead(struct block *block, block_sector_t sector,
                                 void *buffer, uint32_t chunk_size, uint32_t sector_ofs, bool
                                 is_readahead) {
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  ASSERT(fs_device == block);

  //d_printf("read sector %u\n", sector);

  struct cache_entry *c_entry = cache_get_entry(sector);

  bool is_read_head_entry = c_entry != NULL && c_entry->is_read_head && is_readahead;

  if (c_entry == NULL || is_read_head_entry) {
    if (!cache_has_space_available() && !is_read_head_entry){
      c_entry = cache_evict_some_entry(sector, true);
    }
    else if (!is_read_head_entry)
    {
      d_printf("391 create %u\n", sector);
      c_entry = cache_create_entry(sector, false, false);
    }

    c_entry->pinned++;
    c_entry->lru_timestamp = (uint32_t) get_time_since_start();

    d_printf("382 wating %u\n", c_entry->sector);
    lock_acquire(&c_entry->lock);
    d_printf("384 re %u\n", c_entry->sector);

    ASSERT(cache_get_entry(sector) != NULL);
    //d_printf("read sector %u from disk\n", sector);
    block_read(block, sector, c_entry->data);

    if(!is_readahead) memcpy(buffer, c_entry->data + sector_ofs, chunk_size);

    d_printf("390 relse %u\n", c_entry->sector);
    lock_release(&c_entry->lock);

    if (is_readahead)
    {
      if (!cache_has_space_available())
      {
        cache_evict_some_entry(sector, false);
      }

      c_entry->is_read_head = false;
      d_printf("+++readhead");
      cache_size++;
      ASSERT(cache_size <= CACHE_ENTRIES);
    }

    c_entry->pinned--;

    if (sector + 1 < block_size(fs_device) && !is_readahead) {
      enqueue_read_ahead_sector(sector + 1);
    }

    return;
  }

  if (is_readahead) return;

  ASSERT(cache_get_entry(sector) != NULL);

  d_printf("415 wait %u\n", c_entry->sector);
  lock_acquire(&c_entry->lock);
  d_printf("417 realse %u\n", c_entry->sector);

  c_entry->pinned++;

  if (c_entry->is_read_head) {
    d_printf("421 wait %u\n", c_entry->sector);
    cond_wait(&c_entry->read_ahead_waiting, &c_entry->lock);
    d_printf("423 realse %u\n", c_entry->sector);
  }

  struct cache_entry *debug = cache_get_entry(sector);
  ASSERT(cache_get_entry(sector) != NULL);

  c_entry->accessed = true;
  c_entry->lru_timestamp = (uint32_t) get_time_since_start();

  memcpy(buffer, c_entry->data + sector_ofs, chunk_size);
  c_entry->pinned--;

  d_printf("435 realse %u\n", c_entry->sector);
  lock_release(&c_entry->lock);
}


void
cache_block_read_chunk(struct block *block, block_sector_t sector, void
*buffer, uint32_t chunk_size, uint32_t sector_ofs) {
  cache_block_read_chunk_readahead(block, sector, buffer, chunk_size,
                                   sector_ofs, false);
}


static bool is_in_queue(block_sector_t sector) {
  struct list_elem *e;
  struct list *l = &read_ahead_queue;

  for (e = list_begin(l); e != list_end(l);
       e = list_next(e)) {
    struct read_ahead_entry *rh = list_entry(e, struct read_ahead_entry,
                                             list_elem);

    if (rh->sector == sector) {
      return true;
    }
  }

  return false;
}

static void enqueue_read_ahead_sector(block_sector_t sector) {
  struct cache_entry *c_entry = cache_get_entry(sector);

  if (c_entry != NULL && c_entry->is_evcting)
  {
    c_entry->is_evcting = false;
  }

  if (c_entry!= NULL) return;
  if (is_in_queue(sector)) return;

  struct read_ahead_entry *e = malloc(sizeof(struct read_ahead_entry));
  ASSERT(e != NULL);

  e->sector = sector;

  bool list_was_empty = list_empty(&read_ahead_queue);
  list_push_back(&read_ahead_queue, &e->list_elem);

  d_printf("504 create %u\n", sector);
  cache_create_entry(sector, false, true);

  if (list_was_empty) {
    lock_acquire(&read_ahead_queue_lock);
    cond_signal(&is_empty, &read_ahead_queue_lock);
    lock_release(&read_ahead_queue_lock);
  }
}

static _Noreturn void thread_read_ahead(void *aux UNUSED) {
  while (true) {
    if (list_empty(&read_ahead_queue)) {
      ASSERT(list_size(&read_ahead_queue_not_empty_sema.waiters) == 0);

      lock_acquire(&read_ahead_queue_lock);
      cond_wait(&is_empty, &read_ahead_queue_lock);
      lock_release(&read_ahead_queue_lock);
    }

    struct list_elem *e = list_front(&read_ahead_queue);
    struct read_ahead_entry *entry = list_entry(e, struct read_ahead_entry,
                                                list_elem);

    // don't load it if we already have it
    d_printf("read-ahead %u\n", entry->sector);
    struct cache_entry *c_entry = cache_get_entry(entry->sector);
    ASSERT(c_entry != NULL);

    if(!c_entry->is_read_head) goto done;
    //start read
    cache_block_read_chunk_readahead(fs_device, entry->sector, NULL,
                                     BLOCK_SECTOR_SIZE, 0, true);

    //after read
    lock_acquire(&read_ahead_queue_lock);
    d_printf("545 broadcast %u\n", c_entry->sector);
    cond_broadcast(&c_entry->read_ahead_waiting, &read_ahead_queue_lock);
    lock_release(&read_ahead_queue_lock);

    done:

    list_pop_front(&read_ahead_queue);
    free(entry);
  }
}