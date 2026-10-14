#include "perf_accounting.hpp"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
using gt4::perf::Accounting;
using gt4::perf::AccountingFault;
using gt4::perf::Category;

void require(bool result, const char *message) {
  if (!result)
    throw std::runtime_error(message);
}

const auto &time(const Accounting &accounting, Category category) {
  return accounting.interval().categories[static_cast<std::size_t>(category)];
}

void require_partition(const Accounting &accounting) {
  std::uint64_t sum = 0;
  for (const auto &category : accounting.interval().categories) {
    require(category.exclusive_us <= std::numeric_limits<std::uint64_t>::max() - sum,
            "exclusive partition overflow");
    sum += category.exclusive_us;
  }
  require(sum == accounting.interval().elapsed_us, "exclusive budget must close");
  require(accounting.fault() == AccountingFault::none, "unexpected accounting fault");
}

void nested_scopes() {
  Accounting accounting(true);
  const auto ee = accounting.enter(Category::ee_dispatch, 0);
  const auto vif = accounting.enter(Category::vif_gif, 10);
  const auto vu = accounting.enter(Category::vu_interpret, 20);
  require(accounting.leave(vu, 50), "leave VU");
  require(accounting.leave(vif, 60), "leave VIF");
  const auto iop = accounting.enter(Category::iop_run, 70);
  require(accounting.leave(iop, 90), "leave nested IOP");
  const auto host = accounting.enter(Category::host_vblank, 100);
  const auto wait = accounting.enter(Category::presentation_wait, 110);
  require(accounting.leave(wait, 130), "leave wait");
  require(accounting.leave(host, 140), "leave host");
  require(accounting.leave(ee, 150), "leave EE");
  require(accounting.checkpoint(160), "unattributed tail");
  require(time(accounting, Category::ee_dispatch).inclusive_us == 150, "EE inclusive");
  require(time(accounting, Category::ee_dispatch).exclusive_us == 40, "EE self");
  require(time(accounting, Category::vif_gif).inclusive_us == 50, "VIF includes VU");
  require(time(accounting, Category::vif_gif).exclusive_us == 20, "VIF self");
  require(time(accounting, Category::vu_interpret).exclusive_us == 30, "VU self");
  require(time(accounting, Category::iop_run).exclusive_us == 20, "IOP self");
  require(time(accounting, Category::host_vblank).exclusive_us == 20, "host self");
  require(time(accounting, Category::presentation_wait).exclusive_us == 20, "wait self");
  require(time(accounting, Category::unattributed).exclusive_us == 10, "other self");
  require_partition(accounting);
}

void active_scope_crosses_boundaries() {
  Accounting accounting(true);
  const auto ee = accounting.enter(Category::ee_dispatch, 100);
  require(accounting.checkpoint(120), "frame cut");
  require(time(accounting, Category::ee_dispatch).inclusive_us == 20,
          "pending EE must be visible before return");
  require(accounting.restart(130), "report cut");
  require(accounting.active_scopes() == 1, "restart preserves scope ownership");
  require(accounting.leave(ee, 160), "return after report");
  require(accounting.interval().elapsed_us == 30, "no previous interval leakage");
  require(time(accounting, Category::ee_dispatch).exclusive_us == 30,
          "only post-cut EE retained");
  require(time(accounting, Category::ee_dispatch).entries == 0,
          "carried scope is not a new call");
  require_partition(accounting);
}

void disable_and_reenable() {
  Accounting accounting(true);
  const auto ee = accounting.enter(Category::ee_dispatch, 0);
  require(accounting.set_enabled(false, 10), "disable");
  const auto iop = accounting.enter(Category::iop_run, 20);
  require(accounting.leave(iop, 80), "disabled scope return");
  require(accounting.checkpoint(90), "disabled checkpoint");
  require(accounting.interval().elapsed_us == 10, "disabled gap excluded");
  require(accounting.set_enabled(true, 100), "reenable");
  require(accounting.leave(ee, 130), "scope begun before toggle returns");
  require(accounting.interval().elapsed_us == 30, "reenable starts fresh interval");
  require(time(accounting, Category::ee_dispatch).exclusive_us == 30,
          "only enabled portion of spanning scope counts");
  require(time(accounting, Category::iop_run).inclusive_us == 0,
          "disabled work never leaks");
  require_partition(accounting);
}

void recursion_and_faults() {
  Accounting recursive(true);
  const auto outer = recursive.enter(Category::ee_dispatch, 0);
  const auto inner = recursive.enter(Category::ee_dispatch, 10);
  require(recursive.leave(inner, 20) && recursive.leave(outer, 30), "recursive return");
  require(time(recursive, Category::ee_dispatch).inclusive_us == 30,
          "recursive inclusive union is not 40");
  require(time(recursive, Category::ee_dispatch).entries == 2, "both entries recorded");
  require_partition(recursive);

  Accounting interleaved(true);
  const auto parent = interleaved.enter(Category::ee_dispatch, 0);
  const auto child = interleaved.enter(Category::vu_interpret, 10);
  const auto reentry = interleaved.enter(Category::ee_dispatch, 20);
  require(interleaved.leave(reentry, 30) && interleaved.leave(child, 50) &&
          interleaved.leave(parent, 60), "interleaved recursive return");
  require(time(interleaved, Category::ee_dispatch).inclusive_us == 60 &&
          time(interleaved, Category::ee_dispatch).exclusive_us == 30,
          "interleaved EE union and self time");
  require(time(interleaved, Category::vu_interpret).inclusive_us == 40 &&
          time(interleaved, Category::vu_interpret).exclusive_us == 30,
          "interleaved VU union and self time");
  require_partition(interleaved);

  Accounting order(true);
  const auto first = order.enter(Category::ee_dispatch, 0);
  order.enter(Category::vu_interpret, 1);
  require(!order.leave(first, 2) && order.fault() == AccountingFault::scope_order,
          "non-LIFO return must invalidate measurement");
  require(!order.restart(3), "restart must not hide an accounting fault");

  Accounting backward(true);
  backward.checkpoint(10);
  require(!backward.checkpoint(9) && backward.fault() == AccountingFault::backward_clock,
          "clock regression must not wrap");

  Accounting invalid;
  require(!invalid.enter(Category::count, 0) &&
          invalid.fault() == AccountingFault::invalid_category, "invalid category");

  Accounting capacity;
  for (std::size_t index = 0; index < Accounting::scope_capacity; ++index)
    require(capacity.enter(Category::ee_dispatch, 0) != 0, "exact capacity accepted");
  require(!capacity.enter(Category::ee_dispatch, 0) &&
          capacity.fault() == AccountingFault::scope_capacity, "capacity limit");

  Accounting long_clock(true);
  const auto now = std::numeric_limits<std::uint64_t>::max() - 100;
  const auto token = long_clock.enter(Category::ee_dispatch, now);
  require(long_clock.leave(token, now + 30), "large monotonic timestamp");
  require(long_clock.interval().elapsed_us == 30, "large timestamps preserve delta");
  require_partition(long_clock);
}
} // namespace

int main() {
  try {
    nested_scopes();
    active_scope_crosses_boundaries();
    disable_and_reenable();
    recursion_and_faults();
    std::fprintf(stderr, "PASS: controlled-clock performance accounting\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
