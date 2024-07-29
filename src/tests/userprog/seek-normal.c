/* Tests the seek syscall. Relies on the correctness of the create, open, write, and read syscalls. */

#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <string.h>

void test_main(void) {
  int handle;

  CHECK(create("test.txt", 12), "create \"test.txt\"");
  CHECK((handle = open("test.txt")) > 1, "open \"test.txt\"");

  char* write_buf = "hello world";
  char read_buf[6];

  // Write "hello world\0" into test.txt
  write(handle, write_buf, 12);

  // Attempt to set position of test.txt to 6 with seek
  seek(handle, 6);

  // Read remaining characters from test.txt into read_buf
  read(handle, read_buf, 6);

  // Check if seek correctly set position of test.txt to 6
  if (strcmp(read_buf, "world") != 0) {
    fail("Seek syscall failed because file position was not set to 6 as expected");
  }
}