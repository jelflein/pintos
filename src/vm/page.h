//
// Created by pintos on 15.12.21.
//

#ifndef PINTOS_PAGE_H
#define PINTOS_PAGE_H

#endif //PINTOS_PAGE_H


#include <stdint.h>
#include <stddef.h>
#include "../lib/user/syscall.h"
#include "../lib/kernel/hash.h"


struct file;

enum spe_status {
    zeroes,
    code_page_thrown_out,
    swap,
    frame,
    mapped_file
};

struct spt_entry {
    enum spe_status spe_status;

    uint32_t vaddr;
    pid_t pid;

    struct file *file;

    size_t file_offset;

    size_t  read_bytes;
    size_t  zero_bytes;

    bool writable;

    struct hash_elem elem;
};

void spt_init(void);

bool spt_entry(uint32_t vaddr, pid_t pid, uint32_t paddr, bool writable, enum
        spe_status spe_status);

bool spt_entry_empty(uint32_t vaddr, pid_t pid, bool writable, enum spe_status
spe_status);

bool spt_entry_mapped_file(uint32_t vaddr, pid_t pid,
                           bool writable, struct file *mapped_f,
                           size_t file_offset, size_t file_read_size);


struct spt_entry *spt_get_entry(uint32_t vaddr, pid_t pid);