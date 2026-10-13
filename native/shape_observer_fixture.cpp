#include "shape_observer.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace gt4::observation;

namespace {
void require(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}

Inputs inputs(std::uint32_t shape, std::uint32_t sp = 0x00101000) {
  Inputs value;
  value.argument_bits[0] = {shape, 0x11223344, 0x55667788, 0x99AABBCC};
  value.argument_bits[1] = {0xDEAD, 1, 2, 3};
  value.fpr12_bits = 0x7FC12345;
  value.guest_sp = sp;
  value.metadata.valid_fields = all_fields_valid;
  value.metadata.cursor = 0x1000;
  return value;
}

void memory_controls() {
  std::array<std::byte, 64> ram{};
  std::array<std::byte, 0x4000> scratch{};
  const MemoryView memory{ram, scratch};
  const auto original_ram = ram;
  std::array<std::byte, 4> output{};
  ram[63] = std::byte{0xAA};
  require(copy_guest(memory, 60, output) && output[3] == std::byte{0xAA}, "last host RAM word");
  const auto saved_output = output;
  for (auto address : {61u, 0x01FFFFFDu, 0x02000000u, 0x10000000u,
                       0x70003FFDu, 0x70004000u, 0x80000000u, 0xFFFFFFFFu}) {
    require(!copy_guest(memory, address, output), "invalid full span/mapping rejected");
    require(output == saved_output, "failed read leaves output untouched");
  }
  require(!copy_guest(memory, 0, {}), "empty read rejected");
  require(copy_guest(memory, 0x70003FFC, output), "scratch boundary word");
  require(copy_guest(memory, 1, output), "unaligned RAM allowed");
  std::uint32_t shape_bits = 0xFF123456;
  std::uint16_t count = 0xABCD;
  std::memcpy(ram.data() + 8, &shape_bits, 4);
  std::memcpy(ram.data() + 14, &count, 2);
  scratch[0x2003] = std::byte{1};
  const std::uint32_t enabled = 7, cached = 0x3F800000, cursor = 0x12345678;
  std::memcpy(scratch.data() + 0x2014, &enabled, 4);
  std::memcpy(scratch.data() + 0x2020, &cached, 4);
  std::memcpy(scratch.data() + 0x21F8, &cursor, 4);
  const auto unchanged_ram = ram;
  const auto unchanged_scratch = scratch;
  const auto sample = sample_metadata(memory, 4);
  require(sample.valid_fields == all_fields_valid && sample.shape_bits == shape_bits &&
              sample.descriptor_count == count && sample.flag == 1 && sample.enabled == 7 &&
              sample.cached_bits == cached && sample.cursor == cursor, "literal fixed fields");
  require(sample_metadata(memory, 0).valid_fields == 15, "null shape rejected before offsets");
  require(sample_metadata(memory, 0xFFFFFFFC).valid_fields == 15, "shape offset overflow");
  require(sample_metadata({ram, {}}, 4).valid_fields == 48, "missing scratch independent validity");
  require(sample_metadata({{}, {}}, 4).valid_fields == 0, "missing host backing rejected");
  require(ram == unchanged_ram && scratch == unchanged_scratch && ram != original_ram,
          "sampling does not mutate memory");
}

struct GuestControl {
  Inputs registers = inputs(0x2000);
  std::uint32_t pc = 0x00455708, guest_counter = 0, shape_executions = 0, budget = 500;
  Metadata state = registers.metadata;
  bool operator==(const GuestControl &) const = default;
};

void authoritative_shape(GuestControl &guest) {
  ++guest.shape_executions;
  guest.registers.argument_bits[0] = {};
  guest.registers.fpr12_bits = 0;
  guest.state.cursor += 144;
  guest.pc = shape_return_pc;
  guest.budget -= 8;
}

void guest_call(GuestControl &guest, ShapeObserver *observer, unsigned depth) {
  if (observer)
    observer->begin(7, false, guest.registers);
  if (depth >= 192) {
    guest.pc = shape_target_pc;
    return;
  }
  authoritative_shape(guest);
  if (guest.pc != shape_return_pc)
    return;
  if (observer)
    observer->complete(7, false, guest.pc, guest.registers.guest_sp, guest.state);
  ++guest.guest_counter;
}

void frontier_controls() {
  ShapeObserver observer;
  GuestControl baseline, observed;
  guest_call(observed, &observer, 0);
  guest_call(baseline, nullptr, 0);
  require(observed == baseline && observer.records().empty() && observer.counters().started == 0,
          "disabled observer inert");
  observer.start();
  baseline = {};
  observed = {};
  const auto original = observed.registers;
  guest_call(baseline, nullptr, 0);
  guest_call(observed, &observer, 0);
  require(observed == baseline && observed.shape_executions == 1, "direct guest results equal");
  require(observer.records().size() == 1 && observer.records()[0].before == original &&
              observer.records()[0].after.cursor == 0x1090, "original bits preserved before clobber");

  observer.start();
  baseline = {};
  observed = {};
  guest_call(baseline, nullptr, 192);
  guest_call(observed, &observer, 192);
  require(observed == baseline && observer.records().empty(), "spill has no early completion");
  observed.pc = shape_target_pc | 1u;
  observer.complete(7, false, observed.pc, original.guest_sp, {});
  observer.complete(7, false, shape_return_pc | 1u, original.guest_sp, {});
  require(observer.records().empty(), "two suspended frontiers are not completion");
  authoritative_shape(baseline);
  authoritative_shape(observed);
  observer.complete(7, false, observed.pc, observed.registers.guest_sp, observed.state);
  ++baseline.guest_counter;
  ++observed.guest_counter;
  require(observed == baseline && observer.records().size() == 1 &&
              observer.records()[0].before == original, "synthetic dispatch resume retains token");
  observer.complete(7, false, shape_return_pc, original.guest_sp, {});
  require(observer.counters().orphan == 1 && observer.records().size() == 1, "consume once");
}

void ownership_controls() {
  ShapeObserver observer;
  observer.start();
  const auto first = inputs(0x100), second = inputs(0x200);
  observer.begin(1, false, first);
  observer.begin(2, false, second);
  observer.begin(1, true, second);
  observer.complete(1, true, shape_return_pc, first.guest_sp, {});
  observer.complete(2, false, shape_return_pc, second.guest_sp, {});
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  require(observer.records().size() == 2 && observer.records()[0].before == second &&
              observer.records()[1].before == first && observer.counters().handler_skipped == 2,
          "thread switch and handler do not exchange tokens");
  const auto incarnation = observer.records()[1].incarnation;
  observer.begin(1, false, first);
  observer.invalidate_owner(1);
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  observer.begin(1, false, second);
  observer.complete(1, false, shape_return_pc, second.guest_sp, {});
  require(observer.records().back().incarnation > incarnation && observer.counters().aborted == 1 &&
              observer.counters().orphan == 1, "reuse invalidation");
  const auto previous_epoch = observer.records().back().epoch;
  observer.begin(2, false, first);
  observer.reset_epoch();
  observer.complete(2, false, shape_return_pc, first.guest_sp, {});
  observer.begin(2, false, first);
  observer.complete(2, false, shape_return_pc, first.guest_sp, {});
  require(observer.records().back().epoch > previous_epoch && observer.counters().orphan == 2,
          "boot/load-attempt invalidation preserves completed history");

  observer.start();
  auto nested = inputs(0x300, first.guest_sp - 0x80);
  observer.begin(1, false, first);
  observer.begin(1, false, nested);
  observer.complete(1, false, shape_return_pc, nested.guest_sp, {});
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  observer.begin(1, false, first);
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  require(observer.records()[0].before == nested && observer.records()[1].before == first &&
              observer.records()[2].sequence > observer.records()[1].sequence, "recursive LIFO and repetition");
  observer.begin(1, false, first);
  observer.begin(1, false, nested);
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  require(!observer.enabled() && observer.counters().aborted == 2, "never steal older SP match");
  observer.start();
  observer.begin(1, false, first);
  observer.begin(1, false, first);
  require(!observer.enabled() && observer.counters().ambiguous_stack == 1, "equal-SP nested activation rejected");
  observer.start();
  observer.begin(1, false, first);
  observer.stop();
  require(observer.counters().aborted == 1, "abandoned pending call counted");
  observer.start();
  observer.complete(1, false, shape_return_pc, first.guest_sp, {});
  require(observer.counters().orphan == 1 && observer.records().empty(), "restart cannot recover old inputs");
  observer.begin(1, false, first);
  observer.complete(1, false, shape_return_pc, first.guest_sp, first.metadata);
  require(observer.records().size() == 1 && observer.records()[0].after == first.metadata,
          "normal early return needs no cursor change or helper emission");
}

void bounds_controls() {
  ShapeObserver observer;
  observer.start();
  auto value = inputs(0x100);
  for (std::size_t index = 0; index <= ShapeObserver::pending_per_owner; ++index) {
    value.guest_sp = 0x10000 - static_cast<std::uint32_t>(index * 128);
    observer.begin(1, false, value);
  }
  require(!observer.enabled() && observer.counters().aborted == 16 &&
              observer.counters().capacity_rejected == 1, "pending cap disables without old-token stealing");
  observer.start();
  for (std::uint32_t owner = 0; owner <= ShapeObserver::owner_limit; ++owner)
    observer.begin(owner, false, value);
  require(!observer.enabled() && observer.counters().aborted == 64, "owner cap bounded");
  observer.start();
  value.metadata = {};
  observer.begin(99, false, value);
  for (std::size_t index = 0; index <= ShapeObserver::record_limit; ++index) {
    observer.begin(1, false, value);
    observer.complete(1, false, shape_return_pc, value.guest_sp, {});
  }
  require(!observer.enabled() && observer.records().size() == 256 &&
              observer.counters().record_dropped == 1 && observer.counters().aborted == 1,
          "record cap preserves records and aborts other owner");
  require(observer.counters().incomplete_samples == 515, "missing metadata counted independently");
  const auto last_sequence = observer.records().back().sequence;
  observer.complete(1, false, shape_return_pc, value.guest_sp, {});
  require(observer.records().back().sequence == last_sequence && observer.records().size() == 256,
          "completion after drop cannot retry");
}
} // namespace

int main() {
  try {
    memory_controls();
    frontier_controls();
    ownership_controls();
    bounds_controls();
    std::puts("PASS: shape observer memory, frontier, ownership and bounds controls");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
