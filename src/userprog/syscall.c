#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include <float.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "process.h"
#include "devices/input.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"

static void syscall_handler(struct intr_frame*);

// Memory validation
static bool valid_byte_pointer(uint8_t* p);
static bool valid_pointer(uint8_t* p, size_t size);
static char* valid_str_pointer(char* s);

// General syscall
static void syscall_practice(struct intr_frame* f, int i);
static void syscall_halt(void);
static void syscall_exit(struct intr_frame* f, int exit_status);
static void syscall_exec(struct intr_frame* f, const char* cmd_line);
static void syscall_wait(struct intr_frame* f, pid_t child_pid);
static void syscall_compute_e(struct intr_frame* f, int n);

// File operation helper
static struct fdt_entry* get_fdt_entry(int fd);
static int next_fd(void);

// File operations
static void syscall_create(struct intr_frame* f, const char* file, unsigned initial_size);
static void syscall_remove(struct intr_frame* f, const char* file);
static void syscall_open(struct intr_frame* f, const char* file_name);
static void syscall_filesize(struct intr_frame* f, int fd);
static void syscall_read(struct intr_frame* f, int fd, char* buffer, unsigned length);
static void syscall_write(struct intr_frame* f, int fd, const char* buffer, unsigned length);
static void syscall_seek(int fd, unsigned position);
static void syscall_tell(struct intr_frame* f, int fd);
static void syscall_close(int fd);

// Extensible files
static void syscall_inumber(struct intr_frame* f, int fd);

// Subdirectories
static void syscall_chdir(struct intr_frame* f, char* dir);
static void syscall_mkdir(struct intr_frame* f, char* dir);
static void syscall_readdir(struct intr_frame* f, int fd, char* name);
static void syscall_isdir(struct intr_frame* f, int fd);

// Buffer cache
static void syscall_bc_reset(void);
static void syscall_bc_stat(float* hit_rate_cnt_ptr, int* block_write_cnt_ptr);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  // Get syscall type
  if (!valid_pointer((uint8_t*)&(args[0]), sizeof(uint32_t)))
    process_exit();
  uint32_t syscall_type = args[0];

  // Validate arguments, call respective helper function
  switch (syscall_type) {
    case SYS_PRACTICE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_practice(f, args[1]);
      break;
    }
    case SYS_HALT: {
      syscall_halt();
      break;
    }
    case SYS_EXIT: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_exit(f, args[1]);
      break;
    }
    case SYS_EXEC: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      char* cmd_line = valid_str_pointer((char*)args[1]);
      if (!cmd_line)
        process_exit();
      syscall_exec(f, cmd_line);
      free(cmd_line);
      break;
    }
    case SYS_WAIT: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(pid_t)))
        process_exit();
      syscall_wait(f, args[1]);
      break;
    }
    case SYS_CREATE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(unsigned)))
        process_exit();
      char* file = valid_str_pointer((char*)args[1]);
      if (!file)
        process_exit();
      syscall_create(f, file, args[2]);
      free(file);
      break;
    }
    case SYS_REMOVE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      char* file = valid_str_pointer((char*)args[1]);
      if (!file)
        process_exit();
      syscall_remove(f, file);
      free(file);
      break;
    }
    case SYS_OPEN: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      char* file = valid_str_pointer((char*)args[1]);
      if (!file)
        process_exit();
      syscall_open(f, file);
      free(file);
      break;
    }
    case SYS_FILESIZE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_filesize(f, args[1]);
      break;
    }
    case SYS_READ: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(void*)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[3]), sizeof(unsigned)))
        process_exit();
      void* buffer = (void*)args[2];
      unsigned length = args[3];
      if (!valid_pointer((uint8_t*)buffer, length))
        process_exit();
      syscall_read(f, args[1], buffer, length);
      break;
    }
    case SYS_WRITE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(void*)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[3]), sizeof(unsigned)))
        process_exit();
      void* buffer = (void*)args[2];
      unsigned length = args[3];
      if (!valid_pointer((uint8_t*)buffer, length))
        process_exit();
      syscall_write(f, args[1], buffer, length);
      break;
    }
    case SYS_SEEK: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(unsigned)))
        process_exit();
      syscall_seek(args[1], args[2]);
      break;
    }
    case SYS_TELL: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_tell(f, args[1]);
      break;
    }
    case SYS_CLOSE: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_close(args[1]);
      break;
    }
    case SYS_COMPUTE_E: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      int n = args[1];
      syscall_compute_e(f, n);
      break;
    }
    case SYS_INUMBER: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_inumber(f, args[1]);
      break;
    }
    case SYS_CHDIR: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      char* dir = valid_str_pointer((char*)args[1]);
      if (!dir)
        process_exit();
      syscall_chdir(f, dir);
      free(dir);
      break;
    }
    case SYS_MKDIR: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(char*)))
        process_exit();
      char* dir = valid_str_pointer((char*)args[1]);
      if (!dir)
        process_exit();
      syscall_mkdir(f, dir);
      free(dir);
      break;
    }
    case SYS_READDIR: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(char*)))
        process_exit();
      syscall_readdir(f, args[1], (char*)args[2]);
      break;
    }
    case SYS_ISDIR: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(int)))
        process_exit();
      syscall_isdir(f, args[1]);
      break;
    }
    case SYS_BC_RESET: {
      syscall_bc_reset();
      break;
    }
    case SYS_BC_STAT: {
      if (!valid_pointer((uint8_t*)&(args[1]), sizeof(float*)))
        process_exit();
      float* hit_rate_cnt_ptr = (float*)args[1];
      if (hit_rate_cnt_ptr && !valid_pointer((uint8_t*)hit_rate_cnt_ptr, sizeof(float)))
        process_exit();
      if (!valid_pointer((uint8_t*)&(args[2]), sizeof(int*)))
        process_exit();
      int* block_write_cnt_ptr = (int*)args[2];
      if (block_write_cnt_ptr && !valid_pointer((uint8_t*)block_write_cnt_ptr, sizeof(int)))
        process_exit();
      syscall_bc_stat(hit_rate_cnt_ptr, block_write_cnt_ptr);
      break;
    }
  }
}

