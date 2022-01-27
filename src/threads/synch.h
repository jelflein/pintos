#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore 
  {
    unsigned value;             /* Current value. */
    struct list waiters;        /* List of waiting threads. */
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_down_push_front (struct semaphore *sema);
void sema_self_test (void);

/* Lock. */
struct lock 
  {
    struct thread *holder;      /* Thread holding lock (for debugging). */
    struct semaphore semaphore; /* Binary semaphore controlling access. */
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition 
  {
    struct list waiters;        /* List of waiting threads. */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);


struct read_writer_lock
{
  bool active_atomic;
  bool is_atomic;

  uint32_t active_readers;
  uint32_t active_writers;

  struct lock mutex;

  struct condition waiting_readers;
  struct condition waiting_writers;
  struct condition waiting_atomic;
};

void acquire_read(struct read_writer_lock *read_writer_lock);
void acquire_write(struct read_writer_lock *read_writer_lock);
void acquire_atomic(struct read_writer_lock *read_writer_lock);

void release_read(struct read_writer_lock *read_writer_lock);
void release_write(struct read_writer_lock *read_writer_lock);
void release_atomic(struct read_writer_lock *read_writer_lock);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
