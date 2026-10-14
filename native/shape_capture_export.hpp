#pragma once

#include "shape_observer.hpp"

#include <filesystem>
#include <string>

namespace gt4::observation {

enum class CaptureStopReason { manual, observer_stopped, shutdown };
inline constexpr std::size_t capture_json_limit = 256 * 1024;

// Same EE owner, outside guest execution; stop collection before reading records.
std::string serialize_capture(const ShapeObserver &observer, CaptureStopReason reason);
void export_capture(const std::filesystem::path &destination,
                    const ShapeObserver &observer, CaptureStopReason reason);

} // namespace gt4::observation
