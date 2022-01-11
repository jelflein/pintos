#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

struct thread;
struct m_file;

void syscall_init (void);

void close_mfile(struct thread *t, struct m_file *m_file);

void fs_unlock(void);
void fs_lock(void);

bool fs_lock_held_by_current_thread(void);

#endif /* userprog/syscall.h */
