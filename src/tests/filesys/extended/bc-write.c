/* Test the buffer cache's ability to coalesce writes to the same sector.
Write a large file (~64KiB, twice the maximum allowed buffer cache size), 
byte-by-byte. Then, read it in byte-by-byte. The total number of device 
writes should be on the order of 128, since 64KiB is 128 blocks. */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  /* Get initial number of block device writes. */
  int initial_write_cnt;
  bc_stat(NULL, &initial_write_cnt);

  /* Create empty test file. */
  char* file_name = "test";
  CHECK(create(file_name, 0), "Create file \"%s\".", file_name);

  /* Open test file. */
  int fd;
  CHECK((fd = open(file_name)) > 1, "Open file \"%s\".", file_name);

  /* Write 64KiB to test file, byte-by-byte. */
  int file_size = 65536;
  int total_bytes_written = 0;
  int bytes_written = 0;
  while (total_bytes_written < file_size) {
    bytes_written = write(fd, sample, 1);
    total_bytes_written += bytes_written;
  }
  CHECK(total_bytes_written >= file_size, "Write >= 64KiB to file.");
  CHECK(file_size == filesize(fd), "File has size >= 64KiB.");

  /* Flush buffer cache to write back any remaining dirty file cache blocks to disk. */
  bc_reset();

  /* Read 64KiB from test file, byte-by-byte. */
  char buf;
  int total_bytes_read = 0;
  int bytes_read = 0;
  while (total_bytes_read < file_size) {
    seek(fd, 0);
    bytes_read = read(fd, &buf, 1);
    total_bytes_read += bytes_read;
  }
  CHECK(total_bytes_read == file_size, "Read >= 64KiB from file.");

  /* Get final number of block device writes. */
  int final_write_cnt;
  bc_stat(NULL, &final_write_cnt);

  /* Calculate number of device writes. Check value is on order of 128. */
  int write_cnt = final_write_cnt - initial_write_cnt;
  CHECK(write_cnt <= 128 * 1.25, "The total number of device writes is on order of 128.");
}