/* Checks validity of byte at pointer P. */
static bool valid_byte_pointer(uint8_t* p) {
  uint32_t* pagedir = thread_current()->pcb->pagedir;
  return p != NULL && is_user_vaddr(p) && pagedir_get_page(pagedir, p) != NULL;
}

/* Checks validity of SIZE bytes at pointer P. */
static bool valid_pointer(uint8_t* p, size_t size) {
  return valid_byte_pointer(p) && valid_byte_pointer(p + size - 1);
}

/* Returns memory allocated copy of string S if S is valid. Returns NULL otherwise. */
static char* valid_str_pointer(char* s) {
  if (!s)
    return NULL;
  size_t max_str_len = PGSIZE;
  size_t str_len = 0;
  while (true) {
    if (!valid_byte_pointer((uint8_t*)s + str_len) || str_len >= max_str_len)
      return NULL;
    if (s[str_len] == '\0')
      break;
    str_len++;
  }
  char* new_s = malloc(str_len + 1);
  if (new_s != NULL) {
    memcpy(new_s, s, str_len + 1);
    new_s[str_len] = '\0';
  }
  return new_s;
}

/* Takes in a intr_frame pointer f, as well as a integer i, and increments i by 1. */
static void syscall_practice(struct intr_frame* f, int i) { f->eax = i + 1; }

/* Does not take any arguments, terminates Pintos using shutdown_power_off function in
    devices/shutdown.h. Use with caution, as may cause information loss about deadlock
    situations, etc. */
static void syscall_halt(void) { shutdown_power_off(); }

/* Takes in a intr_frame pointer f and a integer exit_status. Terminates the current
    user program, and returns exit_status to the kernel. Status 0 indicates success
    and nonzero values indicate errors. */
static void syscall_exit(struct intr_frame* f, int exit_status) {
  f->eax = exit_status;
  thread_current()->pcb->info->exit_status = exit_status;
  process_exit();
}

/* Takes in a intr_frame pointer f and a string pointer cmd_line. Runs the executable
    in cmd_line, passes any given arguments, and finally stores the new process's pid.
    In case the program cannot load or run, stores pid as -1. */
static void syscall_exec(struct intr_frame* f, const char* cmd_line) {
  pid_t child_pid = process_execute(cmd_line);
  if (child_pid == TID_ERROR) {
    f->eax = -1;
    return;
  }

  // Search for child proc_info struct
  struct proc_info* child_proc_info = NULL;
  struct list_elem* e;
  struct list* child_info = &thread_current()->pcb->child_info;
  for (e = list_begin(child_info); e != list_end(child_info); e = list_next(e)) {
    struct proc_info* curr_proc_info = list_entry(e, struct proc_info, elem);
    if (curr_proc_info->pid == child_pid) {
      child_proc_info = curr_proc_info;
      break;
    }
  }
  if (child_proc_info == NULL) {
    f->eax = -1;
    return;
  }

  // Synchronize load
  sema_down(&child_proc_info->load_sema);
  if (child_proc_info->load_status) {
    f->eax = child_pid;
  } else {
    f->eax = -1;
  }
}

/*  Takes in a intr_frame pointer f and a child process id child_pid. Waits for a child 
    process pid and retrieves the child's exit status. If pid is still alive, waits until 
    it terminates. Then, returns the status pid passed to exit. Must return -1 if pid did
    not. */
