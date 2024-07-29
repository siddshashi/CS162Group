/* Test the buffer cache’s effectiveness by measuring its hit rate. 
Reset the buffer cache, then open a file and read it sequentially to 
determine the cache hit rate for a cold cache. Then, close it, re-open it, 
and read it sequentially again, making sure that the cache hit rate improves. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  /* Create test file. */
  char* file_name = "test";
  CHECK(create(file_name, 10240), "Create file \"%s\".", file_name);

  /* Reset buffer cache, so cache is cold. */
  bc_reset();

  /* Open file and read sequentially (256B at a time). */
  int fd;
  int bytes_read = 0;
  int total_bytes_read = 0;
  char buf[256];
  CHECK((fd = open(file_name)) > 1, "Open file \"%s\".", file_name);
  do {
    bytes_read = read(fd, &buf, 256);
    total_bytes_read += bytes_read;
  } while (bytes_read > 0);
  CHECK(total_bytes_read == 10240, "Total bytes read %d.", total_bytes_read);

  /* Get cache hit rate for cold cache. */
  float initial_hit_rate;
  bc_stat(&initial_hit_rate, NULL);
  msg("Get hit rate for cold cache.");

  /* Close file, then reopen. */
  close(fd);
  CHECK((fd = open(file_name)) > 1, "Reopen file \"%s\".", file_name);

  /* Read file sequentially (256B at a time). */
  bytes_read = 0;
  total_bytes_read = 0;
  do {
    bytes_read = read(fd, &buf, 256);
    total_bytes_read += bytes_read;
  } while (bytes_read > 0);
  CHECK(total_bytes_read == 10240, "Total bytes read %d.", total_bytes_read);

  /* Get cache hit rate for hot cache. */
  float final_hit_rate;
  bc_stat(&final_hit_rate, NULL);
  msg("Get hit rate for hot cache.");

  /* Check for improved hit rate. */
  CHECK(initial_hit_rate < final_hit_rate, "Improved hit rate for hot cache.");
}