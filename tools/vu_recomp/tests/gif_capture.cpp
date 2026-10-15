// Replacement definitions for the public ps2::gif API, used by the VU
// differential tests only. The donor gif.cpp is never linked into these tests,
// because every ps2::gif symbol the runtime references is defined here.
// Only path 1 (XGKICK) is recorded; the VU tests issue no other GIF traffic.

#include "gif_capture.hpp"

#include "gs/gif.hpp"

#include <cstring>

namespace {

std::vector<std::uint8_t> g_kick_bytes;

}

namespace gt4::vutest {

std::vector<std::uint8_t> &kick_bytes() { return g_kick_bytes; }
void reset_kick_capture() { g_kick_bytes.clear(); }

}

namespace ps2::gif {

void state(ps2::state::Stream &) {}
void reset() {}
void stats() {}
void render_reset() {}
void render(int, const std::uint8_t *, std::uint32_t) {}
void scan_signals(int, const std::uint8_t *, std::uint32_t) {}
bool busy(int) { return false; }
std::uint32_t read(std::uint32_t) { return 0; }
void write(std::uint32_t, std::uint32_t) {}

void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    if (path != 1) return;
    const std::size_t old_size = gt4::vutest::kick_bytes().size();
    gt4::vutest::kick_bytes().resize(old_size + static_cast<std::size_t>(qwc) * 16u);
    std::memcpy(gt4::vutest::kick_bytes().data() + old_size, data, static_cast<std::size_t>(qwc) * 16u);
}

}
