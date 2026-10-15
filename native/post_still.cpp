// CoreGS 1.1 still-image tool (slice 182, TASK-028). Runs two consecutive frames (PPM, P6, 8-bit) through
// the GPU post passes and writes the second frame's result as PPM, so the owner's sample images can be made
// from headless frame dumps. Not part of CTest; the CTest control is post_fixture.
//
// usage: post_still <previous.ppm> <current.ppm> <out.ppm> [--interlaced] [--field N]
//        [--deinterlace auto|off|weave|bob|blend|adaptive] [--aa off|fxaa]
//        [--filter nearest|linear|fsr] [--sharpness STOPS] [--target WxH]
#include "gpu.hpp"
#include "gs/raster.hpp"
#include "post/post_settings.hpp"
#include "post/post_test.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2::gs;
using gpu::PostDeinterlace;
using gpu::PostFilter;

struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
};

Image read_ppm(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path);
    std::string magic;
    std::uint32_t width = 0, height = 0, maximum = 0;
    input >> magic >> width >> height >> maximum;
    input.get();
    if (magic != "P6" || maximum != 255 || !width || !height) throw std::runtime_error(path + " is not a P6 8-bit PPM");
    Image image{width, height, std::vector<std::uint32_t>(static_cast<std::size_t>(width) * height)};
    std::vector<std::uint8_t> bytes(image.pixels.size() * 3);
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error(path + " is truncated");
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
        image.pixels[i] = bytes[i * 3] | (bytes[i * 3 + 1] << 8) | (bytes[i * 3 + 2] << 16) | (0xFFu << 24);
    return image;
}

void write_ppm(const std::string &path, const Image &image) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (std::uint32_t pixel : image.pixels) {
        const char rgb[3] = {static_cast<char>(pixel), static_cast<char>(pixel >> 8), static_cast<char>(pixel >> 16)};
        output.write(rgb, 3);
    }
}

PostDeinterlace parse_deinterlace(const std::string &value) {
    if (value == "auto") return PostDeinterlace::automatic;
    if (value == "off") return PostDeinterlace::off;
    if (value == "weave") return PostDeinterlace::weave;
    if (value == "bob") return PostDeinterlace::bob;
    if (value == "blend") return PostDeinterlace::blend;
    if (value == "adaptive") return PostDeinterlace::adaptive;
    throw std::runtime_error("unknown --deinterlace " + value);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 4) throw std::runtime_error("usage: post_still <previous.ppm> <current.ppm> <out.ppm> [options]");
        gpu::PostSettings settings;
        bool interlaced = false;
        std::uint32_t field = 1, target_width = 0, target_height = 0;
        for (int i = 4; i < argc; ++i) {
            const std::string option = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(option + " needs a value");
                return argv[++i];
            };
            if (option == "--interlaced") interlaced = true;
            else if (option == "--field") field = static_cast<std::uint32_t>(std::atoi(value().c_str()));
            else if (option == "--deinterlace") settings.deinterlace = parse_deinterlace(value());
            else if (option == "--aa") settings.fxaa = value() == "fxaa";
            else if (option == "--filter") {
                const std::string filter = value();
                settings.filter = filter == "fsr" ? PostFilter::fsr : filter == "nearest" ? PostFilter::nearest
                                : filter == "linear" ? PostFilter::linear : PostFilter::automatic;
            } else if (option == "--sharpness") settings.sharpness = static_cast<float>(std::atof(value().c_str()));
            else if (option == "--target") {
                const std::string size = value();
                const auto x = size.find('x');
                if (x == std::string::npos) throw std::runtime_error("--target takes WxH");
                target_width = static_cast<std::uint32_t>(std::stoul(size.substr(0, x)));
                target_height = static_cast<std::uint32_t>(std::stoul(size.substr(x + 1)));
            } else throw std::runtime_error("unknown option " + option);
        }
        if (_putenv_s("PS2_SCALE", "1") != 0) throw std::runtime_error("cannot set PS2_SCALE");
        ps2::gs::init();
        ps2::gs::reset();
        if (gpu::mode() != gpu::Mode::primary) throw std::runtime_error("post_still requires primary GPU rasterization");
        const Image previous = read_ppm(argv[1]);
        const Image current = read_ppm(argv[2]);
        if (previous.width != current.width || previous.height != current.height)
            throw std::runtime_error("the two frames differ in size");
        gpu::post_reset_for_test();
        std::vector<std::uint32_t> result;
        gpu::post_frame_for_test(previous.pixels, previous.width, previous.height, interlaced, 0, settings, target_width,
                                 target_height, result);
        // The second frame sees the first in its deinterlace history; the field alternates.
        gpu::post_frame_for_test(current.pixels, current.width, current.height, interlaced, field, settings, target_width,
                                 target_height, result);
        const bool fsr = settings.filter == PostFilter::fsr && target_width && target_height;
        Image out{fsr ? target_width : current.width, fsr ? target_height : current.height, result};
        write_ppm(argv[3], out);
        std::fprintf(stderr, "[post_still] wrote %s (%ux%u)\n", argv[3], out.width, out.height);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[post_still] FAIL: %s\n", error.what());
        return 1;
    }
}
