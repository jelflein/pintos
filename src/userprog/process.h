#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute(const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void init_segement_lock();

NO_RETURN void
process_terminate(struct thread *t, int status_code, const char *cmd_line);

struct shared_segment {
  struct list_elem list_elem;
  char program_path[128];
  int use_counter;
  uint32_t page;
  uint32_t frame;
  bool framed;
};

#endif /* userprog/process.h */
