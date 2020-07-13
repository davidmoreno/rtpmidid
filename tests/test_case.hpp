/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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
#pragma once

#include <functional>
#include <rtpmidid/logger.hpp>
#include <string>

#define TEST(fn)                                                               \
  { #fn, fn }

class test_exception : public std::exception {
public:
  std::string msg;
  const char *filename;
  int line;
  std::string error;

  test_exception(const char *file, int line, const std::string &error) {
    this->filename = file;
    this->line = line;
    this->error = error;
    msg = fmt::format("{}:{} Test Fail: {}{}{}", file, line, "\033[1;31m",
                      error, "\033[0m");
  }
  virtual const char *what() const noexcept { return msg.c_str(); }
};

struct test_t {
  std::string name;
  std::function<void(void)> fn;
};

class test_case_t {
public:
  std::vector<test_t> tests;
  bool error = false;

  test_case_t(const std::initializer_list<test_t> &tests_) { tests = tests_; }

  bool string_in_argv(const std::vector<std::string> &args,
                      const std::string &name) {
    if (args.size() == 0)
      return true;

    for (auto &item : args) {
      if (name.find(item) != name.npos)
        return true;
    }
    return false;
  }

  bool run(int argc = 0, char **argv = nullptr) {
    std::vector<std::string> args;
    for (auto i = 1; i < argc; i++) {
      args.push_back(argv[i]);
    }

    int errors = 0;
    int total = tests.size();
    int count = 0;
    for (auto &tcase : tests) {
      count += 1;
      INFO("*******************************************************************"
           "**************************");
      INFO("Test ({}/{}) {} Run", count, total, tcase.name);
      if (string_in_argv(args, tcase.name))
        try {
          tcase.fn();
          SUCCESS("Test {} OK", tcase.name);
        } catch (const test_exception &e) {
          logger::log(e.filename, e.line, logger::ERROR, e.error);
          errors += 1;
        } catch (const std::exception &e) {
          ERROR("{}", e.what());
          errors += 1;
        } catch (...) {
          ERROR("Unknown exception");
          errors += 1;
        }
    }
    if (errors == 0) {
      INFO("No errors.");
      return true;
    } else {
      error = true;
      ERROR("{} Errors", errors);
      return false;
    }
  }

  int exit_code() { return error ? 1 : 0; }
};

#define ASSERT_TRUE(A)                                                         \
  if (!(A)) {                                                                  \
    throw test_exception(__FILE__, __LINE__, "Assert [" #A "] failed");        \
  }
#define ASSERT_FALSE(A)                                                        \
  if (A) {                                                                     \
    throw test_exception(__FILE__, __LINE__, "Assert ![" #A "] failed");       \
  }
#define ASSERT_EQUAL(A, B)                                                     \
  if ((A) != (B)) {                                                            \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " == " #B "] failed");                  \
  }
#define ASSERT_NOT_EQUAL(A, B)                                                 \
  if ((A) == (B)) {                                                            \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " != " #B "] failed");                  \
  }
#define ASSERT_GT(A, B)                                                        \
  if ((A) <= (B)) {                                                            \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " > " #B "] failed");                   \
  }
#define ASSERT_GTE(A, B)                                                       \
  if ((A) < (B)) {                                                             \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " >= " #B "] failed");                  \
  }
#define ASSERT_LT(A, B)                                                        \
  if ((A) >= (B)) {                                                            \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " < " #B "] failed");                   \
  }
#define ASSERT_LTE(A, B)                                                       \
  if ((A) > (B)) {                                                             \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " <= " #B "] failed");                  \
  }
#define FAIL(msg)                                                              \
  { throw test_exception(__FILE__, __LINE__, msg); }
