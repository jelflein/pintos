#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H


struct thread;
struct m_file;

void syscall_init (void);

void close_mfile(struct thread *t, struct m_file *m_file);

void fs_unlock();
void fs_lock();

#endif /* userprog/syscall.h */
