#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <threads/thread.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  bool is_dir;
  char *last_component;
  struct dir *dir = traverse_path(name, &is_dir, true, &last_component, NULL);
  if (!is_dir || dir == NULL) return false;
  block_sector_t inode_sector = 0;

  d_printf("creating file %s\n", last_component);

  bool success = (free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, last_component, inode_sector));

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create_dir (const char *name)
{
  bool is_dir;
  char *last_component;
  struct dir *dir = traverse_path(name, &is_dir, true, &last_component, NULL);
  if (!is_dir || dir == NULL) return false;
  block_sector_t inode_sector = 0;

  d_printf("creating directory %s\n", last_component);

  bool success = (free_map_allocate (1, &inode_sector)
                  && dir_create(inode_sector, 16)
                  && dir_add (dir, last_component, inode_sector));

  if (success)
  {
    // add reference to parent directory
    struct inode *new_dir_inode = inode_open(inode_sector);
    struct dir *new_dir = dir_open(new_dir_inode);
    success &= dir_add(new_dir, "..", inode_get_sector(dir_get_inode(dir)));
    dir_close(new_dir);
  }

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void *
filesys_open (const char *name, bool *is_dir)
{
  bool path_is_dir;
  void *file_or_dir = traverse_path(name, &path_is_dir, false, NULL, NULL);
  if (file_or_dir == NULL) return false;

  *is_dir = path_is_dir;

  return file_or_dir;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool path_is_dir;
  struct dir *containing_dir = NULL;
  char *last_component;
  void *file_or_dir = traverse_path(name, &path_is_dir, false, &last_component,
                                    &containing_dir);
  if (file_or_dir == NULL) return false;

  bool success = false;

  if (path_is_dir) {
    struct dir *d = (struct dir *) file_or_dir;

    // check if dir not empty
    if (!dir_is_empty(d))
    {
      dir_close(d);
      goto done;
    }

    // check if attempting to delete the root dir
    struct dir *root_dir = dir_open_root();
    if (dir_get_inode(root_dir) == dir_get_inode(d))
    {
      dir_close(root_dir);
      dir_close(d);
      success = false;
      goto done;
    }
    dir_close(root_dir);

    // check if deleting current working directory
    if (dir_get_inode(thread_current()->working_directory) == dir_get_inode(
            d))
    {
      thread_current()->working_directory = NULL;
      thread_current()->working_directory_deleted = true;
    }
    dir_close(d);
  }
  // path points to file
  else {
    file_close((struct file *) file_or_dir);
  }

  ASSERT(containing_dir != NULL);
  // remove the entry from the containing directory
  // this will also erase the file itself
  success = dir_remove(containing_dir, last_component);

  done:
  dir_close (containing_dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  // add reference to "parent" directory
  struct inode *new_dir_inode = inode_open(ROOT_DIR_SECTOR);
  struct dir *new_dir = dir_open(new_dir_inode);
  if (!dir_add(new_dir, "..", inode_get_sector(new_dir_inode)))
    PANIC("Failed to add parent dir to root dir");

  dir_close(new_dir);

  free_map_close ();
  printf ("done.\n");
}

void *traverse_path(char *path, bool *is_dir, bool last_component_must_be_null,
                    char **last_component, struct dir **containing_dir)
{
  uint32_t path_length = strlen(path);
  if (path_length == 0) return NULL;

  bool should_end_with_dir = path[path_length] == '/';
  bool last_component_does_not_exist = false;

  bool current_is_dir = true;
  struct file *current_file = NULL;
  struct dir *current_dir = NULL;

  struct dir *previous_dir = NULL;

  struct thread *t = thread_current();
  if (path[0] == '/')
    current_dir = dir_open_root();
  else
  {
    // relative path
    if (t->working_directory_deleted) goto fail;
    current_dir = t->working_directory ?
                  dir_reopen(t->working_directory) : dir_open_root();
  }

  ASSERT(current_dir != NULL);
  char *token, *save_ptr;
  for (token = strtok_r (path, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
  {
    if (last_component_does_not_exist) goto fail;

    d_printf ("traversing '%s'\n", token);


    // check for . special case no-op
    if (strcmp(token, ".") == 0)
    {
      if (!current_is_dir) goto fail;
      continue;
    }

    // if we encountered a file last, abort
    if (!current_is_dir) goto fail;

    if (last_component) *last_component = token;

    // traverse next directory
    struct inode *inode;
    if (dir_lookup(current_dir, token, &inode))
    {
      current_is_dir = inode_is_directory(inode);
      if (current_is_dir)
      {
        if (current_file) file_close(current_file);
        current_file = NULL;
        if (current_dir) {
          if (previous_dir) dir_close(previous_dir);
          previous_dir = current_dir;
        }
        current_dir = dir_open(inode);
      }
      else
      {
        if (current_file) file_close(current_file);
        current_file = file_open(inode);
        if (current_dir) {
          if (previous_dir) dir_close(previous_dir);
          previous_dir = current_dir;
        }
        current_dir = NULL;
      }
    }
    // file "token" does not exist
    else
    {
      last_component_does_not_exist = true;
      if (!last_component_must_be_null)
      {
        goto fail;
      }
    }
  }

  if (should_end_with_dir && last_component_does_not_exist) goto fail;

  if (should_end_with_dir && !current_is_dir) goto fail;

  if (last_component_must_be_null && !last_component_does_not_exist) goto fail;

  *is_dir = current_is_dir;
  if (containing_dir) *containing_dir = previous_dir;
  else if (previous_dir) dir_close(previous_dir);
  return current_is_dir ? (void*)current_dir : (void*)current_file;

  fail:
  if (current_file) file_close(current_file);
  current_file = NULL;
  if (current_dir) dir_close(current_dir);
  current_dir = NULL;
  return NULL;
}