// TASK-011 step 2: binds the donor VIF1, GIF and VU1 entry points to gt4::mtvu (slice170 capsule only).
// Built into ps2runtime_vulkan together with the --mtvu capsule (scripts/prepare_perf_runtime.py --mtvu).
#include "mtvu_runtime.hpp"

#include "gs/gif.hpp"
#include "vif.hpp"
#include "vu_micro.hpp"

namespace ps2::vif {
// Defined by the --mtvu capsule (vif.cpp): hash of the EE shadow or the worker's VIF1 unit state.
std::uint64_t verify_hash(bool worker_side);
}

namespace gt4::mtvu {
namespace {

void worker_vif_packet(const std::uint8_t *data, std::uint32_t qwc) { ps2::vif::transfer(1, data, qwc); }

void worker_vif_write(std::uint32_t reg, std::uint32_t value) { ps2::vif::write(1, reg, value); }

void worker_vu_reset() {
  ps2::vif::reset_vu_side();
  ps2::vu::reset();
}

void render_gs(int path, const std::uint8_t *data, std::uint32_t qwc) { ps2::gif::render(path, data, qwc); }

std::uint64_t vif1_state_hash(bool worker_side) { return ps2::vif::verify_hash(worker_side); }

}  // namespace

void start_with_donor(const Settings &settings) {
  Donor donor;
  donor.worker_vif_packet = worker_vif_packet;
  donor.worker_vif_write = worker_vif_write;
  donor.worker_vu_reset = worker_vu_reset;
  donor.render_gs = render_gs;
  donor.vif1_state_hash = vif1_state_hash;
  start(settings, donor);
}

}  // namespace gt4::mtvu
