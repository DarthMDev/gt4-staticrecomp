#pragma once

// Experimental 480p switch (GT4_FORCE_480P=1). The guest's default NTSC request
// (SetGsCrt interlace 1, mode 0x2, 480i) is rewritten to 480p progressive
// (interlace 0, mode 0x50). Every other request, including 480p, 1080i and 720p,
// is passed through unchanged. The guest overlay calls apply_crt_override from
// the donor gs_set_crt in the slice156 runtime capsule.

#include <cstdint>

namespace gt4::display {

struct CrtRequest {
  std::uint32_t interlace;
  std::uint32_t mode;
};

inline constexpr std::uint32_t default_ntsc_interlace = 1;
inline constexpr std::uint32_t default_ntsc_mode = 0x2;
inline constexpr std::uint32_t progressive_480p_interlace = 0;
inline constexpr std::uint32_t progressive_480p_mode = 0x50;

// GT4_FORCE_480P: unset or "0" disables, "1" enables. Any other value throws
// std::invalid_argument.
bool parse_force_480p(const char *value);

// Pure rewrite: only the default NTSC 480i request changes, and only when forced.
CrtRequest rewrite_crt_request(CrtRequest request, bool force_480p);

// Reads GT4_FORCE_480P, applies the rewrite and prints one line when it changes
// the request. Called from the guest overlay for every SetGsCrt request.
CrtRequest apply_crt_override(CrtRequest request);

// Throws std::invalid_argument for an unknown GT4_FORCE_480P; called at startup.
void validate_environment();

} // namespace gt4::display
