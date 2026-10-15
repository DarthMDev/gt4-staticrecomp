// Test seam for XGKICK. The donor runtime delivers XGKICK data through
// ps2::gif::transfer(path 1, ...). The differential tests provide that symbol
// (see gif_capture.cpp), so the captured bytes can be compared between runs.
#pragma once

#include <cstdint>
#include <vector>

namespace gt4::vutest {

// Bytes delivered on GIF path 1 (XGKICK) since the last reset.
std::vector<std::uint8_t> &kick_bytes();
void reset_kick_capture();

}
