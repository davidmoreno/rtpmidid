#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <rtpmidid/logger.hpp>

int rtpmidi_rand(void)
{
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1) {
    ERROR("Cannot access /dev/urandom! {}", strerror(errno));
    return rand();  // not good
  }

  int tgt;
  uint8_t *p = (uint8_t *)&tgt;
  size_t n_left = sizeof tgt;

  while(n_left > 0) {
    int rc = read(fd, p, n_left);
    if (rc == -1) {
      ERROR("Cannot read from /dev/urandom! {}", strerror(errno));
      close(fd);
      return rand();  // not good
    }

    p += rc;
    n_left -= rc;
  }

  close(fd);

  return tgt;
}
