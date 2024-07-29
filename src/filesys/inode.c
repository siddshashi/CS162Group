#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
#define INODE_NUM_DP 123
struct inode_disk {
  off_t length;                    /* File size in bytes. */
  uint32_t is_dir;                 /* Mark inode as directory. */
  block_sector_t dp[INODE_NUM_DP]; /* Direct pointers. */
  block_sector_t ip;               /* Indirect pointer. */
  block_sector_t dip;              /* Double indirect pointer. */
  unsigned magic;                  /* Magic number. */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* Resizes an inode file. */
static bool inode_file_resize(struct inode_disk* data, off_t size);

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct lock lock;      /* Synchronization lock. */
};

/* Buffer cache. */
struct buffer_cache_entry {
  uint8_t block[BLOCK_SECTOR_SIZE]; /* Cache block. */
  block_sector_t block_id;          /* Block index. */
  bool valid;                       /* Indicate if block is valid. */
  bool dirty;                       /* Indicate if block is dirty. */
  int ref_cnt;                      /* Serialize block access, number of current access to block. */
  struct condition cond;            /* Condition variable to serialize block access. */
  struct list_elem elem;            /* Element of available cache list. */
};
struct buffer_cache_entry buffer_cache[64]; /* Static memory allocation of buffer cache. */
struct lock buffer_cache_lock;              /* Synchronize updates to buffer cache. */
struct list available_cache;                /* List of available cache blocks. */
static int buffer_cache_access_cnt;         /* The number of times the buffer cache is accessed. */
static int buffer_cache_hit_cnt; /* The number of times a hit occurs in the buffer cache. */

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  int dp_index = pos / BLOCK_SECTOR_SIZE;
  if (dp_index < INODE_NUM_DP) { /* Block inside direct pointer. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(inode->sector, false);
    struct inode_disk* data = (struct inode_disk*)bce->block;
    if (data->dp[dp_index] != 0) {
      block_sector_t ret = data->dp[dp_index]; /* Access direct pointer. */
      buffer_cache_release(bce);
      return ret;
    }
    buffer_cache_release(bce);
  } else if (dp_index < INODE_NUM_DP + 128) { /* Block inside indirect pointer. */
    struct buffer_cache_entry* bce_data = buffer_cache_acquire(inode->sector, false);
    struct inode_disk* data = (struct inode_disk*)bce_data->block;
    block_sector_t data_ip = data->ip; /* Read pointer to indrect block. */
    buffer_cache_release(bce_data);

    if (data_ip != 0) {
      /* Read indrect block. */
      block_sector_t* bounce = malloc(BLOCK_SECTOR_SIZE);
      struct buffer_cache_entry* bce = buffer_cache_acquire(data_ip, false);
      memcpy(bounce, bce->block, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);

      /* Check direct pointers. */
      if (bounce[dp_index - INODE_NUM_DP] != 0) {
        block_sector_t block = bounce[dp_index - INODE_NUM_DP];
        free(bounce);
        return block;
      }
      free(bounce);
    }
  } else if (dp_index < INODE_NUM_DP + 128 + 128 * 128) { /* Block inside DIP. */
    block_sector_t* bounce_dip = malloc(BLOCK_SECTOR_SIZE);
    block_sector_t* bounce_ip = malloc(BLOCK_SECTOR_SIZE);

    /* Read doubly indirect pointer. */
    struct buffer_cache_entry* bce_data = buffer_cache_acquire(inode->sector, false);
    struct inode_disk* data = (struct inode_disk*)bce_data->block;
    block_sector_t data_dip = data->dip;
    buffer_cache_release(bce_data);

    /* Read doubly indirect block. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(data_dip, false);
    memcpy(bounce_dip, bce->block, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);

    /* Check indirect pointers. */
    int ip_index_bounce = (dp_index - INODE_NUM_DP - 128) / 128;
    if (bounce_dip[ip_index_bounce] != 0) {
      struct buffer_cache_entry* bce = buffer_cache_acquire(bounce_dip[ip_index_bounce], false);
      memcpy(bounce_ip, bce->block, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);

      /* Check direct pointers. */
      int dp_index_bounce = (dp_index - INODE_NUM_DP - 128) % 128;
      if (bounce_ip[dp_index_bounce] != 0) {
        block_sector_t block = bounce_ip[dp_index_bounce];
        free(bounce_ip);
        free(bounce_dip);
        return block;
      }
    }

    free(bounce_ip);
    free(bounce_dip);
  }

  /* Block not found. */
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;

    /* Allocate blocks for initial file size. */
    success = inode_file_resize(disk_inode, length);
    if (!success)
      inode_file_resize(disk_inode, 0);

    /* Write new inode disk to disk. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(sector, true);
    memcpy(bce->block, disk_inode, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);

    free(disk_inode);
  }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      lock_release(&open_inodes_lock);
      inode_reopen(inode);
      return inode;
    }
  }
  lock_release(&open_inodes_lock);

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);

  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Write inode disk back to block device. */

  /* Release resources if this was the last opener. */
  lock_acquire(&inode->lock);
  int open_cnt = --inode->open_cnt;
  lock_release(&inode->lock);

  if (open_cnt == 0) {
    /* Remove from inode list and release lock. */
    lock_acquire(&open_inodes_lock);
    list_remove(&inode->elem);
    lock_release(&open_inodes_lock);

    /* Deallocate blocks if removed. */
    lock_acquire(&inode->lock);
    bool removed = inode->removed;
    lock_release(&inode->lock);
    if (removed) {
      /* Get associated inode disk from block device. */
      struct inode_disk* data = malloc(sizeof(struct inode_disk));
      struct buffer_cache_entry* bce_data = buffer_cache_acquire(inode->sector, false);
      memcpy(data, bce_data->block, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce_data);

      /* Remove data blocks, pointer blocks, and inode disk block. */
      inode_file_resize(data, 0);
      free_map_release(inode->sector, 1);
    }

    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;

  /* Update read boundaries if reading beyond EOF. */
  off_t inode_data_length = inode_length(inode);
  if (offset > inode_data_length) /* Read beyond EOF. */
    return 0;
  else if (offset + size > inode_data_length) /* Partial read beyond EOF. */
    size = inode_data_length - offset;        /* Only read up to EOF. */

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      struct buffer_cache_entry* bce = buffer_cache_acquire(sector_idx, false);
      memcpy(buffer + bytes_read, bce->block, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);
    } else {
      struct buffer_cache_entry* bce = buffer_cache_acquire(sector_idx, false);
      memcpy(buffer + bytes_read, (uint8_t*)&bce->block[0] + sector_ofs, chunk_size);
      buffer_cache_release(bce);
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
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;

  /* Check if file is denied from writing. */
  lock_acquire(&inode->lock);
  if (inode->deny_write_cnt) {
    lock_release(&inode->lock);
    return 0;
  }

  /* Read inode_disk from block device. */
  struct inode_disk* data = malloc(sizeof(struct inode_disk));
  struct buffer_cache_entry* bce_data = buffer_cache_acquire(inode->sector, false);
  memcpy(data, bce_data->block, BLOCK_SECTOR_SIZE);
  buffer_cache_release(bce_data);

  /* Extend inode if write exceeds EOF. */
  if (offset + size > data->length) {
    if (!inode_file_resize(data, offset + size)) { /* File extension fails. */
      inode_file_resize(data, data->length);       /* Rollback file extension. */
      free(data);
      lock_release(&inode->lock);
      return 0;
    }

    /* Update inode disk block in buffer cache. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(inode->sector, true);
    memcpy(bce->block, data, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);
  }
  free(data);
  lock_release(&inode->lock);

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      struct buffer_cache_entry* bce = buffer_cache_acquire(sector_idx, true);
      memcpy(bce->block, buffer + bytes_written, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);
    } else {
      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      struct buffer_cache_entry* bce = buffer_cache_acquire(sector_idx, true);
      if (!(sector_ofs > 0 || chunk_size < sector_left))
        memset(bce->block, 0, BLOCK_SECTOR_SIZE);
      memcpy(&bce->block[0] + sector_ofs, buffer + bytes_written, chunk_size);
      buffer_cache_release(bce);
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
void inode_deny_write(struct inode* inode) {
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  lock_acquire(&inode->lock);
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  struct buffer_cache_entry* bce = buffer_cache_acquire(inode->sector, false);
  struct inode_disk* data = (struct inode_disk*)bce->block;
  off_t inode_data_length = data->length;
  buffer_cache_release(bce);
  return inode_data_length;
}

/* Resize an inode disk to SIZE bytes. Inode disk is not updated in block device.
    All other data/pointer blocks are updated in disk. If resize fails, inode disk file size
    is not updated. */
static bool inode_file_resize(struct inode_disk* data, off_t size) {
  /* Check resize requets is valid. */
  if (size < 0 || size > (INODE_NUM_DP + 128 + 128 * 128) * BLOCK_SECTOR_SIZE)
    return false;

  /* Iterate over direct pointers */
  block_sector_t* dp = data->dp;
  for (int i = 0; i < INODE_NUM_DP; i++) {
    if (size <= i * BLOCK_SECTOR_SIZE && dp[i] != 0) { /* Shrink file. */
      free_map_release(dp[i], 1);
      dp[i] = 0;
    } else if (size > i * BLOCK_SECTOR_SIZE && dp[i] == 0) { /* Grow file. */
      if (!free_map_allocate(1, &dp[i]))
        return false;
      struct buffer_cache_entry* bce = buffer_cache_acquire(dp[i], true);
      memset(bce->block, 0, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);
    }
  }

  /* Check indirect pointer */
  block_sector_t* ip = &data->ip;
  if (*ip == 0 && size <= INODE_NUM_DP * BLOCK_SECTOR_SIZE) {
    data->length = size;
    return true;
  }

  block_sector_t* bounce_ip = calloc(128, sizeof(block_sector_t));
  if (*ip == 0) { /* Indirect pointer unallocated. */
    free_map_allocate(1, ip);
  } else {
    /* Read indirect pointer block from disk. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(*ip, false);
    memcpy(bounce_ip, bce->block, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);
  }

  /* Iterate over direct pointers. */
  for (int i = 0; i < 128; i++) {
    if (size <= (INODE_NUM_DP + i) * BLOCK_SECTOR_SIZE && bounce_ip[i] != 0) { /* Shrink file. */
      free_map_release(bounce_ip[i], 1);
      bounce_ip[i] = 0;
    } else if (size > (INODE_NUM_DP + i) * BLOCK_SECTOR_SIZE &&
               bounce_ip[i] == 0) { /* Grow file. */
      if (!free_map_allocate(1, &bounce_ip[i])) {
        free(bounce_ip);
        return false;
      }
      /* Set block to zero. */
      struct buffer_cache_entry* bce = buffer_cache_acquire(bounce_ip[i], true);
      memset(bce->block, 0, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);
    }
  }

  struct buffer_cache_entry* bce = buffer_cache_acquire(*ip, true);
  memcpy(bce->block, bounce_ip, BLOCK_SECTOR_SIZE);
  buffer_cache_release(bce);

  free(bounce_ip);

  if (size <= INODE_NUM_DP * BLOCK_SECTOR_SIZE) { /* Unallocate indirect pointer if not needed. */
    free_map_release(*ip, 1);
    *ip = 0;
  }

  /* Return if DIP unallocated and not needed. */
  block_sector_t* dip = &data->dip;
  if (*dip == 0 && size <= (INODE_NUM_DP + 128) * BLOCK_SECTOR_SIZE) {
    data->length = size;
    return true;
  }

  /* Load DIP buffer. */
  block_sector_t* bounce_dip = calloc(128, sizeof(block_sector_t));
  if (*dip == 0) {
    free_map_allocate(1, dip);
  } else {
    /* Read DIP block from disk. */
    struct buffer_cache_entry* bce = buffer_cache_acquire(*dip, false);
    memcpy(bounce_dip, bce->block, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);
  }

  for (int i = 0; i < 128; i++) { /* Iterate over indirect pointers. */
    /* Break out of for-loop if IP bounce_dip[i] unallocated and not needed. */
    if (bounce_dip[i] == 0 && size <= (INODE_NUM_DP + 128 + 128 * i) * BLOCK_SECTOR_SIZE)
      break;

    bounce_ip = calloc(128, sizeof(block_sector_t));
    if (bounce_dip[i] == 0) { /* Indirect pointer unallocated. */
      free_map_allocate(1, &bounce_dip[i]);
    } else {
      /* Read indirect pointer block from disk. */
      struct buffer_cache_entry* bce = buffer_cache_acquire(bounce_dip[i], false);
      memcpy(bounce_ip, bce->block, BLOCK_SECTOR_SIZE);
      buffer_cache_release(bce);
    }

    for (int j = 0; j < 128; j++) { /* Iterate over direct pointers. */
      if (size <= (INODE_NUM_DP + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
          bounce_ip[j] != 0) { /* Shrink file. */
        free_map_release(bounce_ip[j], 1);
        bounce_ip[j] = 0;
      } else if (size > (INODE_NUM_DP + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
                 bounce_ip[j] == 0) { /* Grow file. */
        if (!free_map_allocate(1, &bounce_ip[j])) {
          free(bounce_dip);
          free(bounce_ip);
          return false;
        }
        /* Set data block to zero. */
        struct buffer_cache_entry* bce = buffer_cache_acquire(bounce_ip[j], true);
        memset(bce->block, 0, BLOCK_SECTOR_SIZE);
        buffer_cache_release(bce);
      }
    }

    /* Write indirect block to disk (through buffer cache). */
    struct buffer_cache_entry* bce = buffer_cache_acquire(bounce_dip[i], true);
    memcpy(bce->block, bounce_ip, BLOCK_SECTOR_SIZE);
    buffer_cache_release(bce);
    free(bounce_ip);

    if (size <= (INODE_NUM_DP + 128 + 128 * i) * BLOCK_SECTOR_SIZE) {
      /* Unallocate IP block. */
      free_map_release(bounce_dip[i], 1);
      bounce_dip[i] = 0;
    }
  }

  /* Write DIP block to disk (through buffer cache). */
  bce = buffer_cache_acquire(*dip, true);
  memcpy(bce->block, bounce_dip, BLOCK_SECTOR_SIZE);
  buffer_cache_release(bce);
  free(bounce_dip);

  if (size <= (INODE_NUM_DP + 128) * BLOCK_SECTOR_SIZE) {
    /* Unallocate DIP block. */
    free_map_release(*dip, 1);
    *dip = 0;
  }

  data->length = size;
  return true;
}

void inode_set_isdir(struct inode* inode, bool value) {
  struct buffer_cache_entry* bce = buffer_cache_acquire(inode->sector, true);
  struct inode_disk* data = (struct inode_disk*)bce->block;
  data->is_dir = value;
  buffer_cache_release(bce);
}

bool inode_isdir(struct inode* inode) {
  struct buffer_cache_entry* bce = buffer_cache_acquire(inode->sector, false);
  struct inode_disk* data = (struct inode_disk*)bce->block;
  bool ret = (bool)data->is_dir;
  buffer_cache_release(bce);
  return ret;
}

int inode_open_cnt(struct inode* inode) {
  lock_acquire(&inode->lock);
  int open_cnt = inode->open_cnt;
  lock_release(&inode->lock);
  return open_cnt;
}

/* Initialize buffer cache. */
void buffer_cache_init(void) {
  list_init(&available_cache);
  lock_init(&buffer_cache_lock);
  for (int i = 0; i < 64; i++) {
    buffer_cache[i].valid = false;
    cond_init(&buffer_cache[i].cond);
    list_push_back(&available_cache, &buffer_cache[i].elem);
  }
  buffer_cache_access_cnt = 0;
  buffer_cache_hit_cnt = 0;
}

void buffer_cache_done(void) { buffer_cache_flush(); }

/* Flush dirty blocks in buffer cache to disk. */
void buffer_cache_flush(void) {
  for (struct list_elem* e = list_begin(&available_cache); e != list_end(&available_cache);
       e = list_next(e)) {
    struct buffer_cache_entry* bce = list_entry(e, struct buffer_cache_entry, elem);
    if (bce->valid && bce->dirty) {
      block_write(fs_device, bce->block_id, bce->block);
      bce->dirty = false;
    }
  }
}

struct buffer_cache_entry* buffer_cache_acquire(block_sector_t block_id, bool write) {
  lock_acquire(&buffer_cache_lock);
  buffer_cache_access_cnt += 1;

  /* Search for BCE in cache. */
  struct buffer_cache_entry* bce = NULL;
  for (struct list_elem* e = list_begin(&available_cache); e != list_end(&available_cache);
       e = list_next(e)) {
    struct buffer_cache_entry* available_bce = list_entry(e, struct buffer_cache_entry, elem);
    if (available_bce->valid && available_bce->block_id == block_id) {
      bce = available_bce;
      break;
    }
  }

  if (!bce) { /* Evict cache block. */
    /* Get LRU buffer cache entry. */
    struct list_elem* e = list_pop_back(&available_cache);
    bce = list_entry(e, struct buffer_cache_entry, elem);

    /* Write dirty block to disk. */
    if (bce->valid && bce->dirty)
      block_write(fs_device, bce->block_id, bce->block);

    /* Initialize new buffer cache entry. */
    block_read(fs_device, block_id, bce->block);
    bce->block_id = block_id;
    bce->valid = true;
    bce->dirty = false;
    bce->ref_cnt = 0;
  } else { /* Cache entry found. */
    buffer_cache_hit_cnt += 1;
    while (bce->ref_cnt > 0)
      cond_wait(&bce->cond, &buffer_cache_lock);
    list_remove(&bce->elem);
  }

  /* LRU scheme: push block to front of available cache list on access. */
  list_push_front(&available_cache, &bce->elem);

  /* Mark block as dirty on write operation. */
  if (write)
    bce->dirty = true;

  bce->ref_cnt += 1;
  lock_release(&buffer_cache_lock);
  return bce;
}

void buffer_cache_release(struct buffer_cache_entry* bce) {
  lock_acquire(&buffer_cache_lock);
  bce->ref_cnt -= 1;
  cond_signal(&bce->cond, &buffer_cache_lock);
  lock_release(&buffer_cache_lock);
}

void buffer_cache_reset(void) {
  lock_acquire(&buffer_cache_lock);
  buffer_cache_flush();
  buffer_cache_access_cnt = 0;
  buffer_cache_hit_cnt = 0;
  for (int i = 0; i < 64; i++) {
    buffer_cache[i].valid = false;
  }
  lock_release(&buffer_cache_lock);
}

float buffer_cache_hit_rate(void) {
  lock_acquire(&buffer_cache_lock);
  float hit_rate = (float)buffer_cache_hit_cnt / buffer_cache_access_cnt;
  lock_release(&buffer_cache_lock);
  return hit_rate;
}