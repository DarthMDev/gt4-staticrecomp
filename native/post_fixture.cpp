// CoreGS 1.1 post-pass control (slice 182, TASK-028). The GPU post passes (native/post/present.wgsl,
// through the donor adapter) must match the CPU reference (native/post/post_reference.cpp) on small
// synthetic interlaced sequences:
//   - weave, bob, blend and adaptive frame by frame (exact), with the automatic mode equal to adaptive;
//   - progressive input is never changed by the deinterlace modes or automatic;
//   - FXAA equals the reference to within one level per channel, and leaves flat areas unchanged.
#include "gpu.hpp"
#include "gs/raster.hpp"
#include "post/post_reference.hpp"
#include "post/post_settings.hpp"
#include "post/post_test.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <utility>
#include <vector>

namespace {
using namespace ps2::gs;
using gpu::PostDeinterlace;
using gpu::PostSettings;

constexpr std::uint32_t kWidth = 16;
constexpr std::uint32_t kHeight = 12;
constexpr float kThreshold = 0.08f;

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

// A smooth-ish moving pattern with some sharp edges, so that the adaptive threshold sees motion and static areas.
std::vector<std::uint32_t> make_frame(std::uint32_t index) {
    std::vector<std::uint32_t> frame(kWidth * kHeight);
    std::uint32_t seed = 0x9E3779B9u * (index + 1u);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const std::uint32_t moving = ((x + index * 2u) / 4u + (y / 3u)) & 1u ? 200u : 40u;
            const std::uint32_t noise = (seed >> 24) & 15u;
            const std::uint32_t r = (moving + noise) & 255u;
            const std::uint32_t g = (x * 13u + y * 7u + index * 5u) & 255u;
            const std::uint32_t b = (y % 5u == 0u) ? 230u : (seed >> 16) & 255u;
            frame[y * kWidth + x] = r | (g << 8) | (b << 16) | (0xFFu << 24);
        }
    return frame;
}

std::vector<std::uint32_t> run_gpu(const std::vector<std::uint32_t> &frame, bool interlaced, std::uint32_t field,
                                   const PostSettings &settings) {
    std::vector<std::uint32_t> result;
    gpu::post_frame_for_test(frame, kWidth, kHeight, interlaced, field, settings, kWidth, kHeight, result);
    return result;
}

void compare_exact(const std::vector<std::uint32_t> &gpu_frame, const std::vector<std::uint32_t> &cpu_frame,
                   const std::string &label, std::uint32_t frame_index) {
    for (std::size_t i = 0; i < cpu_frame.size(); ++i)
        if (gpu_frame[i] != cpu_frame[i])
            throw std::runtime_error(label + " frame " + std::to_string(frame_index) + " pixel " + std::to_string(i) +
                                     " differs: GPU " + std::to_string(gpu_frame[i]) + ", CPU " + std::to_string(cpu_frame[i]));
}

void compare_near(const std::vector<std::uint32_t> &gpu_frame, const std::vector<std::uint32_t> &cpu_frame,
                  const std::string &label) {
    for (std::size_t i = 0; i < cpu_frame.size(); ++i)
        for (unsigned k = 0; k < 4; ++k) {
            const int a = static_cast<int>((gpu_frame[i] >> (8 * k)) & 255u);
            const int b = static_cast<int>((cpu_frame[i] >> (8 * k)) & 255u);
            if (std::abs(a - b) > 1)
                throw std::runtime_error(label + " pixel " + std::to_string(i) + " channel " + std::to_string(k) +
                                         " differs by more than 1: GPU " + std::to_string(a) + ", CPU " + std::to_string(b));
        }
}

void check_deinterlace(PostDeinterlace mode, const char *name) {
    gpu::post_reset_for_test();
    post_reference::Deinterlacer reference;
    PostSettings settings;
    settings.deinterlace = mode;
    for (std::uint32_t index = 0; index < 8; ++index) {
        const std::uint32_t field = index & 1u;
        const auto frame = make_frame(index);
        const auto gpu_frame = run_gpu(frame, true, field, settings);
        const auto cpu_frame = reference.process(frame, kWidth, kHeight, mode, field, 1u, kThreshold);
        compare_exact(gpu_frame, cpu_frame, name, index);
    }
    std::fprintf(stderr, "[post] %s: 8 interlaced frames identical to the CPU reference\n", name);
}

} // namespace

