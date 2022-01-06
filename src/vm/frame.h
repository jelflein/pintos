
#include <filesys/off_t.h>

struct thread;
enum palloc_flags;


void *allocate_frame(struct thread *t, enum palloc_flags fgs);

void free_frame(void *page);

void frame_table_init(uint32_t num_user_pages);