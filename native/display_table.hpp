#pragma once

// Slice158: 480p through the guest CRT mode table, applied to guest RAM after the ELF is loaded.
//
// The guest table D_00618648 (ELF .data at 0x618648) holds 16-byte rows of four little-endian
// words {id, interlace, mode, field}. Two routines read it:
//   func_001060A0(interlace, mode, field): scans the first 8 rows for matching values and returns
//                                          the row id, or 0 when no row matches.
//   func_00106058(id):                     scans the first 8 rows for the id and returns that row;
//                                          when none matches it returns row 8 (the fallback row).
// The boot path requests (1, 2, 1) (480i), stores the returned id, and fetches the row by that id.
// The row values then feed SetGsCrt and the game's display state.
//
// Rewriting the values of row id 1 alone is not enough: the request would not match and the id
// would be 0, which fetches the fallback row, still 480i. The fallback row is therefore rewritten
// to the same 480p values, so every request for 480i, and every unlisted request, reaches 480p.

#include <cstddef>
#include <cstdint>

#include "display_override.hpp"

namespace gt4::display {

// Guest address of the table (ELF vaddr; the donor loader places it at this RAM offset).
inline constexpr std::uint32_t crt_table_address = 0x618648u;
inline constexpr std::size_t crt_table_search_rows = 8;
inline constexpr std::size_t crt_table_row_bytes = 16;
inline constexpr std::size_t crt_table_fallback_index = crt_table_search_rows;
// Search rows plus the fallback row.
inline constexpr std::size_t crt_table_stored_rows = crt_table_search_rows + 1;

// Values come from display_override.hpp: default_ntsc_interlace/mode and progressive_480p_interlace/mode.
inline constexpr std::uint32_t ntsc_row_id = 1;
inline constexpr std::uint32_t ntsc_field = 1;
inline constexpr std::uint32_t progressive_480p_row_id = 6;
inline constexpr std::uint32_t progressive_480p_field = 1;

// Verifies the qualified rows and rewrites row id 1 and the fallback row to 480p progressive.
// `table` points at crt_table_address in guest RAM and holds crt_table_stored_rows rows.
// Throws std::runtime_error, without writing, when a row differs from the qualified values or
// the 480p row (id 6) is not present.
void rewrite_table_to_480p(std::uint8_t *table, std::size_t table_bytes);

// GT4_FORCE_480P=1 (--force-480p): applies rewrite_table_to_480p to guest RAM and prints one line.
// Unset or "0" does nothing. Any other value, a table that is outside RAM, or a table that does not
// match the qualified values prints the reason and exits with status 2 before the guest runs.
void apply_force_480p_table(std::uint8_t *ram, std::uint32_t ram_size);

} // namespace gt4::display
