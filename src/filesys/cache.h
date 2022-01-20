//
// Created by pintos on 19.01.22.
//

#include <devices/block.h>
#include <hash.h>

#ifndef PINTOS_CACHE_H
#define PINTOS_CACHE_H

#endif //PINTOS_CACHE_H

struct cache_entry {
    block_sector_t sector;
    struct hash_elem elem;

    bool dirty;
    bool accessed;

    bool pinned;

    uint32_t lru_timestamp;

    uint8_t data[BLOCK_SECTOR_SIZE];
};

void init_cache(void);
void cache_shutdown(void);

void
cache_block_read (struct block *block, block_sector_t sector, void *buffer);

void
cache_block_write (struct block *block, block_sector_t sector, const void
*buffer);
