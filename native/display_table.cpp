#include "display_table.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "display_override.hpp"

namespace gt4::display {
namespace {

struct Row {
  std::uint32_t id;
  std::uint32_t interlace;
  std::uint32_t mode;
  std::uint32_t field;
};

std::uint32_t read_le32(const std::uint8_t *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void write_le32(std::uint8_t *bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

Row read_row(const std::uint8_t *row) {
  return {read_le32(row), read_le32(row + 4), read_le32(row + 8), read_le32(row + 12)};
}

void write_values(std::uint8_t *row, std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {
  write_le32(row + 4, interlace);
  write_le32(row + 8, mode);
  write_le32(row + 12, field);
}

bool has_values(const Row &row, std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {
  return row.interlace == interlace && row.mode == mode && row.field == field;
}

std::string describe(const Row &row) {
  char text[96];
  std::snprintf(text, sizeof text, "{id %u, interlace %u, mode 0x%X, field %u}", row.id, row.interlace,
                row.mode, row.field);
  return text;
}

} // namespace

void rewrite_table_to_480p(std::uint8_t *table, std::size_t table_bytes) {
  if (table_bytes < crt_table_stored_rows * crt_table_row_bytes)
    throw std::runtime_error("CRT table does not fit in guest RAM");

  // Locate row id 1 among the search rows, exactly as the guest lookup scans them.
  std::size_t ntsc_index = crt_table_search_rows;
  bool has_480p_row = false;
  for (std::size_t index = 0; index < crt_table_search_rows; ++index) {
    const Row row = read_row(table + index * crt_table_row_bytes);
    if (row.id == ntsc_row_id)
      ntsc_index = index;
    if (row.id == progressive_480p_row_id && has_values(row, progressive_480p_interlace,
                                                        progressive_480p_mode, progressive_480p_field))
      has_480p_row = true;
  }
  if (ntsc_index == crt_table_search_rows)
    throw std::runtime_error("CRT table has no row with id 1");
  const Row ntsc = read_row(table + ntsc_index * crt_table_row_bytes);
  if (!has_values(ntsc, default_ntsc_interlace, default_ntsc_mode, ntsc_field))
    throw std::runtime_error("CRT table row id 1 is " + describe(ntsc) + ", expected 480i");

  const Row fallback = read_row(table + crt_table_fallback_index * crt_table_row_bytes);
  if (fallback.id != 0 || !has_values(fallback, default_ntsc_interlace, default_ntsc_mode, ntsc_field))
    throw std::runtime_error("CRT fallback row is " + describe(fallback) + ", expected {id 0, 480i}");
  if (!has_480p_row)
    throw std::runtime_error("CRT table has no 480p progressive row (id 6)");

  write_values(table + ntsc_index * crt_table_row_bytes, progressive_480p_interlace, progressive_480p_mode,
               progressive_480p_field);
  write_values(table + crt_table_fallback_index * crt_table_row_bytes, progressive_480p_interlace,
               progressive_480p_mode, progressive_480p_field);
}

void apply_force_480p_table(std::uint8_t *ram, std::uint32_t ram_size) {
  bool force_480p = false;
  try {
    force_480p = parse_force_480p(std::getenv("GT4_FORCE_480P"));
  } catch (const std::invalid_argument &error) {
    std::fprintf(stderr, "[display] %s\n", error.what());
    std::exit(2);
  }
  if (!force_480p)
    return;
  const std::size_t table_bytes = crt_table_stored_rows * crt_table_row_bytes;
  if (crt_table_address + table_bytes > ram_size) {
    std::fprintf(stderr, "[display] GT4_FORCE_480P refused: CRT table is outside guest RAM\n");
    std::exit(2);
  }
  try {
    rewrite_table_to_480p(ram + crt_table_address, table_bytes);
  } catch (const std::runtime_error &error) {
    std::fprintf(stderr, "[display] GT4_FORCE_480P refused: %s\n", error.what());
    std::exit(2);
  }
  std::fprintf(stderr,
               "[display] GT4_FORCE_480P=1: CRT table rows id 1 and fallback set to interlace %u mode 0x%X "
               "field %u\n",
               progressive_480p_interlace, progressive_480p_mode, progressive_480p_field);
}

} // namespace gt4::display
