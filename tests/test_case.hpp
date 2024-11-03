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
#pragma once

#include "stringpp.hpp"
#include <format>
#include <functional>
#include <rtpmidid/logger.hpp>
#include <string>

// NOLINTNEXTLINE
#define TEST(fn) {#fn, fn}

class test_exception : public std::exception {
public:
  std::string msg;
  const char *filename;
  int line;
  std::string error;

  test_exception(const char *file, int line, const std::string &error)
      : filename(file), line(line), error(error) {
    msg = FMT::format("{}:{} Test Fail: {}{}{}", file, line, "\033[1;31m",
                      error, "\033[0m");
  }
  const char *what() const noexcept override { return msg.c_str(); }
};

struct test_t {
  std::string name;
  std::function<void(void)> fn;
};

class test_case_t {
public:
  std::vector<test_t> tests;
  bool error = false;

  test_case_t(std::initializer_list<test_t> tests_) : tests(tests_) {}

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

  void help(const char *cmdname) {
    INFO("{} [--failfast] [--help] [testname...]", cmdname);
  }

  bool run(int argc = 0, char **argv = nullptr) {
    std::vector<std::string> args;
    bool fastfail = false;
    for (auto i = 1; i < argc; i++) {
      std::string opt = argv[i];
      if (opt == "--help") {
        help(argv[0]);
        return true;
      }
      if (opt == "--failfast") {
        fastfail = true;
      } else if (std::startswith(opt, "--")) {
        ERROR("Unknown argument.");
        help(argv[0]);
        return false;
      } else {
        args.push_back(std::move(opt));
      }
    }

    int errors = 0;
    auto total = tests.size();
    int count = 0;
    for (auto &tcase : tests) {
      count += 1;
      INFO("*******************************************************************"
           "**************************");
      INFO("Test ({}/{}) {} Run", count, total, tcase.name);
      if (string_in_argv(args, tcase.name)) {
        try {
          tcase.fn();
          INFO("Test {} OK", tcase.name);
        } catch (const test_exception &e) {
          ::rtpmidid::logger2.log(::rtpmidid::logger_level_t::ERROR, e.filename,
                                  e.line, "{}", e.error);
          ERROR("FAIL TEST {}: {}", tcase.name, e.what());
          errors += 1;
        } catch (const std::exception &e) {
          ERROR("FAIL TEST {}: {}", tcase.name, e.what());
          errors += 1;
        } catch (...) {
          ERROR("Unknown exception");
          errors += 1;
        }

        if (fastfail && errors) {
          return false;
        }
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

// NOLINTNEXTLINE
#define ASSERT_TRUE(A)                                                         \
  if (!(A)) {                                                                  \
    throw test_exception(__FILE__, __LINE__, "Assert [" #A "] failed");        \
  }
// NOLINTNEXTLINE
#define ASSERT_FALSE(A)                                                        \
  if (A) {                                                                     \
    throw test_exception(__FILE__, __LINE__, "Assert ![" #A "] failed");       \
  }
// NOLINTNEXTLINE
#define ASSERT_EQUAL(A, B)                                                     \
  if ((A) != (B)) {                                                            \
    ERROR("{} != {}", A, B);                                                   \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " == " #B "] failed");                  \
  }
// NOLINTNEXTLINE
#define ASSERT_IN(A, ...)                                                      \
  {                                                                            \
    auto set = {__VA_ARGS__};                                                  \
    auto value = A;                                                            \
    if (std::find(set.begin(), set.end(), A) == set.end()) {                   \
      ERROR("{} not in {}", value, "{" #__VA_ARGS__ "}");                      \
      throw test_exception(__FILE__, __LINE__,                                 \
                           "Assert [" #A " in " #__VA_ARGS__ "] failed");      \
    }                                                                          \
  }
// NOLINTNEXTLINE
#define ASSERT_NOT_EQUAL(A, B)                                                 \
  if ((A) == (B)) {                                                            \
    ERROR("{} == {}", A, B);                                                   \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " != " #B "] failed");                  \
  }
// NOLINTNEXTLINE
#define ASSERT_GT(A, B)                                                        \
  if ((A) <= (B)) {                                                            \
    ERROR("{} <= {}", A, B);                                                   \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " > " #B "] failed");                   \
  }
// NOLINTNEXTLINE
#define ASSERT_GTE(A, B)                                                       \
  if ((A) < (B)) {                                                             \
    ERROR("{} < {}", A, B);                                                    \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " >= " #B "] failed");                  \
  }
// NOLINTNEXTLINE
#define ASSERT_LT(A, B)                                                        \
  if ((A) >= (B)) {                                                            \
    ERROR("{} < {}", A, B);                                                    \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " < " #B "] failed");                   \
  }
// NOLINTNEXTLINE
#define ASSERT_LTE(A, B)                                                       \
  if ((A) > (B)) {                                                             \
    ERROR("{} > {}", A, B);                                                    \
    throw test_exception(__FILE__, __LINE__,                                   \
                         "Assert [" #A " <= " #B "] failed");                  \
  }
// NOLINTNEXTLINE
#define FAIL(msg)                                                              \
  { throw test_exception(__FILE__, __LINE__, msg); }