static void syscall_wait(struct intr_frame* f, pid_t child_pid) {
  f->eax = process_wait(child_pid);
}

/* Computes sys_sum_to_e(n). */
static void syscall_compute_e(struct intr_frame* f, int n) { f->eax = sys_sum_to_e(n); }

/* Get fdt_entry by searching fdt on fd. Return NULL if not found. */
static struct fdt_entry* get_fdt_entry(int fd) {
  struct list* fdt = &(thread_current()->pcb->fdt);
  struct fdt_entry* fdt_entry;
  for (struct list_elem* e = list_begin(fdt); e != list_end(fdt); e = list_next(e)) {
    fdt_entry = list_entry(e, struct fdt_entry, elem);
    if (fdt_entry->fd == fd) {
      return fdt_entry;
    }
  }
  return NULL;
}

// Gets next file descriptor from current process
static int next_fd(void) { return (thread_current()->pcb->next_fd)++; }

/* Creates a file with name FILE and the given INITIAL_SIZE.
   Returns true if successful, false otherwise. */
static void syscall_create(struct intr_frame* f, const char* file, unsigned initial_size) {
  f->eax = filesys_create(file, (off_t)initial_size);
}

/* Deletes the file with name FILE.
  Returns true if successful, false otherwise. */
static void syscall_remove(struct intr_frame* f, const char* file) {
  f->eax = filesys_remove(file);
}

/* Opens the file with name FILE_NAME.
   Returns the new file descriptor if successful or -1 otherwise. */
static void syscall_open(struct intr_frame* f, const char* file_name) {
  // Open file
  struct file* file = filesys_open(file_name);
  if (file == NULL) {
    f->eax = -1;
    return;
  }

  // New FDT entry
  struct fdt_entry* fdt_entry = malloc(sizeof(struct fdt_entry));
  if (fdt_entry == NULL) {
    f->eax = -1;
    return;
  }

  // Check if file is a directory
  if (inode_isdir(file_get_inode(file))) {
    fdt_entry->file = NULL;
    fdt_entry->dir = dir_open(inode_reopen(file_get_inode(file)));
    file_close(file);
  } else {
    fdt_entry->file = file;
    fdt_entry->dir = NULL;
  }

  fdt_entry->fd = next_fd();
  f->eax = fdt_entry->fd;

  // Add FDT entry to FDT
  list_push_back(&thread_current()->pcb->fdt, &fdt_entry->elem);
}

/* Returns the size, in bytes, of the open file with file descriptor fd. 
   Returns -1 if fd does not correspond to an entry in the file descriptor table. */
static void syscall_filesize(struct intr_frame* f, int fd) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = file_length(fdt_entry->file);
}

/* Read LENGTH bytes to BUFFER from file associated with file descriptor FD. Returns -1 if the file
   descriptor is not found in the file descriptor table. Returns number of byte read. */
static void syscall_read(struct intr_frame* f, int fd, char* buffer, unsigned length) {
  switch (fd) {
    case STDIN_FILENO: {
      f->eax = length;
      for (unsigned i = 0; i < length; i++) {
        buffer[i] = input_getc();
      }
      break;
    }
    case STDOUT_FILENO: {
      f->eax = 0; // Cannot read from standard out.
      break;
    }
    default: {
      struct fdt_entry* fdt_entry = get_fdt_entry(fd);
      if (fdt_entry == NULL || fdt_entry->dir != NULL) {
        f->eax = -1;
        return;
      }
      f->eax = file_read(fdt_entry->file, buffer, length);
      break;
    }
  }
}

/* Write LENGTH bytes from BUFFER to file associated with file descriptor FD. Returns -1 if the file
   descriptor is not found in the file descriptor table. Returns number of bytes written. */
static void syscall_write(struct intr_frame* f, int fd, const char* buffer, unsigned length) {
  switch (fd) {
    case STDIN_FILENO: {
      f->eax = 0;
      break;
    }
    case STDOUT_FILENO: {
      while (length) {
        if (length <= 256) {
          putbuf(buffer, length);
          break;
        } else {
          putbuf(buffer, 256);
          length -= 256;
        }
      }
      break;
    }
    default: {
      struct fdt_entry* fdt_entry = get_fdt_entry(fd);
      if (fdt_entry == NULL || fdt_entry->dir != NULL) {
        f->eax = -1;
        return;
      }
      f->eax = file_write(fdt_entry->file, buffer, length);
      break;
    }
  }
}

/* Changes the position of the next byte to be read/written to POSITION. If file descriptor FD not found
   in file descriptor table, syscall does nothing. */
static void syscall_seek(int fd, unsigned position) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry == NULL) {
    return;
  }
  file_seek(fdt_entry->file, position);
}

