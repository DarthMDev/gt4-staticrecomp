// SPDX-License-Identifier: MIT
//
// hw_cpuref_replay: replays a GT4_PGS_CAPTURE stream through the HWR CPU reference renderer and
// writes one frame_NNNNN.ppm per frame that has a display image (TASK-027, M4 to M6 path).
//
// Usage: hw_cpuref_replay <capture.gscap> <output-dir>
// Copyright 2026 GT4Recomp contributors. Licensed under the MIT License (see LICENSE).
// Written from docs/hw-renderer-spec (clean room, decision 0069).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cpuref_replay.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: hw_cpuref_replay <capture> <output-dir>\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "hw_cpuref_replay: cannot open %s\n", argv[1]);
        return 1;
    }
    const std::vector<uint8_t> stream((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::error_code ec;
    std::filesystem::create_directories(argv[2], ec);
    if (ec) {
        std::fprintf(stderr, "hw_cpuref_replay: cannot create %s: %s\n", argv[2], ec.message().c_str());
        return 1;
    }

    hwgs::cpuref::CpuRenderer renderer;
    hwgs::cpuref::ReplaySummary summary;
    std::string error;
    bool writeFailed = false;
    const bool ok = hwgs::cpuref::ReplayCapture(
        stream, renderer, summary,
        [&](uint64_t frame, const hwgs::cpuref::FrameImage& image) {
            if (!image.valid) {
                return;
            }
            char name[64];
            std::snprintf(name, sizeof(name), "frame_%05llu.ppm", static_cast<unsigned long long>(frame));
            const std::filesystem::path path = std::filesystem::path(argv[2]) / name;
            std::ofstream out(path, std::ios::binary);
            const std::string ppm = hwgs::cpuref::EncodePpm(image.image);
            out.write(ppm.data(), static_cast<std::streamsize>(ppm.size()));
            if (!out) {
                writeFailed = true;
            }
        },
        error);

    std::printf("records=%llu gif_packets=%llu gif_bytes=%llu privileged=%llu fifo_reads=%llu frames=%llu images=%llu\n",
                static_cast<unsigned long long>(summary.records), static_cast<unsigned long long>(summary.gifPackets),
                static_cast<unsigned long long>(summary.gifBytes), static_cast<unsigned long long>(summary.privileged),
                static_cast<unsigned long long>(summary.fifoReads), static_cast<unsigned long long>(summary.frames),
                static_cast<unsigned long long>(summary.framesWithImage));
    const hwgs::cpuref::DrawStats& stats = renderer.Stats();
    std::printf("batches=%llu primitives=%llu covered_pixels=%llu frame_writes=%llu depth_writes=%llu\n",
                static_cast<unsigned long long>(stats.batches), static_cast<unsigned long long>(stats.primitives),
                static_cast<unsigned long long>(stats.coveredPixels),
                static_cast<unsigned long long>(stats.framePixelsWritten),
                static_cast<unsigned long long>(stats.depthPixelsWritten));
    if (!ok) {
        std::fprintf(stderr, "hw_cpuref_replay: %s\n", error.c_str());
        return 1;
    }
    if (writeFailed) {
        std::fprintf(stderr, "hw_cpuref_replay: a frame could not be written\n");
        return 1;
    }
    return 0;
}
