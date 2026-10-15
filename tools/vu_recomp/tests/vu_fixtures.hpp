// Authored VU1 test programs as literal instruction words. Each instruction is
// a lower word at pc and an upper word at pc+4, both little-endian, as the
// donor interpreter reads them. Images are 16384 bytes, like recorded corpus files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::vurecomp::fixtures {

struct Fixture {
    std::string name;
    std::vector<std::uint8_t> image;
    std::vector<std::uint32_t> entries;  // recorded entries written to the corpus
    bool expect_refused = false;         // true when the emitter must refuse it
};

// The fixture set. Entries are chosen so that no jump-target derivation lands
// outside the authored code, except where a test expects it.
std::vector<Fixture> build_fixtures();

// Lower and upper word encoders used by the fixtures.
std::uint32_t lower_iaddiu(unsigned it, unsigned is, std::uint32_t imm15);
std::uint32_t lower_branch(std::uint32_t opcode, unsigned it, unsigned is, std::int32_t offset_in_instructions);
std::uint32_t lower_unknown();
std::uint32_t upper_vector(std::uint32_t funct, unsigned dest, unsigned fd, unsigned fs, unsigned ft, bool end_bit);
constexpr std::uint32_t kUpperVnop = 0x0000033Cu;

}
