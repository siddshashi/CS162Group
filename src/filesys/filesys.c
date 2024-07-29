#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  /* Initialize buffer cache. */
  buffer_cache_init();

  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();

  /* Initialize root directory. */
  if (format) {
    dir_create(ROOT_DIR_SECTOR, 16);
    struct dir* root_dir = dir_open(inode_open(ROOT_DIR_SECTOR));
    inode_set_isdir(dir_get_inode(root_dir), true);
    dir_add(root_dir, ".", ROOT_DIR_SECTOR);
    dir_add(root_dir, "..", ROOT_DIR_SECTOR);
    dir_close(root_dir);
  }

  /* Open root directory as CWD for current working directory */
  struct process* pcb = thread_current()->pcb;
  pcb->cwd = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  buffer_cache_done();
  free_map_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  block_sector_t inode_sector = 0;

  /* Check invalid name. */
  if (name == NULL || name[0] == '\0')
    return false;

  struct dir* dir;
  bool absolute = name[0] == '/';
  char file_name[NAME_MAX + 1];
  int num_parts = dir_file_path_num_parts((char*)name);
  if (num_parts == -1) { /* Invalid file path. */
    return false;
  } else if (num_parts == 1) { /* Simple file path. */
    if (absolute)
      dir = dir_open_root();
    else
      dir = dir_reopen(thread_current()->pcb->cwd);
    char* name_cpy = (char*)name;
    dir_get_next_part(file_name, (const char**)&name_cpy);
  } else { /* Multi-part (nested directory) file path. */
    dir_split_file_path((char*)name, file_name);
    dir = dir_open(dir_resolve_path((char*)name));
  }

  /* Create operation. */
  bool success =
      (dir != NULL && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size) && dir_add(dir, file_name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* dir;
  bool absolute = name[0] == '/';
  char file_name[NAME_MAX + 1];

  int num_parts = dir_file_path_num_parts((char*)name);
  if (num_parts == 0) {
    if (!strcmp(name, "/"))
      return file_open(dir_resolve_path((char*)name));
    return NULL;
  } else if (num_parts == 1) { /* Simple file path. */
    if (absolute)
      dir = dir_open_root();
    else
      dir = dir_reopen(thread_current()->pcb->cwd);
    char* name_cpy = (char*)name;
    dir_get_next_part(file_name, (const char**)&name_cpy);
  } else { /* Multi-part (nested directory) file path. */
    dir_split_file_path((char*)name, file_name);
    dir = dir_open(dir_resolve_path((char*)name));
  }
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, file_name, &inode);
  dir_close(dir);

  return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
  struct dir* dir;
  bool absolute = name[0] == '/';
  char file_name[NAME_MAX + 1];
  int num_parts = dir_file_path_num_parts((char*)name);
  if (num_parts == 0) {
    return false;
  } else if (num_parts == 1) { /* Simple file path. */
    if (absolute)
      dir = dir_open_root();
    else
      dir = dir_reopen(thread_current()->pcb->cwd);
    char* name_cpy = (char*)name;
    dir_get_next_part(file_name, (const char**)&name_cpy);
  } else { /* Multi-part (nested directory) file path. */
    dir_split_file_path((char*)name, file_name);
    dir = dir_open(dir_resolve_path((char*)name));
  }

  /* Check if file being removed is directory. */
  struct inode* inode;
  if (dir_lookup(dir, file_name, &inode)) {
    if (inode_isdir(inode)) {
      if (inode_open_cnt(inode) != 1) { /* Directory has multiple opens, invalid remove. */
        inode_close(inode);
        dir_close(dir);
        return false;
      }

      /* Check directory is empty. */
      struct dir* remove_dir = dir_open(inode);
      char remove_dir_entry[NAME_MAX + 1];
      while (dir_readdir(remove_dir, remove_dir_entry)) {
        if (strcmp(remove_dir_entry, ".") != 0 &&
            strcmp(remove_dir_entry, "..") != 0) { /* Non-empty. */
          dir_close(remove_dir);
          dir_close(dir);
          return false;
        }
      }

      dir_close(remove_dir);
    } else {
      inode_close(inode);
    }
  }

  bool success = dir != NULL && dir_remove(dir, file_name);
  dir_close(dir);

  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}
