#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute(const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

NO_RETURN void
process_terminate(struct thread *t, int status_code, const char *cmd_line);

#endif /* userprog/process.h */
