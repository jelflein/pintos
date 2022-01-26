#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "file.h"
#include <list.h>


/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
struct block *fs_device;
struct dir;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
bool filesys_create_dir (const char *name);
void *filesys_open (const char *name, bool *is_dir);
bool filesys_remove (const char *name);
void *traverse_path(char *path, bool *is_dir, bool last_component_must_be_null,
                    char **last_component, struct dir **containing_dir);

struct file_descriptor {
  int descriptor_id;
  union {
      struct file *f;
      struct dir *d;
  };
  bool is_directory;
  struct list_elem list_elem;
};

#endif /* filesys/filesys.h */
