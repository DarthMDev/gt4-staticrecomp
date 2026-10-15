// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0069.
// CTest hw_present_rect: the HWR 16:9 rectangle (chapter 13.6) against the shared
// gt4::vulkan::presentation_rect used by the paraLLEl/donor host (chapter 12.5).

#include "../native_vulkan.hpp"
#include "hw_gs_present_rect.hpp"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

bool same(const gt4::hw::PresentRect &a, const gt4::vulkan::PresentationRect &b) {
  return a.left == b.left && a.top == b.top && a.width == b.width && a.height == b.height;
}

void expect_rect(std::uint32_t width, std::uint32_t height, gt4::hw::PresentRect expected) {
  const auto got = gt4::hw::present_rect_widescreen(width, height);
  const auto shared = gt4::vulkan::presentation_rect(width, height, gt4::vulkan::Aspect::widescreen);
  if (got.left != expected.left || got.top != expected.top || got.width != expected.width ||
      got.height != expected.height || !same(got, shared)) {
    ++failures;
    std::fprintf(stderr, "FAIL: %ux%u gave (%u,%u,%u,%u) expected (%u,%u,%u,%u)\n", width, height,
                 got.left, got.top, got.width, got.height, expected.left, expected.top, expected.width,
                 expected.height);
  }
}

} // namespace

int main() {
  // Known vectors (the 1280x1024 case is the letterbox example of chapter 13.6).
  expect_rect(1920, 1080, {0, 0, 1920, 1080});
  expect_rect(1280, 1024, {0, 152, 1280, 720});
  expect_rect(960, 720, {0, 90, 960, 540});
  expect_rect(2560, 1080, {320, 0, 1920, 1080});
  expect_rect(0, 0, {0, 0, 0, 0});
  expect_rect(1, 1, {0, 0, 1, 1});

  // Overflow guard and a sweep: the HWR rectangle must equal the shared one everywhere.
  const std::uint32_t samples[] = {1, 2, 3, 7, 16, 255, 640, 1023, 1024, 1366, 1920, 2160, 4096, 65535, UINT32_MAX};
  for (std::uint32_t width : samples) {
    for (std::uint32_t height : samples) {
      const auto got = gt4::hw::present_rect_widescreen(width, height);
      const auto shared = gt4::vulkan::presentation_rect(width, height, gt4::vulkan::Aspect::widescreen);
      if (!same(got, shared)) {
        ++failures;
        std::fprintf(stderr, "FAIL: sweep %ux%u differs from presentation_rect\n", width, height);
        return 1;
      }
      if (got.width && got.height && got.left + got.width > width) {
        ++failures;
        std::fprintf(stderr, "FAIL: sweep %ux%u rectangle leaves the surface\n", width, height);
        return 1;
      }
    }
  }

  if (failures == 0) std::printf("hw_present_rect: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
