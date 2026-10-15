// F10 request for a PNG of the presented paraLLEl-GS scanout (TASK-012, slice 188).
//
// The window thread (native/window.cpp) sets the flag; Backend::present() (native/parallel_gs_backend.cpp)
// consumes it on the next presented frame. Header-only and inline, so the donor build links without the
// paraLLEl-GS library. With the donor renderer nothing consumes the flag.
#pragma once

#include <atomic>

namespace gt4 {

inline std::atomic<bool> &presented_scanout_dump_requested() {
    static std::atomic<bool> requested{false};
    return requested;
}

} // namespace gt4
