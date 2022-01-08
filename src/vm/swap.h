//
// Created by pintos on 19.12.21.
//

#include "stddef.h"
#include "stdbool.h"
#include <stdint.h>

#ifndef PINTOS_SWAP_H
#define PINTOS_SWAP_H

void swap_init(void);
void set_swap_index(size_t slot, bool value);
size_t frame_to_swap(void *addr);
void swap_to_frame(uint32_t slot, void *frame);

#endif //PINTOS_SWAP_H
