/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once
#include "./exceptions.hpp"
#include "logger.hpp"
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-avoid-magic-numbers)

namespace rtpmidid {
class io_bytes_reader;
class io_bytes_writer;

static constexpr uint32_t BYTE_MASK = 0x0FF;

/**
 * @short iobuffer to read and write bin data.
 *
 * It always references an external buffer, normally stack allocated, and
 * does NOT manage the buffer.
 *
 * Read is done with a reader, and write with a writer. Its possible to
 * Switch between the IO mode easily with to_reader, or to_writer. At
 * conversion it seeks to the start of the buffer.
 */
class io_bytes {
public:
  uint8_t *start = nullptr;
  uint8_t *end = nullptr;
  uint8_t *position = nullptr;

  io_bytes() {}
  ~io_bytes() = default;

  io_bytes(io_bytes &other)
      : start(other.start), end(other.end), position(other.position) {}
  io_bytes(io_bytes_writer &other);

  io_bytes(uint8_t *start, uint32_t size)
      : start(start), end(start + size), position(start) {}
  io_bytes(io_bytes &&other) = delete;
  io_bytes &operator=(io_bytes &&other) = delete;
  io_bytes &operator=(const io_bytes &other) = default;

  void check_enough(size_t nbytes) const {
    if (position + nbytes > end)
      throw exception("Try to access end of buffer at {}",
                      (position - start) + nbytes);
  }
  void assert_valid_position() const {
    if (position > end)
      throw exception("Invalid buffer position {}", position - start);
  }
  void skip(int nbytes) {
    position += nbytes;
    assert_valid_position();
  }
  void seek(size_t pos) {
    position = start + pos;
    assert_valid_position();
  }
  size_t size() const { return end - start; }
  size_t pos() const { return position - start; }

  bool compare(const io_bytes &other) const {
    if (size() != other.size())
      return false;
    uint32_t i = 0;
    auto l = size();
    for (i = 0; i < l; i++) {
      if (other.start[i] != start[i])
        return false;
    }
    return true;
  }

  void print_hex(bool to_end = true, bool ascii = false) const {
    constexpr int LINE_SIZE = 16;
    constexpr int LINE_SIZE_THIRD = LINE_SIZE / 3;

    auto data = start;
    auto n = (to_end ? end : position) - data;
    puts("\033[1;34m");
    for (int i = 0; i < n; i++) {
      if (data == position) {
        puts("\033[0m");
      }
      // NOLINTNEXTLINE
      printf("%02X ", (*data) & BYTE_MASK);
      if (i % LINE_SIZE_THIRD == (LINE_SIZE_THIRD - 1))
        puts(" ");
      if (i % LINE_SIZE == (LINE_SIZE - 1))
        puts("\n");
      ++data;
    }
    puts("\n");
    if (!ascii)
      return;
    puts("\033[1;34m");
    data = start;
    for (int i = 0; i < n; i++) {
      if (data == position) {
        puts("\033[0m");
      }
      if (isprint(*data)) {
        putc(*data, ::stdout);
      } else {
        puts(".");
      }
      if (i % 4 == 3)
        puts(" ");
      if (i % LINE_SIZE == (LINE_SIZE - 1))
        puts("\n");
      ++data;
    }
    puts("\n");
  }
};
class io_bytes_writer : public io_bytes {
public:
  io_bytes_writer(io_bytes &other) {
    start = other.start;
    position = other.position;
    end = other.end;
  }
  io_bytes_writer(uint8_t *data, size_t size) {
    start = data;
    position = data;
    end = start + size;
  }

  void write_uint8(uint16_t n) {
    check_enough(1);
    *position++ = (n & BYTE_MASK);
  }
  void write_uint16(uint16_t n) { // NOLINT
    check_enough(2);
    *position++ = (n >> 8) & BYTE_MASK;
    *position++ = (n & BYTE_MASK);
  }

  void write_uint32(uint32_t n) {
    check_enough(4);
    *position++ = (n >> 24) & BYTE_MASK;
    *position++ = (n >> 16) & BYTE_MASK;
    *position++ = (n >> 8) & BYTE_MASK;
    *position++ = (n & BYTE_MASK);
  }
  void write_uint64(uint64_t n) {
    check_enough(8);
    *position++ = (n >> 56) & BYTE_MASK;
    *position++ = (n >> 48) & BYTE_MASK;
    *position++ = (n >> 40) & BYTE_MASK;
    *position++ = (n >> 32) & BYTE_MASK;

    *position++ = (n >> 24) & BYTE_MASK;
    *position++ = (n >> 16) & BYTE_MASK;
    *position++ = (n >> 8) & BYTE_MASK;
    *position++ = (n & BYTE_MASK);
  }

  void write_str0(const std::string_view &view) {
    check_enough(view.size());
    for (auto c : view) {
      *position++ = c;
    }
    *position++ = '\0';
  }

  /// Copies from position to the end
  void copy_from_and_consume(io_bytes &from) {
    copy_from(from, from.end - from.position);
  }

  void copy_from_and_consume(io_bytes &from, size_t count) {
    check_enough(count);
    from.check_enough(count);
    memcpy(position, from.position, count);
    position += count;
    from.position += count;
  }

