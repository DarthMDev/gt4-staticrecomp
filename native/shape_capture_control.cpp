#include "shape_capture_control.hpp"

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace gt4::observation {

CaptureControl::CaptureControl(ShapeObserver &observer, std::filesystem::path destination)
    : observer_(observer), destination_(std::move(destination)) {
  if (observer_.enabled() || !observer_.records().empty() || observer_.counters().started != 0)
    throw std::logic_error("Capture control requires an unused stopped observer");
  if (!destination_.is_absolute() || destination_.filename().empty() ||
      destination_.filename().native().find(L':') != std::wstring::npos)
    throw std::invalid_argument("Capture output must be an absolute ordinary file path");
  if (!std::filesystem::is_directory(destination_.parent_path()))
    throw std::invalid_argument("Capture output parent must already exist");
  if (std::filesystem::exists(std::filesystem::symlink_status(destination_)))
    throw std::invalid_argument("Capture output already exists; preserve it");
}

void CaptureControl::request_toggle(bool repeated_key) noexcept {
  if (!repeated_key) requested_ = !requested_;
}

void CaptureControl::export_ready() noexcept {
  try {
    export_capture(destination_, observer_, reason_);
    phase_ = CapturePhase::exported;
    std::fprintf(stderr, "[shape-capture] saved %zu metadata records; one capture per launch\n",
                 observer_.records().size());
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[shape-capture] export failed: %s; records retained, F8 retries\n", error.what());
  }
}

void CaptureControl::service() noexcept {
  const bool requested = std::exchange(requested_, false);
  if (phase_ == CapturePhase::exported) return;
  if (phase_ == CapturePhase::idle) {
    if (requested) {
      observer_.start();
      phase_ = CapturePhase::capturing;
      std::fputs("[shape-capture] started; F8 stops, bounded to 256 records\n", stderr);
    }
    return;
  }
  if (phase_ == CapturePhase::capturing) {
    if (!observer_.enabled()) {
      reason_ = CaptureStopReason::observer_stopped;
    } else if (requested) {
      observer_.stop();
      reason_ = CaptureStopReason::manual;
    } else {
      return;
    }
    phase_ = CapturePhase::ready;
    export_ready();
  } else if (requested) {
    export_ready();
  }
}

void CaptureControl::finish() noexcept {
  requested_ = false;
  if (phase_ == CapturePhase::capturing) {
    reason_ = observer_.enabled() ? CaptureStopReason::shutdown : CaptureStopReason::observer_stopped;
    observer_.stop();
    phase_ = CapturePhase::ready;
  }
  if (phase_ == CapturePhase::ready) export_ready();
}

} // namespace gt4::observation
