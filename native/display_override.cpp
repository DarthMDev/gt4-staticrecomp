#include "display_override.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace gt4::display {

bool parse_force_480p(const char *value) {
  if (!value || std::strcmp(value, "0") == 0)
    return false;
  if (std::strcmp(value, "1") == 0)
    return true;
  throw std::invalid_argument("GT4_FORCE_480P must be 0 or 1");
}

CrtRequest rewrite_crt_request(CrtRequest request, bool force_480p) {
  const bool default_ntsc = request.interlace == default_ntsc_interlace &&
                            request.mode == default_ntsc_mode;
  if (force_480p && default_ntsc)
    return {progressive_480p_interlace, progressive_480p_mode};
  return request;
}

void validate_environment() {
  if (parse_force_480p(std::getenv("GT4_FORCE_480P")))
    std::fprintf(stderr, "[display] GT4_FORCE_480P=1: experimental 480p rewrite enabled\n");
}

CrtRequest apply_crt_override(CrtRequest request) {
  const bool force_480p = parse_force_480p(std::getenv("GT4_FORCE_480P"));
  const CrtRequest rewritten = rewrite_crt_request(request, force_480p);
  if (rewritten.interlace != request.interlace || rewritten.mode != request.mode)
    std::fprintf(stderr,
                 "[display] GT4_FORCE_480P: SetGsCrt interlace %u mode 0x%X "
                 "rewritten to interlace %u mode 0x%X (experimental)\n",
                 request.interlace, request.mode, rewritten.interlace,
                 rewritten.mode);
  return rewritten;
}

} // namespace gt4::display
