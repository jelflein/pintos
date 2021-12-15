//
// Created by pintos on 15.12.21.
//

#ifndef PINTOS_PAGE_H
#define PINTOS_PAGE_H

#endif //PINTOS_PAGE_H


#include <stdint.h>

void install_page(int *table, uint32_t u_page);
void init_dir();
void init_table(int *dir);