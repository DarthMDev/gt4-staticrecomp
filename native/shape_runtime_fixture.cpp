#include "shape_observer_adapter.hpp"
#include "kernel.hpp"
#include "ps2/generated.hpp"
#include "ps2/memory.hpp"
#include "ps2/state.hpp"
#include "ps2/system.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace gt4::observation;
using namespace ps2;

namespace {
#ifdef GT4_OBSERVER_CONTROL
constexpr bool capturing = false;
#else
constexpr bool capturing = true;
#endif

void require(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}

std::array<std::byte, sizeof(Cpu)> cpu_bytes() {
  std::array<std::byte, sizeof(Cpu)> bytes;
  std::memcpy(bytes.data(), &g_cpu, bytes.size());
  return bytes;
}

void transcript(const char *label) {
  state::Writer writer;
  static_assert(sizeof(Reg128) == 16);
  writer.pod(g_cpu.r);
  writer.pod(g_cpu.hi);
  writer.pod(g_cpu.lo);
  writer.pod(g_cpu.sa);
  writer.pod(g_cpu.pc);
  writer.pod(g_cpu.budget);
  writer.pod(g_cpu.base);
  writer.pod(g_cpu.f);
  writer.pod(g_cpu.facc);
  writer.pod(g_cpu.fcr31);
  writer.pod(g_cpu.vu0.vf);
  writer.pod(g_cpu.vu0.acc);
  writer.pod(g_cpu.vu0.vi);
  writer.pod(g_cpu.vu0.q);
  writer.pod(g_cpu.vu0.p);
  writer.pod(g_cpu.vu0.i);
  writer.pod(g_cpu.vu0.r);
  writer.pod(g_cpu.vu0.mac);
  writer.pod(g_cpu.vu0.status);
  writer.pod(g_cpu.vu0.clip);
  writer.pod(g_cpu.vu0.cmsar0);
  writer.pod(g_cpu.vu0.tpc);
  writer.pod(g_cpu.cop0);
  auto owner = kernel::current_thread();
  auto in_handler = kernel::in_handler();
  writer.pod(owner);
  writer.pod(in_handler);
  // The private Thread POD contains host padding; compare defined public status fields.
  require(load<std::uint32_t>(g_ram + 0x18C0) == 0, "status query cannot invoke a custom guest syscall");
  const auto before = cpu_bytes();
  for (std::uint32_t id : {1u, 2u}) {
    std::array<std::uint8_t, 48> saved, status{};
    std::memcpy(saved.data(), g_ram + 0x1B000, saved.size());
    std::memset(g_ram + 0x1B000, 0, status.size());
    Cpu query{};
    query.r[3].w[0] = 0x30;
    query.r[4].w[0] = id;
    query.r[5].w[0] = 0x1B000;
    kernel::handle_syscall(query, 0x00700000);
    writer.pod(query.r[2].w[0]);
    std::memcpy(status.data(), g_ram + 0x1B000, status.size());
    writer.bytes(status.data(), status.size());
    std::memcpy(g_ram + 0x1B000, saved.data(), saved.size());
  }
  require(before == cpu_bytes(), "public status queries preserve live CPU");
  require(writer.out.size() < 64 * 1024, "bounded synthetic transcript");
  std::printf("%s:", label);
  for (const auto byte : writer.out)
    std::printf("%02x", byte);
  std::putchar('\n');
}

void setup() {
  kernel::reset();
  kernel::start_main_thread(0x00455A64, 0, 0, 0);
  g_cpu.r[29].w[0] = 0x00100000;
  g_cpu.budget = 500;
  store<std::uint32_t>(g_ram + 0x10004, 0x3F800000);
  store<std::uint16_t>(g_ram + 0x1000A, 4);
  store<std::uint32_t>(g_ram + 0x20004, 0xDEADBEEF);
  store<std::uint32_t>(g_spr + 0x2014, 1);
  store<std::uint32_t>(g_spr + 0x21F8, 0x12000);
  if (capturing)
    runtime_observer().start();
}

void pre() {
  g_cpu.pc = 0x00440000;
  for (unsigned argument = 0; argument < 4; ++argument)
    for (unsigned lane = 0; lane < 4; ++lane)
      g_cpu.r[argument + 4].w[lane] = 0x10000 + argument * 16 + lane;
  g_cpu.f[12] = 0x7FC12345;
  const auto before = cpu_bytes();
  probe(g_cpu, shape_call_pc);
  require(before == cpu_bytes(), "probe leaves full Cpu unchanged");
}

void post() {
  g_cpu.r[4].w[0] = 0x20000;
  g_cpu.f[12] = 0;
  g_cpu.pc = shape_return_pc;
  probe(g_cpu, shape_return_pc);
}

void syscall(std::uint32_t number, std::uint32_t first = 0, std::uint32_t second = 0) {
  g_cpu.r[3].w[0] = number;
  g_cpu.r[4].w[0] = first;
  g_cpu.r[5].w[0] = second;
  kernel::handle_syscall(g_cpu, 0x00700000);
}

void seed_owner(std::uint32_t owner) {
  if (!capturing)
    return;
  Inputs input;
  input.guest_sp = 0x100000;
  input.argument_bits[0][0] = 0x10000;
  runtime_observer().begin(owner, false, input);
}

void require_owner_aborted(std::uint64_t before) {
  if (capturing)
    require(runtime_observer().counters().aborted == before + 1, "actual lifecycle clears pending owner");
}

void adapter_controls() {
  setup();
  pre();
  store<std::uint32_t>(g_ram + 0x10004, 0x40000000);
  g_cpu.r[4].w[0] = 0x20000;
  g_cpu.pc = shape_return_pc | kUnwind;
  probe(g_cpu, shape_return_pc);
  if (capturing)
    require(runtime_observer().records().empty(), "unwind is not shape completion");
  post();
  if (capturing) {
    const auto records = runtime_observer().records();
    require(records.size() == 1 && records[0].before.argument_bits[0][0] == 0x10000 &&
                records[0].before.argument_bits[0][3] == 0x10003 &&
                records[0].before.fpr12_bits == 0x7FC12345 &&
                records[0].after.shape_bits == 0x40000000,
            "actual adapter samples saved original shape instead of volatile output");
  }
  transcript("adapter");
  const auto before = cpu_bytes();
  probe(g_cpu, 0x00707070);
  require(before == cpu_bytes(), "unknown diagnostic probe retained");
}

void lifecycle_controls() {
  setup();
  store<std::uint32_t>(g_ram + 0x18004, 0x00123400);
  store<std::uint32_t>(g_ram + 0x18008, 0x00200000);
  store<std::uint32_t>(g_ram + 0x1800C, 0x10000);
  store<std::uint32_t>(g_ram + 0x18014, 32);
  seed_owner(2);
  auto aborted = runtime_observer().counters().aborted;
  syscall(0x20, 0x18000);
  require(g_cpu.r[2].w[0] == 2 && kernel::current_thread() == 1, "create dormant TID2");
  require_owner_aborted(aborted);
  transcript("create");
  seed_owner(2);
  aborted = runtime_observer().counters().aborted;
  syscall(0x22, 2, 0xABCDEF);
  require(g_cpu.r[2].w[0] == 2 && kernel::current_thread() == 1, "start lower priority owner without dispatch");
  require_owner_aborted(aborted);
  transcript("start");
  for (auto number : {0x25u, 0x26u, 0x21u}) {
    seed_owner(2);
    aborted = runtime_observer().counters().aborted;
    syscall(number, 2);
    require(g_cpu.r[2].w[0] == 0, "terminate/delete result");
    require_owner_aborted(aborted);
    transcript(number == 0x21 ? "delete" : number == 0x25 ? "terminate" : "iterminate");
  }
  syscall(0x20, 0x18000);
  require(g_cpu.r[2].w[0] == 2, "deleted numerical ID reused");
  transcript("reuse");
  for (auto number : {0x23u, 0x24u}) {
    setup();
    pre();
    aborted = runtime_observer().counters().aborted;
    const auto previous_pc = g_cpu.pc;
    syscall(number);
    require_owner_aborted(aborted);
    require(kernel::current_thread() == 0, "exit releases ordinary owner");
    require(number == 0x23 ? g_cpu.pc == (kIdle | kUnwind) : g_cpu.pc == previous_pc,
            "pinned exit/delete PC effects preserved without dispatch");
    transcript(number == 0x23 ? "exit" : "exit-delete");
  }
  setup();
  pre();
  aborted = runtime_observer().counters().aborted;
  kernel::start_main_thread(0x00455A64, 0, 0, 0);
  require_owner_aborted(aborted);
  transcript("main-restart");
  pre();
  aborted = runtime_observer().counters().aborted;
  kernel::reset();
  require_owner_aborted(aborted);
  const auto invalid_before = invalid_owner_count();
  probe(g_cpu, shape_call_pc);
  if (capturing)
    require(invalid_owner_count() == invalid_before + 1, "idle owner rejected");
  transcript("reset");
}

void load_controls() {
  setup();
  pre();
  auto aborted = runtime_observer().counters().aborted;
  require(!load_state(std::string("")), "missing path load fails");
  require_owner_aborted(aborted);
  post();
  if (capturing)
    require(runtime_observer().records().empty() && runtime_observer().counters().orphan == 1,
            "failed path load invalidates before open");
  transcript("missing-load");
  pre();
  aborted = runtime_observer().counters().aborted;
  const std::uint8_t invalid[] = {0};
  require(!load_state(invalid, sizeof invalid), "short byte load fails");
  require_owner_aborted(aborted);
  transcript("short-load");

  pre();
  aborted = runtime_observer().counters().aborted;
  state::Writer partial;
  char magic[8] = {'P', 'S', '2', 'S', 'T', 'A', 'T', 'E'};
  partial.bytes(magic, sizeof magic);
  std::uint32_t version = 2;
  partial.pod(version);
  partial.tag("ee");
  Cpu replacement{};
  std::memset(&replacement, 0, sizeof replacement);
  replacement.pc = 0x00ABC000;
  replacement.r[4].w[0] = 0x55667788;
  partial.pod(replacement);
  require(!load_state(partial.out.data(), partial.out.size()), "partial mutation load fails");
  require_owner_aborted(aborted);
  require(g_cpu.pc == replacement.pc && g_cpu.r[4].w[0] == 0x55667788,
          "actual CPU mutation occurs before later load failure");
  transcript("partial-load");
}

unsigned handler_calls = 0;
void synthetic_handler(Cpu &cpu, std::uint32_t entry, std::uint32_t) {
  require(entry == 1 && kernel::in_handler(), "actual guest handler dispatch");
  ++handler_calls;
  probe(cpu, shape_call_pc);
  cpu.pc = shape_return_pc;
  probe(cpu, shape_return_pc);
  cpu.r[2].w[0] = 0;
  cpu.pc = kHandlerReturn;
}

void handler_controls() {
  setup();
  pre();
  static constexpr std::uint16_t entries[] = {1};
  const UnitInfo unit{0x00710000, 4, synthetic_handler, entries};
  register_units(&unit, 1);
  syscall(0x18, 1, unit.base);
  const int alarm = static_cast<int>(g_cpu.r[2].w[0]);
  require(alarm > 0, "synthetic alarm installed");
  kernel::alarm_action(alarm)();
  kernel::service(g_cpu);
  require(handler_calls == 1 && !kernel::in_handler() && kernel::current_thread() == 1,
          "ordinary guest owner restored after native handler dispatch");
  if (capturing)
    require(runtime_observer().counters().handler_skipped == 2 && runtime_observer().records().empty(),
            "handler probes cannot consume ordinary pending token");
  post();
  if (capturing)
    require(runtime_observer().records().size() == 1, "ordinary completion survives actual handler save/restore");
  transcript("handler");
}
} // namespace

int main() {
  try {
    require(!runtime_observer().enabled() && runtime_observer().records().empty(),
            "runtime observation must start disabled and empty");
    adapter_controls();
    lifecycle_controls();
    handler_controls();
    load_controls();
    std::fputs("PASS: synthetic runtime adapter/lifecycle controls\n", stderr);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
