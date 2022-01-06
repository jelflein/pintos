//
// Created by pintos on 19.12.21.
//

#include <stdint.h>

#ifndef PINTOS_SWAP_H
#define PINTOS_SWAP_H

void swap_init(void);
int frame_to_swap(void *addr);
void swap_to_frame(uint32_t slot, void *frame)

#endif //PINTOS_SWAP_H
