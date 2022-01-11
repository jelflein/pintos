//
// Created by pintos on 15.12.21.
//

#ifndef PINTOS_PAGE_H
#define PINTOS_PAGE_H

#endif //PINTOS_PAGE_H


#include <stdint.h>
#include <stddef.h>
#include <filesys/off_t.h>
#include "../lib/user/syscall.h"
#include "../lib/kernel/hash.h"


struct file;
struct thread;

enum spe_status {
    zeroes,
    frame_from_file,
    swap,
    frame,
    mapped_file,
    mapped_file_nowriteback,
};

struct spt_entry {
    enum spe_status spe_status;

    uint32_t vaddr;
    pid_t pid;

    struct file *file;

    size_t file_offset;

    size_t  read_bytes;

    size_t swap_slot;

    bool writable;

    struct hash_elem elem;
};

struct m_file {
    int id;
    struct list_elem list_elem;
    struct file *file;
    uint32_t vaddr;
};

void spt_init(struct hash *spt);

bool spt_entry(struct thread *t, uint32_t vaddr, pid_t pid, uint32_t frame_addr,
               bool writable, enum
                       spe_status spe_status);

bool spt_entry_empty(uint32_t vaddr, pid_t pid, bool writable, enum spe_status
spe_status);

bool spt_entry_mapped_file(uint32_t vaddr, pid_t pid,
                           bool writable, struct file *mapped_f,
                           size_t file_offset, size_t file_read_size, bool write_back_to_file);


struct spt_entry *spt_get_entry(struct thread *t, uint32_t vaddr, pid_t pid);

bool spt_file_overlaping(uint32_t addr, off_t file_size, pid_t pid);

void spt_remove_entry(uint32_t vaddr, struct thread *t);

void spt_destroy(struct hash *);