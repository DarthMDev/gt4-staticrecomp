#pragma once
// TASK-086 step 1 (slice232, decision 0072): progressive 480p menus and races, switchable at run time.
//
// The game asks for a video mode through func_001074D0(state, psm, row, w, h). Menus ask through
// MOption.applyMenuDisplayMode (func_00436740) for (psm 1 = CT24, row 0 = the default row 1 = 480i field, 640, 448)
// when they come back from a scene (race, replay, demo, movie); the race "normal" display index asks for the same tuple
// through func_00436640. The 480p "Try..." pop-up of the Screen options asks for (psm 2 = CT16, row 6 = 480p, 640, 448).
// The menus after boot keep the boot mode (func_00107730 -> func_001062E8 -> func_001063A8: 480i field layout, CRT row
// 1) until the first scene.
//
// The EE library profile "progressive" (scripts/aspect_elf_patch.py PROGRESSIVE_PATCHES and BOOT_REWRITES) makes these
// read the words below, so the host decides at run time:
//   GT4_PROGRESSIVE=1 (the default): menus and the normal race index ask for (2, 6), the Try pop-up's mode, with the
//     same VRAM footprint as the 480i menus. The first race switches the CRT to 480p; from then on menu and race
//     requests are equal, so the CRT never changes between menus and races (no 1-second black). The race monitor's
//     explicit 480i field request (0x003DA488/0x003DA490) asks for row 6 too. The two UI passes that halve their
//     rectangle for 224-line field buffers (func_00218170, the colour window backdrop, and
//     mRenderContextPS2__virtual_67, the capture pass) use the full-frame values.
//   GT4_PROGRESSIVE=0: the original constants; the game runs the original code.
//   GT4_PROGRESSIVE_BOOT=1 (experimental, off by default; TASK-086 step 2): the boot builds the 480p frame layout too
//     (func_00106DA8(state, 2, 0x50, -1, -1), and the display mode object's mode 2 takes its 480p case). Known problem:
//     the boot then stops drawing (headless: the main game object waits in GameObjectBase::sync from
//     MenuGameObject::startProject); the launcher never sets it.
// Photo pause, movies and photo develop keep their own modes (TASK-086 step 2).
//
// The words sit after the TASK-080 HUD parameter block (native/display_aspect.hpp, 0x7F00..0x7F07) in guest kernel RAM
// that neither the game nor our HLE kernel uses; the patched code reads them as `lw rt, offset(zero)`. The host writes
// them at every guest VBlank when they differ. A zero boot layout word (also: not filled yet) is the original boot.

#include <cstdint>

namespace gt4::progressive {

inline constexpr std::uint32_t kBlockBase = 0x00007F00u;
inline constexpr std::uint32_t kProgressiveBlock = 0x00007F08u;
inline constexpr std::uint32_t kProgressiveWords = 6u;
// Offsets from the TASK-080 block base 0x7F00, as in the patch table (block_offset).
inline constexpr std::uint32_t kMenuPsmOffset = 0x08u;           // a1 of func_001074D0 for menus and the normal race
inline constexpr std::uint32_t kMenuRowOffset = 0x0Cu;           // a2: CRT table row (0 = default row 1)
inline constexpr std::uint32_t kFieldHalvingOffset = 0x10u;      // f32 factor of the two UI field halvings
inline constexpr std::uint32_t kDrawBackViewportOffset = 0x14u;  // func_004A2A10 type of the capture pass draw-back
inline constexpr std::uint32_t kBootLayoutOffset = 0x18u;        // func_001062E8, func_00100A40 mode 2: 0 field, 1 480p
inline constexpr std::uint32_t kMonitorRowOffset = 0x1Cu;        // the race monitor's explicit CRT row 1 request

// The original constants (GT4_PROGRESSIVE=0) and the progressive values (GT4_PROGRESSIVE=1).
inline constexpr std::uint32_t kOriginalMenuPsm = 1u;                // CT24
inline constexpr std::uint32_t kOriginalMenuRow = 0u;                // default row (row 1, 480i field)
inline constexpr std::uint32_t kOriginalFieldHalving = 0x3F000000u;  // 0.5f
inline constexpr std::uint32_t kOriginalDrawBackViewport = 1u;       // half-height field viewport
inline constexpr std::uint32_t kOriginalBootLayout = 0u;             // the boot's own field layout (also: not filled)
inline constexpr std::uint32_t kOriginalMonitorRow = 1u;             // CRT row 1 (480i field)
inline constexpr std::uint32_t kProgressiveMenuPsm = 2u;             // CT16, as the 480p Try pop-up
inline constexpr std::uint32_t kProgressiveMenuRow = 6u;             // CRT row 6: 480p (interlace 0, mode 0x50)
inline constexpr std::uint32_t kProgressiveFieldHalving = 0x3F800000u;  // 1.0f
inline constexpr std::uint32_t kProgressiveDrawBackViewport = 0u;       // full-frame viewport
inline constexpr std::uint32_t kProgressiveBootLayout = 1u;             // func_00106DA8(state, 2, 0x50, -1, -1)
inline constexpr std::uint32_t kProgressiveMonitorRow = 6u;             // CRT row 6 (480p)

struct Block {
  std::uint32_t words[kProgressiveWords] = {};
  bool operator==(const Block &other) const {
    for (std::uint32_t i = 0; i < kProgressiveWords; ++i)
      if (words[i] != other.words[i])
        return false;
    return true;
  }
};

// GT4_PROGRESSIVE: nullptr (unset) or "1" is on, "0" is off. Any other value throws std::invalid_argument.
bool parse_progressive(const char *value);

// GT4_PROGRESSIVE_BOOT (experimental, TASK-086 step 2): nullptr (unset) or "0" is off, "1" is on. Any other value
// throws std::invalid_argument.
bool parse_progressive_boot(const char *value);

// The words for the switch. The boot layout word is 1 only when both switches are on. Pure.
Block block_for(bool progressive, bool boot = false);

// Index of a block offset (kMenuPsmOffset ...) in Block::words.
std::uint32_t word_index(std::uint32_t offset);

// Writes the words into guest RAM when they differ. Returns true when words were written; false when unchanged or
// when the block is not inside `ram_size`.
bool write_block(std::uint8_t *ram, std::uint32_t ram_size, const Block &block);

// Whether this executable links the EE library profile "progressive" (CMake defines GT4_PROGRESSIVE_LIBRARY). Without
// it the words have no reader, so the host writes nothing.
bool library_has_table();

// --- Runtime glue (EE thread) ---------------------------------------------------------------------------------------

// Reads GT4_PROGRESSIVE once. An invalid value prints the refusal and exits with status 1 (called at window start, and
// again at the first VBlank so headless runs refuse too).
bool configured_progressive();
bool configured_progressive_boot();

// At every guest VBlank (native/mtvu_runtime.cpp vblank_sync): fills the words when the library has the table; logs
// the first fill and refills.
void at_guest_vblank(std::uint8_t *ram, std::uint32_t ram_size);

}  // namespace gt4::progressive
