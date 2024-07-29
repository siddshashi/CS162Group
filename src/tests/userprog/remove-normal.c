/* Verifies functionality of remove, by creating and then removing it */
/* Tests the remove syscall. Relies on correctness of create, open, write, and close syscalls.*/

#include "tests/lib.h"
#include <syscall.h>
#include "tests/main.h"

void test_main(void) {
  int fd;

  // Create and open file
  CHECK(create("test.txt", 100), "create \"test.txt\"");
  CHECK((fd = open("test.txt")) > 1, "open \"test.txt\"");

  // Test remove
  bool success = remove("test.txt");
  if (!success)
    fail("Remove syscall failed to remove test.txt");

  // Test writing after remove
  int num_bytes = write(fd, "hello", sizeof("hello"));
  if (num_bytes < 0)
    fail("Unable to write to file after file removed but not closed");

  // Test open after removed file is closed
  close(fd);
  if (open("test.txt") != -1)
    fail("File opened after file removed and closed");
}
