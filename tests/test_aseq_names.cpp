/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "../src/aseq.hpp"
#include "./test_case.hpp"
#include <rtpmidid/logger.hpp>

using rtpmididns::aseq_join_names;

void test_join_names_port_repeats_client(void) {
  // Client name repeated at the start of the port name: keep it once.
  ASSERT_EQUAL(aseq_join_names("JUPITER-Xm", "JUPITER-Xm MIDI 1"),
               "JUPITER-Xm MIDI 1");
  ASSERT_EQUAL(aseq_join_names("Peak", "Peak MIDI 1"), "Peak MIDI 1");
  // Exactly the same name.
  ASSERT_EQUAL(aseq_join_names("VMPK", "VMPK"), "VMPK");
}

void test_join_names_unrelated(void) {
  // Nothing in common: both names are needed.
  ASSERT_EQUAL(aseq_join_names("VMPK", "Out 1"), "VMPK-Out 1");
  ASSERT_EQUAL(aseq_join_names("FLUID Synth", "Synth input port"),
               "FLUID Synth-Synth input port");
}

void test_join_names_contained_not_at_start(void) {
  // Repetition in the middle is also a repetition.
  ASSERT_EQUAL(aseq_join_names("Peak", "USB Peak MIDI 1"), "USB Peak MIDI 1");
  // And the reverse, the client already contains the port name.
  ASSERT_EQUAL(aseq_join_names("Arturia KeyStep", "KeyStep"),
               "Arturia KeyStep");
}

void test_join_names_only_whole_words(void) {
  // "Pea" is a substring of "Peak", but not a whole word: it must not be
  // taken for a repetition.
  ASSERT_EQUAL(aseq_join_names("Pea", "Peak MIDI 1"), "Pea-Peak MIDI 1");
  // Case differences still count as the same word.
  ASSERT_EQUAL(aseq_join_names("peak", "Peak MIDI 1"), "Peak MIDI 1");
}

void test_join_names_empty(void) {
  ASSERT_EQUAL(aseq_join_names("", "Out 1"), "Out 1");
  ASSERT_EQUAL(aseq_join_names("VMPK", ""), "VMPK");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_join_names_port_repeats_client),
      TEST(test_join_names_unrelated),
      TEST(test_join_names_contained_not_at_start),
      TEST(test_join_names_only_whole_words),
      TEST(test_join_names_empty),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}