int main() {
    try {
        if (_putenv_s("PS2_SCALE", "1") != 0) throw std::runtime_error("cannot set PS2_SCALE");
        ps2::gs::init();
        ps2::gs::reset();
        if (gpu::mode() != gpu::Mode::primary) throw std::runtime_error("post control requires primary GPU rasterization");

        check_deinterlace(PostDeinterlace::weave, "weave");
        check_deinterlace(PostDeinterlace::bob, "bob");
        check_deinterlace(PostDeinterlace::blend, "blend");
        check_deinterlace(PostDeinterlace::adaptive, "adaptive");

        // Automatic is adaptive on interlaced output.
        gpu::post_reset_for_test();
        post_reference::Deinterlacer reference;
        for (std::uint32_t index = 0; index < 6; ++index) {
            const auto frame = make_frame(index);
            const auto gpu_frame = run_gpu(frame, true, index & 1u, PostSettings{});
            const auto cpu_frame = reference.process(frame, kWidth, kHeight, PostDeinterlace::adaptive, index & 1u, 1u, kThreshold);
            compare_exact(gpu_frame, cpu_frame, "automatic", index);
        }
        std::fprintf(stderr, "[post] automatic equals adaptive on interlaced frames\n");

        // Progressive output is untouched by every deinterlace mode and by automatic.
        const PostDeinterlace modes[] = {PostDeinterlace::automatic, PostDeinterlace::weave, PostDeinterlace::bob,
                                         PostDeinterlace::blend, PostDeinterlace::adaptive};
        for (PostDeinterlace mode : modes) {
            gpu::post_reset_for_test();
            PostSettings progressive;
            progressive.deinterlace = mode;
            for (std::uint32_t index = 0; index < 4; ++index) {
                const auto frame = make_frame(index);
                const auto gpu_frame = run_gpu(frame, false, index & 1u, progressive);
                compare_exact(gpu_frame, frame, "progressive", index);
            }
        }
        std::fprintf(stderr, "[post] progressive frames untouched by every deinterlace mode and by automatic\n");

        // FXAA: near the reference; flat areas unchanged exactly.
        gpu::post_reset_for_test();
        PostSettings fxaa;
        fxaa.fxaa = true;
        for (std::uint32_t index = 0; index < 4; ++index) {
            const auto frame = make_frame(index);
            const auto gpu_frame = run_gpu(frame, false, 0, fxaa);
            compare_near(gpu_frame, post_reference::fxaa(frame, kWidth, kHeight), "fxaa frame " + std::to_string(index));
        }
        std::vector<std::uint32_t> flat(kWidth * kHeight, 0xFF203040u);
        compare_exact(run_gpu(flat, false, 0, fxaa), flat, "fxaa flat", 0);
        std::fprintf(stderr, "[post] FXAA within one level of the reference; flat image unchanged\n");
        // FSR 1.0 (EASU then RCAS) at 2x, 3x and a non-integer size: near the CPU reference.
        const std::pair<std::uint32_t, std::uint32_t> targets[] = {{32, 24}, {48, 36}, {40, 30}};
        for (const auto &target : targets) {
            gpu::post_reset_for_test();
            PostSettings fsr_settings;
            fsr_settings.filter = gpu::PostFilter::fsr;
            fsr_settings.sharpness = 0.2f;
            for (std::uint32_t index = 0; index < 3; ++index) {
                const auto frame = make_frame(index);
                std::vector<std::uint32_t> gpu_frame;
                gpu::post_frame_for_test(frame, kWidth, kHeight, false, 0, fsr_settings, target.first, target.second, gpu_frame);
                const auto cpu_frame = post_reference::fsr_reference(frame, kWidth, kHeight, target.first, target.second, 0.2f);
                require(gpu_frame.size() == cpu_frame.size(), "FSR output size");
                compare_near(gpu_frame, cpu_frame, "fsr " + std::to_string(target.first) + "x" + std::to_string(target.second) +
                                                       " frame " + std::to_string(index));
            }
        }
        std::fprintf(stderr, "[post] FSR 1.0 EASU and RCAS within one level of the reference at three sizes\n");
        std::fprintf(stderr, "[post] PASS\n");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[post] FAIL: %s\n", error.what());
        return 1;
    }
}
