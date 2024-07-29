#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init(void);
bool inode_create(block_sector_t, off_t);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(const struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
off_t inode_length(const struct inode*);
void inode_set_isdir(struct inode* inode, bool value);
bool inode_isdir(struct inode* inode);
int inode_open_cnt(struct inode* inode);

/* Buffer cache. */
void buffer_cache_init(void);
void buffer_cache_done(void);
void buffer_cache_flush(void);
struct buffer_cache_entry* buffer_cache_acquire(block_sector_t block_id, bool write);
void buffer_cache_release(struct buffer_cache_entry* bce);
void buffer_cache_reset(void);
float buffer_cache_hit_rate(void);

#endif /* filesys/inode.h */