/* Returns the position of the next byte to be read/written from the file. If file descriptor not found in file
   descriptor table, syscall fails silently. */
static void syscall_tell(struct intr_frame* f, int fd) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry == NULL) {
    return;
  }
  f->eax = file_tell(fdt_entry->file);
}

/* Takes in a file descriptor FD, closes file and removes file descriptor from file descriptor table.
   If file descriptor not found in file descriptor table, syscall fails silently. */
static void syscall_close(int fd) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry == NULL) {
    return;
  }
  file_close(fdt_entry->file);
  dir_close(fdt_entry->dir);

  // Remove fdt_entry from FDT and free data
  list_remove(&fdt_entry->elem);
  free(fdt_entry);
}

static void syscall_inumber(struct intr_frame* f, int fd) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry->file)
    f->eax = inode_get_inumber(file_get_inode(fdt_entry->file));
  else
    f->eax = inode_get_inumber(dir_get_inode(fdt_entry->dir));
}

/* Change current working directory to DIR. */
static void syscall_chdir(struct intr_frame* f, char* dir) {
  struct process* pcb = thread_current()->pcb;
  struct dir* new_cwd = dir_open(dir_resolve_path(dir));
  if (!new_cwd) { /* Invalid directory. */
    f->eax = false;
    return;
  }
  dir_close(pcb->cwd);
  pcb->cwd = new_cwd;
  f->eax = true;
}

/* Create a directory named DIR. */
static void syscall_mkdir(struct intr_frame* f, char* dir) {
  /* Check for empty directory name. */
  if (dir[0] == '\0') {
    f->eax = false;
    return;
  }

  /* Determine path type. */
  bool absolute_path = dir[0] == '/';

  /* Get parent directory. */
  struct dir* parent_dir;
  char dir_name[NAME_MAX + 1];
  if (dir_file_path_num_parts(dir) == 1) { /* Simple file path. */
    if (absolute_path)
      parent_dir = dir_open_root();
    else
      parent_dir = dir_reopen(thread_current()->pcb->cwd);
    char* dir_cpy = dir;
    dir_get_next_part(dir_name, (const char**)&dir_cpy);
  } else {
    dir_split_file_path((char*)dir, dir_name);
    parent_dir = dir_open(dir_resolve_path((char*)dir));
  }

  /* Check valid directory name. */
  struct inode* inode_temp_p;
  if (dir_lookup(parent_dir, dir_name, &inode_temp_p)) { /* Name already exists in directory. */
    inode_close(inode_temp_p);
    dir_close(parent_dir);
    f->eax = false;
    return;
  }

  /* Create new directory. */
  block_sector_t dir_block;
  if (!free_map_allocate(1, &dir_block)) {
    dir_close(parent_dir);
    return;
  } else if (!dir_create(dir_block, 16)) {
    free_map_release(dir_block, 1);
    dir_close(parent_dir);
    return;
  }

  /* Setup new directory. */
  struct dir* new_dir = dir_open(inode_open(dir_block));
  dir_add(new_dir, ".", inode_get_inumber(dir_get_inode(new_dir)));
  dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(parent_dir)));
  inode_set_isdir(dir_get_inode(new_dir), true);
  dir_close(new_dir);

  /* Add directory to parent. */
  dir_add(parent_dir, dir_name, dir_block);
  dir_close(parent_dir);

  f->eax = true;
}

/* Read entry from directory corresponding to FD, storing entry's name into NAME. */
static void syscall_readdir(struct intr_frame* f, int fd, char* name) {
  /* Get corresponding fdt_entry. */
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  if (fdt_entry == NULL || fdt_entry->dir == NULL) {
    f->eax = false;
    return;
  }

  /* Read directory entry into NAME. */
  char entry_name[NAME_MAX + 1];
  do {
    if (!dir_readdir(fdt_entry->dir, entry_name)) {
      f->eax = false;
      return;
    }
  } while (!strcmp(entry_name, ".") || !strcmp(entry_name, ".."));
  memcpy(name, entry_name, NAME_MAX + 1);

  f->eax = true;
}

/* Check if file corresponding to FD is a directory. */
static void syscall_isdir(struct intr_frame* f, int fd) {
  struct fdt_entry* fdt_entry = get_fdt_entry(fd);
  f->eax = fdt_entry != NULL && fdt_entry->dir != NULL;
}

static void syscall_bc_reset(void) { buffer_cache_reset(); }

static void syscall_bc_stat(float* hit_rate_cnt_ptr, int* block_write_cnt_ptr) {
  if (hit_rate_cnt_ptr) {
    *hit_rate_cnt_ptr = buffer_cache_hit_rate();
  }
  if (block_write_cnt_ptr) {
    *block_write_cnt_ptr = block_write_cnt(fs_device);
  }
}
