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

// Per-unit emitter profile (slice 160). Unit 1 is the VU1 profile of slices 151-157: its
// generated output, manifest and identity are byte-identical to those slices. Unit 0 is the
// VU0 profile: 4 KiB code and data, EE-visible MAC, status and clip exact at every write
// (no elision), an exact interpreter hand-off (ctx.exact_run, which runs run(0, pc)) and no XGKICK.
struct UnitProfile {
    int unit = 1;
    std::uint32_t image_bytes = 16384;     // code memory and input image size
    std::uint32_t uploaded_bytes = 16384;  // reachable code at or beyond this is refused
    std::uint32_t data_qwords = 1024;      // data memory in qwords; the qword index mask is data_qwords - 1
    bool xgkick_allowed = true;
    bool exact_flags = false;              // MAC, status and clip kept at every write; image flags forced on
    std::string file_prefix = "vu1_generated";  // table vu1_generated.cpp, parts vu1_generated_NNN.cpp
    std::string function_prefix = "vu1_";  // emitted function names: prefix + key
    std::string identity_tag = "GT4-VU1-PROGRAM-IDENTITY-2";
    std::uint32_t code_mask() const { return image_bytes - 1u; }
};

// Profile of unit 0 or 1. Any other unit is a programming error (the caller checks first).
const UnitProfile &unit_profile(int unit);

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
    int unit = 1;                    // emitter profile (unit_profile): 1 for VU1, 0 for VU0
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

// Reads exactly image_bytes bytes (default kImageBytes). Fails on any other size or short read.
bool read_input_image(const std::filesystem::path &path, std::vector<std::uint8_t> &image, std::string &error,
                      std::size_t image_bytes = kImageBytes);

// Identity over entry and the reachable instruction words, as lowercase SHA-256 hex. The tag
// separates the VU0 and VU1 identity namespaces.
std::string program_digest(const std::vector<std::uint8_t> &image, const std::vector<std::uint32_t> &reachable, std::uint32_t entry, bool flags_used, bool status_used,
                           const std::string &tag = "GT4-VU1-PROGRAM-IDENTITY-2");

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

// One part of a split table: a translation unit with the function bodies of up to
// programs_per_file emitted programs.
struct GeneratedPart {
    std::string file_name;  // vu1_generated_NNN.cpp
    std::string text;
    std::size_t programs = 0;
};

// Split output (slice 157): the returned text is the table file vu1_generated.cpp (forward
// declarations of every emitted function and the kPrograms table). Parts are appended to
// `parts` in record order, programs_per_file functions each, so Ninja can compile them in
// parallel. Deterministic for the same records.
std::string emit_generated_split(std::vector<ProgramRecord> &records, std::size_t programs_per_file, std::vector<GeneratedPart> &parts,
                                 const std::string &name_space = "gt4::vugen::generated");

// Explicit-entry mode (slice 155): one static image and a list of entry addresses,
// instead of recorded KEY_ENTRY.bin files. Reads one 4-digit uppercase hex address per
// line; blank lines and lines starting with '#' are ignored. Each address must be
// 8-byte aligned and below 0x4000. Duplicate addresses are read once.
bool read_entry_list(const std::filesystem::path &path, std::vector<std::uint32_t> &entries, std::string &error, std::uint32_t code_mask = kCodeMask);

// One record per distinct entry of the image, in key order. No jump-target discovery is
// done: the list is the complete entry set. Same collision rule as collect_program_records.
// The profile gives the unit (VU0 or VU1 image size, identity tag and flag policy).
bool collect_explicit_program_records(const std::vector<std::uint8_t> &image, const std::vector<std::uint32_t> &entries,
                                      std::vector<ProgramRecord> &records, std::string &error, const UnitProfile &profile = unit_profile(1));

// Describes the input of a manifest. Corpus mode leaves the explicit fields empty.
struct ManifestInput {
    std::string kind = "corpus";           // "corpus" (KEY_ENTRY.bin files) or "explicit_entries"
    int unit = 1;                          // 1 leaves the manifest unchanged; other units add "unit" and "identity_tag"
    std::string image_sha256;              // explicit mode: SHA-256 of the 16 KiB image
    std::string entry_list_sha256;         // explicit mode: SHA-256 of the entry list file
    std::size_t generated_bytes = 0;       // size of the generated source (all files)
    std::string entry_policy = "unspecified";  // label of the entry list, e.g. "all" or "targeted" (vu1_image.py)
    std::size_t programs_per_file = 0;     // 0: one translation unit; otherwise the split of emit_generated_split
    std::vector<GeneratedPart> parts;      // split mode: the part files, recorded with their hashes
};

// Writes the JSON manifest describing every record, with entry, function and size counts.
std::string manifest_json(const std::vector<ProgramRecord> &records, const std::string &generated_file_name,
                          const std::string &generated_sha256, const ManifestInput &input = {});

}
