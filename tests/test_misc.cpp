/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "./test_case.hpp"
#include <rtpmidid/logger.hpp>
#include <unistd.h>

void test_warning_once(void) {
  INFO("For this test some open/close of files is going on");

  // Set the fds 0 and 1 to read / write.
  // keep a copy, for recover later
  int oldfd0 = dup(0);
  int oldfd1 = dup(1);

  int pipefd[2];
  close(0);
  close(1);
  auto ok = pipe(pipefd);
  ASSERT_TRUE(ok >= 0);
  ASSERT_EQUAL(pipefd[0], 0);
  ASSERT_EQUAL(pipefd[1], 1);

  for (int i = 0; i < 100; i++) {
    WARNING_ONCE("This warning should appear only once");
    ERROR_ONCE("This error should appear only once");
  }

  char buffer[1024];
  auto len = read(0, &buffer, sizeof(buffer));

  close(0);
  auto fd0 = dup(oldfd0);
  ASSERT_EQUAL(fd0, 0);
  close(1);
  auto fd1 = dup(oldfd1);
  ASSERT_EQUAL(fd1, 1);
  close(oldfd0);
  close(oldfd1);

  ASSERT_TRUE(len < 1000);
  buffer[len] = 0;

  INFO("Output text:\n\t {}", buffer);
  int nbr = 0;
  for (char *ch = buffer; ch < buffer + len; ch++) {
    if (*ch == '\n') {
      nbr++;
    }
  }
  ASSERT_EQUAL(nbr, 2);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_warning_once),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