  void copy_from(const io_bytes &from) { copy_from(from, from.size()); }

  void copy_from(const io_bytes &from, size_t count) {
    check_enough(count);
    from.check_enough(count);
    memcpy(position, from.position, count);
    position += count;
  }

  void copy_from(uint8_t *data, size_t count) {
    check_enough(count);
    memcpy(position, data, count);
    position += count;
  }
};

class io_bytes_reader : public io_bytes {
public:
  io_bytes_reader(const io_bytes &other) {
    start = other.start;
    end = other.end;
    position = other.position;
  }
  io_bytes_reader(const io_bytes_writer &other) {
    start = other.start;
    end = other.position;
    position = other.start;
  }
  // NOLINTNEXTLINE
  io_bytes_reader(const io_bytes_reader &other) {
    start = other.start;
    end = other.end;
    position = other.position;
  }
  io_bytes_reader(uint8_t *data, size_t size) {
    start = data;
    position = data;
    end = start + size;
  }
  io_bytes_reader(io_bytes_reader &&other) = delete;
  io_bytes_reader &operator=(io_bytes_reader &&other) = delete;

  // Convert a writer into a reader, seeks to the start automatically, can read
  // up to the write point.
  io_bytes_reader(io_bytes_writer &convert) {
    start = convert.start;
    end = convert.position;
    position = convert.start;
  }
  ~io_bytes_reader() = default;

  io_bytes_reader &operator=(const io_bytes_reader &other) = default;

  uint32_t read_uint32() {
    check_enough(4);
    auto data = position;
    position += 4;
    return ((uint32_t)data[0] << 24) + ((uint32_t)data[1] << 16) +
           ((uint32_t)data[2] << 8) + ((uint32_t)data[3]);
  }

  uint64_t read_uint64() {
    check_enough(8);
    auto data = position;
    position += 8;
    return (((uint64_t)data[0] << 56) + ((uint64_t)data[1] << 48) +
            ((uint64_t)data[2] << 40) + ((uint64_t)data[3] << 32) +
            ((uint64_t)data[4] << 24) + ((uint64_t)data[5] << 16) +
            ((uint64_t)data[6] << 8) + ((uint64_t)data[7]));
  }

  uint16_t read_uint16() {
    check_enough(2);
    auto data = position;
    position += 2;
    return ((uint16_t)data[0] << 8) + ((uint16_t)data[1]);
  }

  uint8_t read_uint8() {
    check_enough(1);
    auto data = position;
    position += 1;
    return data[0];
  }

  // The returned str is the address inside the buffer.
  std::string_view read_str0() {
    auto *strstart = position;
    while (*position && position < end) {
      position++;
    }
    // Normally I stopped because of *position == 0.. But I might have got out
    // of range If I'm on range, construct the std::string up tp pos
    // NOLINTNEXTLINE
    std::string_view ret(reinterpret_cast<char *>(strstart),
                         size_t(position - strstart));
    position++;
    return ret;
  }
};

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

template <size_t Size> class io_bytes_static : public io_bytes {
public:
  uint8_t data[Size];
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  io_bytes_static() : io_bytes(data, Size) {}
};

template <size_t Size> class io_bytes_writer_static : public io_bytes_writer {
public:
  uint8_t data[Size];
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  io_bytes_writer_static() : io_bytes_writer(data, Size) {
    memset(data, 0, Size);
  }
};

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

class io_bytes_managed : public io_bytes {
public:
  std::vector<uint8_t> data;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  io_bytes_managed(int size) : data(size) {
    start = data.data();
    end = data.data() + size;
    position = start;
  }
  io_bytes_managed(const io_bytes_managed &) = delete;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  io_bytes_managed(io_bytes_managed &&other) noexcept
      : data(std::move(other.data)) {
    start = other.start;
    end = other.end;
    position = other.position;
  }
  io_bytes_managed &operator=(io_bytes_managed &&other) noexcept {
    data = std::move(other.data);
    start = other.start;
    end = other.end;
    position = other.position;
    return *this;
  }
  ~io_bytes_managed() = default;

  io_bytes_managed &operator=(const io_bytes_managed &other) = delete;
};

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-avoid-magic-numbers)

} // namespace rtpmidid

template <>
struct fmt::formatter<rtpmidid::io_bytes_reader> : formatter<fmt::string_view> {
  auto format(const rtpmidid::io_bytes_reader &data, format_context &ctx) {
    return formatter<fmt::string_view>::format(
        fmt::format("[io_bytes_reader {} to {}, at {}, {}B left]",
                    (void *)data.start, (void *)data.end, (void *)data.position,
                    data.end - data.position),
        ctx);
  }
};
template <>
struct fmt::formatter<rtpmidid::io_bytes_writer> : formatter<fmt::string_view> {
  auto format(const rtpmidid::io_bytes_reader &data, format_context &ctx) {
    return formatter<fmt::string_view>::format(
        fmt::format("[io_bytes_writer {} to {}, at {}, {}B left]",
                    (void *)data.start, (void *)data.end, (void *)data.position,
                    data.end - data.position),
        ctx);
  }
};
