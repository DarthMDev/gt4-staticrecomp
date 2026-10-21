// MIT License. Copyright (c) 2026 Joao Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), chapters 12.9 and 14.3; clean room
// (decision 0070).
//
// The GPU write-back renderer as a capture replay target, shared by hw_writeback_replay and the replay
// parity fixtures, so the tool and the tests drive the renderer through exactly the same adapter as the
// CPU reference (cpuref::ReplayTarget). EndFrame flushes the frame and composes the display image.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../cpuref/cpuref_replay.hpp"
#include "hw_gs_writeback.hpp"

namespace hwgs::writeback {

class WritebackReplayTarget final : public cpuref::ReplayTarget {
public:
    explicit WritebackReplayTarget(Renderer& renderer) : renderer_(renderer) {}

    bool GifTransfer(std::uint32_t path, const std::vector<std::uint8_t>& bytes) override {
        return renderer_.GifTransfer(path, bytes.data(), bytes.size());
    }
    bool WritePrivileged(std::uint32_t address, std::uint64_t value) override {
        return renderer_.WritePrivileged(address, value);
    }
    std::vector<std::uint8_t> ReadFifo(std::uint32_t qwords) override {
        // Same shape as the CPU reference: the requested qwords, zero-filled past the real data.
        std::vector<std::uint8_t> data(static_cast<std::size_t>(qwords) * 16u, 0);
        renderer_.ReadFifo(data.data(), data.size());
        return data;
    }
    cpuref::FrameImage EndFrame() override {
        renderer_.Flush();
        FrameOutput out;
        renderer_.ComposeFrame(out);
        cpuref::FrameImage frame;
        frame.valid = out.valid && out.width > 0 && out.height > 0;
        if (!frame.valid) return frame;
        frame.image = display::Image(out.width, out.height);
        for (std::uint32_t y = 0; y < out.height; ++y) {
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const std::uint32_t pixel = out.pixels[static_cast<std::size_t>(y) * out.width + x];
                display::Rgba8& dst = frame.image.at(x, y);
                dst.r = static_cast<std::uint8_t>(pixel & 0xFFu);
                dst.g = static_cast<std::uint8_t>((pixel >> 8) & 0xFFu);
                dst.b = static_cast<std::uint8_t>((pixel >> 16) & 0xFFu);
                dst.a = static_cast<std::uint8_t>((pixel >> 24) & 0xFFu);
            }
        }
        return frame;
    }

    void SetDisplayField(bool odd) override { oddField_ = odd; }
    bool DisplayField() const override { return oddField_; }
    void ResetGs(bool clearVram) override {
        renderer_.ResetGs();
        if (clearVram) renderer_.ClearVram();
    }
    void ResetGif(std::uint32_t pathMask) override { renderer_.GifReset(pathMask); }

private:
    Renderer& renderer_;
    bool oddField_ = false;
};

}  // namespace hwgs::writeback
