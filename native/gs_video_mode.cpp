// SetGsCrt to SMODE1/SMODE2 mapping (TASK-012, slice 162 step 4). See gs_video_mode.hpp.
//
// Evidence for each row (labels follow docs/PARALLEL-GS-PLAN.md):
// - Owner logs (private/manual-play/session-*/probe.log, 2026-10-13 to 2026-10-15, 111 SetGsCrt
//   lines): "interlace 1 mode 0x2 field 1" (54 lines, NTSC 480i), "interlace 0 mode 0x50 field 1"
//   (51 lines, 480p after the GT4_FORCE_480P rewrite), "interlace 1 mode 0x51 field 1" (2 lines, 1080i).
//   Confirmed: these three rows reproduce those requests.
// - paraLLEl-GS scanout branches (gs/gs_renderer.cpp, scanout section): NTSC and PAL need
//   LC_ANALOG; CMOD_PROGRESSIVE with LC_HDTV selects 1920x540 when INT=1 and 1280x720 when
//   INT=0; CMOD_PROGRESSIVE with LC_VGA selects 640x480; CMOD_PROGRESSIVE with LC_ANALOG and
//   INT=0 selects the NTSC-timed progressive frame. Confirmed from source.
// - Mode numbers other than 0x02, 0x50, 0x51 and the VGA range, and the VGA range itself, are
//   [Hyp]: taken from the plan's table and not yet checked against a PS2 SDK header or a log.
// - SMODE2 FFMD = the SetGsCrt field argument (slice 167, High confidence; corrected from the slice 162
//   guess, which had it inverted). The third SetGsCrt argument is the SDK's FFMD selector (0 = field,
//   1 = frame), and GT4 itself writes SMODE2 = 0x3 (INT=1, FFMD=1) from vblank 5 after requesting
//   "interlace 1 mode 0x2 field 1" (Confirmed, private/slice163-parallel-smoke/parallel/game-regs.log).

#include "gs_video_mode.hpp"

namespace gt4::gs {
namespace {

constexpr std::uint32_t kVgaModeFirst = 0x1A;
constexpr std::uint32_t kVgaModeLast = 0x3B;

enum class Interlace : std::uint32_t { any, progressive_only, interlaced_only };

struct ModeRow {
    std::uint32_t mode;
    std::uint64_t cmod;
    std::uint64_t lc;
    Interlace interlace;
};

// Modes handled by an explicit row. VGA is a range and is handled separately.
constexpr ModeRow kRows[] = {
    {0x02, kCmodNtsc, kLcAnalog, Interlace::any},            // NTSC, interlaced or not
    {0x03, kCmodPal, kLcAnalog, Interlace::any},             // PAL
    {0x50, kCmodProgressive, kLcAnalog, Interlace::progressive_only}, // DTV 480p
    {0x51, kCmodProgressive, kLcHdtv, Interlace::interlaced_only},    // DTV 1080i (1920x540 fields)
    {0x52, kCmodProgressive, kLcHdtv, Interlace::progressive_only},   // DTV 720p
};

bool is_vga_mode(std::uint32_t mode) { return mode >= kVgaModeFirst && mode <= kVgaModeLast; }

bool interlace_allowed(Interlace rule, std::uint32_t interlace) {
    switch (rule) {
    case Interlace::any: return true;
    case Interlace::progressive_only: return interlace == 0;
    case Interlace::interlaced_only: return interlace == 1;
    }
    return false;
}

VideoRegisters encode(std::uint64_t cmod, std::uint64_t lc, const CrtRequest &request) {
    VideoRegisters registers;
    registers.smode1 = (cmod << kSmode1CmodShift) | (lc << kSmode1LcShift);
    std::uint64_t smode2 = 0;
    if (request.interlace == 1) {
        smode2 |= kSmode2IntBit;
        if (request.field == 1)
            smode2 |= kSmode2FfmdBit; // frame mode (FFMD = 1); field 0 keeps field mode (FFMD = 0)
    }
    registers.smode2 = smode2;
    return registers;
}

} // namespace

CrtMapping map_set_gs_crt(const CrtRequest &request) {
    CrtMapping result;
    if (request.field > 1) {
        result.status = CrtMappingStatus::invalid_field;
        return result;
    }
    if (request.interlace > 1) {
        result.status = CrtMappingStatus::unsupported_combination;
        return result;
    }
    if (is_vga_mode(request.mode)) {
        // VESA/VGA timing: progressive only, CMOD progressive, LC_VGA.
        if (request.interlace != 0) {
            result.status = CrtMappingStatus::unsupported_combination;
            return result;
        }
        result.registers = encode(kCmodProgressive, kLcVga, request);
        result.status = CrtMappingStatus::ok;
        return result;
    }
    for (const ModeRow &row : kRows) {
        if (row.mode != request.mode)
            continue;
        if (!interlace_allowed(row.interlace, request.interlace)) {
            result.status = CrtMappingStatus::unsupported_combination;
            return result;
        }
        result.registers = encode(row.cmod, row.lc, request);
        result.status = CrtMappingStatus::ok;
        return result;
    }
    result.status = CrtMappingStatus::unknown_mode;
    return result;
}

const char *describe(CrtMappingStatus status) {
    switch (status) {
    case CrtMappingStatus::ok: return "ok";
    case CrtMappingStatus::unknown_mode: return "unknown SetGsCrt mode (no scanout is synthesised)";
    case CrtMappingStatus::unsupported_combination: return "mode does not support this interlace setting";
    case CrtMappingStatus::invalid_field: return "field must be 0 or 1";
    }
    return "unrecognised status";
}

} // namespace gt4::gs
