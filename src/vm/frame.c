#include "frame.h"
#include "../threads/palloc.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../threads/loader.h"
#include "../threads/vaddr.h"
#include "../lib/debug.h"

uint32_t *frame_table = NULL;
uint32_t num_frames = 0;

bool entry_is_empty(uint32_t entry)
{
  return (entry & 0xFFFFF000) == 0;
}

void *entry_get_page(uint32_t entry)
{
  return (void *)(entry & 0xFFFFF000);
}

uint32_t entry_create(void *page)
{
  return ((uint32_t)page & 0xFFFFF000); // add additional bits later
}


void *allocate_frame(struct thread *t, enum palloc_flags fgs)
{
  void *u_frame = palloc_get_page(PAL_USER | fgs);

  ASSERT(u_frame != NULL)

  //printf("vtop(u_frame) >> 12 = %u\n" , vtop(u_frame) >> 12);

  // TODO: use page address here
  frame_table[vtop(u_frame) >> 12] = entry_create(u_frame);

  return u_frame;
}

void free_frame(void *page)
{
  void *u_frame = page; // simplification for now
  frame_table[vtop(u_frame) >> 12] = 0;

  palloc_free_page(page);
}


uint32_t divide_round_up(uint32_t a, uint32_t b)
{
  return (a + b - 1) / b;
}

void frame_table_init()
{
  //Too much memory exclud. k_pages
  num_frames = init_ram_pages;
  uint32_t table_size_bytes = num_frames * 4;

  frame_table = palloc_get_multiple(PAL_ZERO | PAL_ASSERT,
                                    divide_round_up(table_size_bytes, PGSIZE));


}