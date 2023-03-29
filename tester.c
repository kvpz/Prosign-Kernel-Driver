#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

int main(void)
{
  int fd = open("/dev/mc", O_WRONLY);
  if(fd < 0) {
    printf("Issue opening the file descriptor\n");
  }
  printf("File opened successfully\n");

  write(fd, "hello world", 11);
  printf("Write successful\n");
  
  close(fd);
  
  return 0;
}
