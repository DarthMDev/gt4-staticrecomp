#pragma once

#include "shape_capture_export.hpp"

namespace gt4::observation {

enum class CapturePhase { idle, capturing, ready, exported };

// One capture per process; message handlers latch only, EE host boundaries service it.
class CaptureControl {
public:
  CaptureControl(ShapeObserver &observer, std::filesystem::path destination);
  CaptureControl(const CaptureControl &) = delete;
  CaptureControl &operator=(const CaptureControl &) = delete;
  void request_toggle(bool repeated_key = false) noexcept;
  void service() noexcept;
  void finish() noexcept;
  CapturePhase phase() const noexcept { return phase_; }

private:
  void export_ready() noexcept;
  ShapeObserver &observer_;
  std::filesystem::path destination_;
  CapturePhase phase_ = CapturePhase::idle;
  CaptureStopReason reason_ = CaptureStopReason::manual;
  bool requested_ = false;
};

} // namespace gt4::observation
