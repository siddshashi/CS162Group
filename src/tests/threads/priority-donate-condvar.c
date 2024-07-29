/* Low priority thread L acquires a lock, then calls cond_wait
   to sleep. Medium priority thread M calls cond_wait to sleep. 
   Next, high priority thread H attempts to
   acquire the lock, donating its priority to L.

   Next, the main thread calls cond_signal, waking up L. 
   L releases the lock, which wakes up H. H releases lock and terminates, 
   then L releases lock and terminates. Main thread calls cond_signal again, 
   waking up M. M terminates, then main terminates. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func l_thread_func;
static thread_func m_thread_func;
static thread_func h_thread_func;

static struct lock resource_lock;
static struct lock cond_lock;
static struct condition cond;

void test_priority_donate_condvar(void) {
  /* This test does not work with the MLFQS. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  // Initialize synchronization primitives
  lock_init(&resource_lock);
  lock_init(&cond_lock);
  cond_init(&cond);

  // Thread L acquires resource_lock, goes to sleep
  msg("Thread L created.");
  thread_create("low", PRI_DEFAULT + 1, l_thread_func, NULL);

  // Thread M goes to sleep
  msg("Thread M created.");
  thread_create("med", PRI_DEFAULT + 3, m_thread_func, NULL);

  // Thread H tries to acquire resource_lock, donates priority to L and goes to sleep
  msg("Thread H created.");
  thread_create("high", PRI_DEFAULT + 5, h_thread_func, NULL);

  // Main thread calls cond_signal, wakes up L (and indirectly wakes up H)
  lock_acquire(&cond_lock);
  msg("Main thread calls cond_signal.");
  cond_signal(&cond, &cond_lock);
  lock_release(&cond_lock);

  // Main thread calls cond_signal, wakes up M
  lock_acquire(&cond_lock);
  msg("Main thread calls cond_signal a second time.");
  cond_signal(&cond, &cond_lock);
  lock_release(&cond_lock);

  msg("Main thread finished.");
}

static void l_thread_func(void* aux UNUSED) {
  lock_acquire(&resource_lock);
  msg("Thread L acquired resource_lock.");

  lock_acquire(&cond_lock);
  msg("Thread L acquired cond_lock and sleeps.");

  cond_wait(&cond, &cond_lock);

  msg("Thread L releases cond_lock after waking up.");
  lock_release(&cond_lock);

  msg("Thread L releases resource_lock.");
  lock_release(&resource_lock);

  msg("Thread L finished.");
}

static void m_thread_func(void* aux UNUSED) {
  lock_acquire(&cond_lock);
  msg("Thread M acquired cond_lock and sleeps.");

  cond_wait(&cond, &cond_lock);

  msg("Thread M releases cond_lock after waking up.");
  lock_release(&cond_lock);

  msg("Thread M finished.");
}

static void h_thread_func(void* aux UNUSED) {
  lock_acquire(&resource_lock);
  msg("Thread H acquired resource_lock.");

  msg("Thread H finished.");
}
