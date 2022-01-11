
#include <filesys/off_t.h>
#include <stdbool.h>

struct thread;
enum palloc_flags;


struct frame_entry {
    uint32_t page;
    struct thread *thread;
    bool pinned;
    uint8_t eviction_score;
};

void *
allocate_frame(struct thread *t, enum palloc_flags fgs, uint32_t page_addr);

void free_frame(void *page);

void frame_table_init(uint32_t num_user_frames, uint32_t num_total_frames);

void compute_eviction_score(void);

void frametable_lock(void);
void frametable_unlock(void);

void set_pinned(void* frame);