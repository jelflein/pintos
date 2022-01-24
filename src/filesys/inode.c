#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_MAGIC_DIRECTORY 0x494e4f43

#define MAX_DOUBLE_INDIRECT_TABLES 128
#define NUM_POINTERS_PER_TABLE 128
#define NUM_DIRECT_POINTERS 124
#define DIRECT_LIMIT (NUM_DIRECT_POINTERS - 1)
#define INDIRECT_LIMIT (NUM_POINTERS_PER_TABLE + DIRECT_LIMIT)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[NUM_DIRECT_POINTERS];
    block_sector_t indirect;
    block_sector_t doubleindirect;
  };

struct inode_disk_pointer_table
{
    block_sector_t pointers[NUM_POINTERS_PER_TABLE];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };


static bool inode_disk_extend(struct inode_disk *disk_inode, uint32_t new_size);


bool inode_is_directory(struct inode *i)
{
  ASSERT(i->data.magic == INODE_MAGIC || i->data.magic ==
  INODE_MAGIC_DIRECTORY);
  return i->data.magic == INODE_MAGIC_DIRECTORY;
}

block_sector_t inode_get_sector(struct inode *i)
{
  ASSERT(i->data.magic == INODE_MAGIC || i->data.magic ==
                                         INODE_MAGIC_DIRECTORY);
  return i->sector;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < 0 || pos >= inode->data.length)
    return UINT32_MAX;

  uint32_t sector_idx = pos / BLOCK_SECTOR_SIZE;
  if (sector_idx < NUM_DIRECT_POINTERS)
  {
    return inode->data.direct[sector_idx];
  }
  else if (sector_idx <= INDIRECT_LIMIT)
  {
    uint32_t indirect_table_idx = sector_idx - NUM_DIRECT_POINTERS;
    block_sector_t indirect_sector;
    // load entry from indirect table
    cache_block_read_chunk(fs_device, inode->data.indirect, &indirect_sector,
                           sizeof(block_sector_t),
                           sizeof(block_sector_t) * indirect_table_idx);
    return indirect_sector;
  }
  else
  {
    uint32_t table_of_tables_idx = (sector_idx - NUM_DIRECT_POINTERS -
            NUM_POINTERS_PER_TABLE) / NUM_POINTERS_PER_TABLE;
    uint32_t table_idx = (sector_idx - NUM_DIRECT_POINTERS -
                          NUM_POINTERS_PER_TABLE) % NUM_POINTERS_PER_TABLE;
    // load table of tables entry
    block_sector_t table_of_tables_entry;
    cache_block_read_chunk(fs_device, inode->data.doubleindirect, &table_of_tables_entry,
                           sizeof(block_sector_t),
                           sizeof(block_sector_t) * table_of_tables_idx);
    // load table entry
    block_sector_t table_entry;
    cache_block_read_chunk(fs_device, table_of_tables_entry, &table_entry,
                           sizeof(block_sector_t),
                           sizeof(block_sector_t) * table_idx);

    return table_entry;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  ASSERT(sizeof(struct inode_disk_pointer_table) == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = 0;
    disk_inode->magic = INODE_MAGIC;

    success = true;
    if (length > 0)
      success = inode_disk_extend(disk_inode, length);
    // write inode to disk
    cache_block_write (fs_device, sector, disk_inode);

    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_block_read (fs_device, inode->sector, &inode->data);
  ASSERT(inode != NULL);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed)
    {
      // delete all data sectors, indirect table and
      // table of tables and indirect tables
      for (uint32_t i = 0; i < NUM_DIRECT_POINTERS; i++)
      {
        if (inode->data.direct[i] != 0)
          free_map_release (inode->data.direct[i], 1);
      }

      if (inode->data.indirect != 0)
      {
        struct inode_disk_pointer_table indirect_table;
        // load indirect table
        cache_block_read(fs_device, inode->data.indirect, &indirect_table);
        for (uint32_t i = 0; i < NUM_POINTERS_PER_TABLE; i++)
        {
          // delete entry
          if (indirect_table.pointers[i] != 0)
            free_map_release (indirect_table.pointers[i], 1);
        }

        // delete indirect table
        free_map_release (inode->data.indirect, 1);
      }

      if (inode->data.doubleindirect != 0)
      {
        struct inode_disk_pointer_table table_of_tables;
        // load table of tables
        cache_block_read(fs_device, inode->data.indirect, &table_of_tables);
        for (uint32_t i = 0; i < NUM_POINTERS_PER_TABLE; i++)
        {
          if (table_of_tables.pointers[i] == 0) break;
          // load table
          struct inode_disk_pointer_table table;
          cache_block_read(fs_device, table_of_tables.pointers[i], &table);
          // delete every entry from the table
          for (uint32_t j = 0; j < NUM_POINTERS_PER_TABLE; j++)
          {
            // delete entry
            if (table.pointers[i] != 0)
              free_map_release (table.pointers[i], 1);
          }
          // delete table
          free_map_release (table_of_tables.pointers[i], 1);
        }

        // delete table of tables
        free_map_release (inode->data.doubleindirect, 1);
      }

      // delete inode_data
      free_map_release (inode->sector, 1);
    }

    free (inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
  {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
    {
      /* Read full sector directly into caller's buffer. */
      cache_block_read (fs_device, sector_idx, buffer + bytes_read);
    }
    else
    {
      cache_block_read_chunk(fs_device, sector_idx,
         buffer + bytes_read, chunk_size, sector_ofs);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      ASSERT(sector_idx != UINT32_MAX);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          cache_block_write_chunk(fs_device, sector_idx,
                                  buffer + bytes_written, chunk_size,
                                  sector_ofs);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

static bool inode_disk_extend(struct inode_disk *disk_inode, uint32_t new_size)
{
  ASSERT(disk_inode != NULL);
  ASSERT(new_size > (uint32_t)disk_inode->length);

  size_t new_sector_num = bytes_to_sectors ((int32_t)new_size);
  size_t old_sector_num = bytes_to_sectors (disk_inode->length);

  disk_inode->length = (int32_t)new_size;
  // write data blocks to disk
  static char zeros[BLOCK_SECTOR_SIZE];

  struct inode_disk_pointer_table *indirect_table = NULL;

  struct inode_disk_pointer_table
          *double_indirect_tables[MAX_DOUBLE_INDIRECT_TABLES] = {0};
  struct inode_disk_pointer_table *double_indirect_table_of_tables = NULL;
  bool has_modified_double_indirect_tables = false;

  for (uint32_t i = old_sector_num; i < new_sector_num; i++)
  {
    block_sector_t current_sector;
    if (!free_map_allocate (1, &current_sector))
    {
      ASSERT(0);
    }
    // write data
    cache_block_write (fs_device, current_sector, zeros);

    // write tables and references
    if (i < NUM_DIRECT_POINTERS)
    {
      ASSERT(disk_inode->direct[i] == 0);
      disk_inode->direct[i] = current_sector;
    }
    else if (i <= INDIRECT_LIMIT)
    {
      if (indirect_table == NULL)
      {
        indirect_table = malloc(sizeof(struct inode_disk_pointer_table));
        ASSERT(indirect_table != NULL);
        // check if we already have table on disk
        if (disk_inode->indirect != 0)
        {
          cache_block_read(fs_device, disk_inode->indirect, indirect_table);
        }
      }
      indirect_table->pointers[i - NUM_DIRECT_POINTERS] = current_sector;
    }
    else
    {
      uint32_t table_id = (i - INDIRECT_LIMIT - 1) / NUM_POINTERS_PER_TABLE;
      uint32_t in_table_index = (i - INDIRECT_LIMIT - 1) %
                                NUM_POINTERS_PER_TABLE;

      has_modified_double_indirect_tables = true;

      // check if we already have table of tables on disk
      if (double_indirect_table_of_tables == NULL &&
      disk_inode->doubleindirect != 0)
      {
        double_indirect_table_of_tables = malloc(sizeof(struct inode_disk_pointer_table));
        ASSERT(double_indirect_table_of_tables != NULL);
        cache_block_read(fs_device, disk_inode->doubleindirect,
                         double_indirect_table_of_tables);
      }

      if (double_indirect_tables[table_id] == NULL)
      {
        double_indirect_tables[table_id] = malloc(sizeof(struct inode_disk_pointer_table));
        ASSERT(double_indirect_tables[table_id] != NULL);
        // check if we already have this table on disk
        if (double_indirect_table_of_tables != NULL &&
        double_indirect_table_of_tables->pointers[table_id] != 0)
        {
          cache_block_read(fs_device, double_indirect_table_of_tables->pointers[table_id],
                           double_indirect_tables[table_id]);
        }
      }

      double_indirect_tables[table_id]->pointers[in_table_index] =
              current_sector;
    }
  }


  // write all kinds of tables to disk

  if (has_modified_double_indirect_tables && double_indirect_table_of_tables == NULL)
  {
    double_indirect_table_of_tables = malloc(sizeof(struct inode_disk_pointer_table));
    ASSERT(double_indirect_table_of_tables != NULL);
  }

  // write double indirect tables to disk
  for (uint32_t idx = 0; idx < (sizeof(double_indirect_tables)) / (sizeof(
  double_indirect_tables[0])); idx++)
  {
    if (double_indirect_tables[idx] == NULL) continue;

    block_sector_t double_indirect_table_sector = 0;
    if (double_indirect_table_of_tables != NULL)
      double_indirect_table_sector =
              double_indirect_table_of_tables->pointers[idx];

    if (double_indirect_table_sector == 0
      && !free_map_allocate (1, &double_indirect_table_sector))
    {
      ASSERT(0);
    }
    cache_block_write (fs_device, double_indirect_table_sector,
                       double_indirect_tables[idx]);
    double_indirect_table_of_tables->pointers[idx] =
            double_indirect_table_sector;
  }

  // write table of tables to disk
  if (has_modified_double_indirect_tables)
  {
    block_sector_t double_indirect_table_of_tables_sector = disk_inode->doubleindirect;
    if (double_indirect_table_of_tables_sector == 0
    && !free_map_allocate(1, &double_indirect_table_of_tables_sector))
    {
      ASSERT(0);
    }
    cache_block_write(fs_device, double_indirect_table_of_tables_sector,
                      double_indirect_table_of_tables);
    disk_inode->doubleindirect = double_indirect_table_of_tables_sector;
  }

  // write indirect table
  if (indirect_table != NULL)
  {
    block_sector_t indirect_table_sector = disk_inode->indirect;
    if (indirect_table_sector == 0
    && !free_map_allocate (1, &indirect_table_sector))
    {
      ASSERT(0);
    }
    cache_block_write (fs_device, indirect_table_sector, indirect_table);
    disk_inode->indirect = indirect_table_sector;
  }

  // writing inode to disk is left to the caller

  return true;
}

bool inode_extend(struct inode *i, uint32_t new_size)
{
  bool success = inode_disk_extend(&i->data, new_size);

  cache_block_write (fs_device, i->sector, &i->data);

  return success;
}