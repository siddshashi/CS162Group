#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          // Page directory.
  char process_name[16];      // Name of the main thread
  struct thread* main_thread; // Pointer to main thread
  struct list child_info;     // List of child proc_info structs
  struct proc_info* info;     // Pointer to own proc_info struct
  struct list fdt;            // File descriptor table
  struct file* file;          // Pointer to process's executable file
  int next_fd;                // Next available file descriptor for file descriptor table
  struct dir* cwd;            // Pointer to the current working directory
};

struct proc_info {
  char* file_name;            // Filename for passing into start_process()
  pid_t pid;                  // Identifier of proc_info struct
  bool load_status;           // Load status of the process
  struct semaphore load_sema; // Synchronize load status between parent and child processes
  int exit_status;            // Exit status of the process
  struct semaphore exit_sema; // Synchronize exit status between parent and child processes
  bool waited;                // Indicator if process has been waited on
  int ref_cnt;                // How many places proc_info is referenced, free struct when 0
  struct lock ref_cnt_lock;   // Synchronize ref_cnt updates
  struct list_elem elem;      // Element of child_info Pintos list
  struct dir* cwd;            // Pointer to the current working directory
};

struct fdt_entry {
  int fd;                // File descriptor
  struct file* file;     // Pointer to open file
  struct dir* dir;       // Pointer to open directory
  struct list_elem elem; // Element of file descriptor table
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
