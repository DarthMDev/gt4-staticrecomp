// Fixture for the slice158 CRT table rewrite. The table bytes are the values read from the pinned
// SCUS_973.28 ELF at 0x618648 (nine rows of {id, interlace, mode, field}, the ninth is the fallback).
// The lookups below mirror func_001060A0 and func_00106058 from the guest code.

#include "display_table.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using gt4::display::crt_table_row_bytes;
using gt4::display::crt_table_search_rows;

struct Row {
  std::uint32_t id, interlace, mode, field;
};

constexpr Row elf_rows[9] = {
    {1, 1, 0x2, 1}, {2, 1, 0x2, 0}, {3, 1, 0x3, 1}, {4, 1, 0x3, 0}, {6, 0, 0x50, 1},
    {8, 0, 0x52, 1}, {7, 1, 0x51, 1}, {5, 0, 0, 1}, {0, 1, 0x2, 1},
};

std::vector<std::uint8_t> make_table(const Row (&rows)[9]) {
  std::vector<std::uint8_t> bytes(9 * crt_table_row_bytes);
  for (std::size_t r = 0; r < 9; ++r) {
    const std::uint32_t words[4] = {rows[r].id, rows[r].interlace, rows[r].mode, rows[r].field};
    for (std::size_t w = 0; w < 4; ++w)
      for (int b = 0; b < 4; ++b)
        bytes[r * crt_table_row_bytes + 4 * w + b] = static_cast<std::uint8_t>(words[w] >> (8 * b));
  }
  return bytes;
}

Row read_row(const std::vector<std::uint8_t> &bytes, std::size_t index) {
  std::uint32_t words[4] = {};
  for (std::size_t w = 0; w < 4; ++w)
    for (int b = 0; b < 4; ++b)
      words[w] |= static_cast<std::uint32_t>(bytes[index * crt_table_row_bytes + 4 * w + b]) << (8 * b);
  return {words[0], words[1], words[2], words[3]};
}

// func_001060A0: the id of the first search row with these values, or 0.
std::uint32_t lookup_id(const std::vector<std::uint8_t> &bytes, std::uint32_t interlace, std::uint32_t mode,
                        std::uint32_t field) {
  for (std::size_t i = 0; i < crt_table_search_rows; ++i) {
    const Row row = read_row(bytes, i);
    if (row.interlace == interlace && row.mode == mode && row.field == field)
      return row.id;
  }
  return 0;
}

// func_00106058: the first search row with this id, or the fallback row 8.
Row fetch_row(const std::vector<std::uint8_t> &bytes, std::uint32_t id) {
  for (std::size_t i = 0; i < crt_table_search_rows; ++i) {
    const Row row = read_row(bytes, i);
    if (row.id == id)
      return row;
  }
  return read_row(bytes, crt_table_search_rows);
}

bool same_row(const Row &a, const Row &b) {
  return a.id == b.id && a.interlace == b.interlace && a.mode == b.mode && a.field == b.field;
}

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void test_boot_path_reaches_480p() {
  auto bytes = make_table(elf_rows);
  require(lookup_id(bytes, 1, 2, 1) == 1, "baseline: boot request selects id 1");
  require(same_row(fetch_row(bytes, 1), elf_rows[0]), "baseline: id 1 is 480i");

  gt4::display::rewrite_table_to_480p(bytes.data(), bytes.size());

  // The boot request (480i values) no longer matches a row: id 0 selects the fallback row.
  require(lookup_id(bytes, 1, 2, 1) == 0, "480i request has no matching row after the rewrite");
  const Row boot = fetch_row(bytes, lookup_id(bytes, 1, 2, 1));
  require(boot.interlace == 0 && boot.mode == 0x50 && boot.field == 1, "boot path fetches 480p");
  // A 480p request selects id 1, whose row is now 480p; the 480p row keeps id 6.
  require(lookup_id(bytes, 0, 0x50, 1) == 1, "480p request selects id 1");
  const Row progressive = fetch_row(bytes, 1);
  require(progressive.id == 1 && progressive.interlace == 0 && progressive.mode == 0x50 && progressive.field == 1,
          "id 1 is 480p after the rewrite");
  require(same_row(fetch_row(bytes, 6), elf_rows[4]), "id 6 is untouched");
  for (std::size_t i = 1; i < 8; ++i) {
    if (i == 4) continue;
    require(same_row(read_row(bytes, i), elf_rows[i]), "other rows are unchanged");
  }
  require(read_row(bytes, 8).id == 0 && read_row(bytes, 8).mode == 0x50, "fallback row is 480p");
}

void test_refusals_leave_table_unchanged() {
  auto check_refused = [](Row (&rows)[9], const char *name) {
    auto bytes = make_table(rows);
    const auto before = bytes;
    bool refused = false;
    try {
      gt4::display::rewrite_table_to_480p(bytes.data(), bytes.size());
    } catch (const std::runtime_error &) {
      refused = true;
    }
    require(refused, name);
    require(bytes == before, "a refused rewrite does not write");
  };
  Row changed_row[9];
  for (int i = 0; i < 9; ++i) changed_row[i] = elf_rows[i];
  changed_row[0].mode = 0x51;
  check_refused(changed_row, "a changed id 1 row is refused");
  Row changed_fallback[9];
  for (int i = 0; i < 9; ++i) changed_fallback[i] = elf_rows[i];
  changed_fallback[8].id = 9;
  check_refused(changed_fallback, "a changed fallback row is refused");
  Row no_480p[9];
  for (int i = 0; i < 9; ++i) no_480p[i] = elf_rows[i];
  no_480p[4].mode = 0x52;
  check_refused(no_480p, "a table without the 480p row is refused");

  auto short_table = make_table(elf_rows);
  short_table.resize(8 * crt_table_row_bytes);
  bool refused = false;
  try {
    gt4::display::rewrite_table_to_480p(short_table.data(), short_table.size());
  } catch (const std::runtime_error &) {
    refused = true;
  }
  require(refused, "a truncated table is refused");
}

} // namespace

int main() {
  try {
    test_boot_path_reaches_480p();
    test_refusals_leave_table_unchanged();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "display table fixture FAIL: %s\n", error.what());
    return 1;
  }
  std::puts("display table fixture PASS");
  return 0;
}
