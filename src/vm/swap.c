//
// Created by pintos on 19.12.21.
//

#include "swap.h"
#include <lib/kernel/bitmap.h>
#include <devices/block.h>
#include <threads/vaddr.h>

#define SECTORS_PER_SLOT (PGSIZE / BLOCK_SECTOR_SIZE)

// maybe use bitmap structure to keep track of used and free swap slots
//true == swap slot occupied
struct bitmap *slots_occupied;
struct block *device;

void swap_init() {
  device = block_get_role(BLOCK_SWAP);
  ASSERT(device != NULL);

  uint32_t swap_size = block_size(device) * BLOCK_SECTOR_SIZE;
  uint32_t num_slots = swap_size / PGSIZE;
  slots_occupied = bitmap_create(num_slots);
}

void set_swap_index(size_t slot, bool value)
{
  bitmap_set(slots_occupied, slot, value);
}

size_t frame_to_swap(void *addr) {
  size_t free_slot = bitmap_scan(slots_occupied, 0, 1, false);
  if (free_slot == BITMAP_ERROR)
  {
    // TODO: Handle this error
    return -1;
  }

  // write out every sector that makes up this slot
  for (uint32_t i = 0; i < SECTORS_PER_SLOT; i++) {
    block_sector_t sector_number = (free_slot * SECTORS_PER_SLOT) + i;
    void *target = addr + (i * BLOCK_SECTOR_SIZE);

    block_write(device, sector_number, target);
  }

  bitmap_set(slots_occupied, free_slot, true);

  return free_slot;
}

void swap_to_frame(uint32_t slot, void *frame) {
  ASSERT(bitmap_test(slots_occupied, slot) == true)

  // write out every sector that makes up this slot
  for (uint32_t i = 0; i < SECTORS_PER_SLOT; i++) {
    block_sector_t sector_number = (slot * SECTORS_PER_SLOT) + i;
    void *target = frame + (i * BLOCK_SECTOR_SIZE);

    block_read(device, sector_number, target);
  }

  bitmap_set(slots_occupied, slot, false);
}