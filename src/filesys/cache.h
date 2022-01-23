//
// Created by pintos on 19.01.22.
//

#include <devices/block.h>
#include <hash.h>
#include "../threads/synch.h"

#ifndef PINTOS_CACHE_H
#define PINTOS_CACHE_H

#endif //PINTOS_CACHE_H

struct cache_entry {
    block_sector_t sector;
    struct hash_elem elem;

    bool dirty;
    bool accessed;

    bool is_evcting;

    bool is_read_head;
    struct condition read_ahead_waiting;

    uint32_t pinned;

    uint32_t lru_timestamp;

    struct lock lock;

    uint8_t data[BLOCK_SECTOR_SIZE];
};

void init_cache(void);
void cache_shutdown(void);

void
cache_block_read (struct block *block, block_sector_t sector, void *buffer);

void
cache_block_read_chunk(struct block *block, block_sector_t sector, void
        *buffer, uint32_t chunk_size, uint32_t sector_ofs);

void
cache_block_write (struct block *block, block_sector_t sector, const void
*buffer);

void
cache_block_write_chunk (struct block *block, block_sector_t sector, const
        void *buffer, uint32_t chunk_size, uint32_t sector_ofs);