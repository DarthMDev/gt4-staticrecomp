// Hardened VU1 offline emitter derived from the MIT ps2-web-recomp donor (MIT-LICENSE.txt; attribution in native/THIRD_PARTY.md).
#pragma once

#include "program_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gt4::vurecomp {

constexpr std::size_t kImageBytes = 16384;

// Flag elision (default on). When on, a program computes MAC and clip only where a
// later reader or the program exit observes them, and VU1 status is not kept when
// no image instruction reads it (see FunctionGenerator in vurecomp.cpp). Off emits
// the donor behaviour for comparison builds. The setting is also part of the
// generated manifest.
void set_flag_elision(bool enabled);
bool flag_elision();
constexpr std::uint32_t kCodeMask = 16383u;

// Mirrors the donor interpreter's scan_flags over the whole image: when any
// status or MAC reader (lower 0x14-0x18, 0x1A, 0x1B) exists anywhere in the 16 KiB
// image, every flag-setting instruction writes MAC and status. This is image-wide,
// not reachable-only, so it is part of a program's identity.
inline bool image_flags_used(const std::uint8_t *code, std::uint32_t size) {
    for (std::uint32_t pc = 0; pc + 8 <= size; pc += 8) {
        const std::uint32_t lower = static_cast<std::uint32_t>(code[pc]) | (static_cast<std::uint32_t>(code[pc + 1]) << 8) |
                                    (static_cast<std::uint32_t>(code[pc + 2]) << 16) | (static_cast<std::uint32_t>(code[pc + 3]) << 24);
        const std::uint32_t upper = static_cast<std::uint32_t>(code[pc + 4]) | (static_cast<std::uint32_t>(code[pc + 5]) << 8) |
                                    (static_cast<std::uint32_t>(code[pc + 6]) << 16) | (static_cast<std::uint32_t>(code[pc + 7]) << 24);
        if (upper & 0x80000000u) continue;
        switch (lower >> 25) {
        case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x1A: case 0x1B: return true;
        default: break;
        }
    }
    return false;
}

// One emitted or refused program. Refused programs are recorded in the
// manifest but produce no function.
struct ProgramRecord {
    std::uint64_t key = 0;
    std::uint32_t entry = 0;
    std::string origin;              // "recorded" or "jump_target"
    std::string source_name;         // input file name the code came from
    std::string source_sha256;       // SHA-256 of the 16 KiB input file
    std::string digest_sha256;       // identity: flag bit, entry, reachable instruction words
    bool image_flags_used = false;   // image-wide flag reader present (see image_flags_used)
    bool image_status_used = false;  // image-wide status instruction present (see image_status_used)
    std::string mac_mode;            // "none", "full" or "observed" (see MacMode)
    std::size_t interpreter_calls = 0; // emitted hand-offs to ps2::vu::interpret
    std::size_t mac_writers = 0;     // flag-setting writes in the reachable code
    std::size_t mac_sites = 0;       // writes that still compute a MAC value
    std::size_t mac_reader_sites = 0; // writes pushed to the FlagPipe for a MAC reader
    std::size_t clip_writers = 0;    // clip writes in the reachable code
    std::size_t clip_sites = 0;      // clip writes that still compute a value
    std::size_t clip_reader_sites = 0; // clip writes pushed to the FlagPipe for a clip reader
    std::vector<std::uint32_t> reachable;
    std::vector<std::uint8_t> image; // 16 KiB code image used for generation
    std::size_t fallback_sites = 0;  // lower encodings that exit to the interpreter
    std::size_t upper_nop_sites = 0; // unknown upper encodings emitted as the donor no-op
    std::vector<std::uint32_t> exit_pcs;
    std::size_t budget_heads = 0;    // backward-transfer targets that charge the budget
    bool refused = false;
    std::string refusal;             // reason when refused
    std::string function_name;       // empty when refused
};

// Parses KEY16HEX_ENTRY4HEX.bin, for example 0123456789ABCDEF_0040.bin.
bool parse_input_file_name(const std::string &name, std::uint64_t &key, std::uint32_t &entry, std::string &error);

// Reads exactly kImageBytes bytes. Fails on any other size or short read.
bool read_input_image(const std::filesystem::path &path, std::vector<std::uint8_t> &image, std::string &error);

// Identity over entry and the reachable instruction words, as lowercase SHA-256 hex.
std::string program_digest(const std::vector<std::uint8_t> &image, const std::vector<std::uint32_t> &reachable, std::uint32_t entry, bool flags_used, bool status_used);

// Adds a candidate. An equal key with an equal digest is a duplicate and is
// skipped. An equal key with a different digest is a collision and is refused.
bool add_program_record(std::vector<ProgramRecord> &records, ProgramRecord candidate, std::string &error);

// Loads every input file, validates it, then appends jump-target programs
// discovered from recorded code. Files are processed in sorted name order.
bool collect_program_records(const std::filesystem::path &input_dir, std::vector<ProgramRecord> &records, std::string &error);

// Fails when the directory exists and is not empty. Creates it otherwise.
// Never deletes anything.
bool prepare_fresh_output_directory(const std::filesystem::path &directory, std::string &error);

// Emits the function for one record. Sets refused, fallback_sites, budget_heads
// and function_name.
std::string emit_program_function(ProgramRecord &record);

// Emits all records into one translation unit with a table of emitted programs.
std::string emit_generated_source(std::vector<ProgramRecord> &records, const std::string &name_space = "gt4::vugen::generated");

// Writes the JSON manifest describing every record.
std::string manifest_json(const std::vector<ProgramRecord> &records, const std::string &generated_file_name,
                          const std::string &generated_sha256);

}
