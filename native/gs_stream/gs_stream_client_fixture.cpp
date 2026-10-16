// SPDX-License-Identifier: MIT
// CTest native_gs_stream_client: the game-side GS stream backend (gs_stream_client.hpp) end to end, without the game.
//
//   gs_stream_client_fixture <path of gs_stream_record.exe> <scratch directory>
//
// Each scenario runs in a fresh child process of this executable (the client reads its environment once):
//   donor          GT4_RENDERER unset: every hook is a no-op and no file appears.
//   donor-capture  GT4_RENDERER=donor, GT4_GS_CAPTURE: the scripted hook sequence is written in process.
//   pcsx2          GT4_RENDERER=pcsx2, GT4_GS_PROCESS=gs_stream_record (recording) and GT4_GS_CAPTURE: the same
//                  sequence travels through shared memory to the recorder process.
//   bad-renderer   GT4_RENDERER=bogus is refused (exit code 1).
//   no-process     GT4_RENDERER=pcsx2 without GT4_GS_PROCESS is refused.
//   early-exit     the GS process exits during the handshake: refused with a message.
//   peer-death     the GS process dies mid-stream: the halt handler runs once, later hooks are harmless.
// Checks: the three stream files (donor capture, pcsx2 in-process capture, recorder output) are byte-identical; the
// file parses back with the expected record counts, MORE split and VSYNC fields; the SetGsCrt table.
#include "gs_stream_client.hpp"
#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gs_stream;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

int g_halts = 0;
void count_halt(const char *reason) {
  ++g_halts;
  std::printf("[fixture] halt: %s\n", reason);
}

std::vector<std::uint8_t> qwords_of(std::size_t qwords, std::uint8_t seed) {
  std::vector<std::uint8_t> data(qwords * 16);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(seed + i * 13);
  return data;
}

// The scripted hook sequence of a short session (deterministic).
void run_script(std::uint32_t frames) {
  namespace client = gt4::gs_stream;
  client::set_halt_handler(count_halt);
  client::on_gs_reset();
  client::on_set_gs_crt(1, 0x02, 1);
  std::vector<std::uint8_t> reply(512 * 16);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const auto path3 = qwords_of(3, static_cast<std::uint8_t>(frame));
    const auto path1 = qwords_of(64, static_cast<std::uint8_t>(frame + 1));
    const auto path2 = qwords_of(5, static_cast<std::uint8_t>(frame + 2));
    client::on_gif_render(3, path3.data(), 3);
    client::on_gif_render(1, path1.data(), 64);
    client::on_gif_render(2, path2.data(), 5);
    client::on_privileged_write(0x12000070u, frame);
    client::on_privileged_write(0x12001000u, 8);
    if (frame == 4) {
      // Larger than one record: split with kRecordFlagMore (16 MiB of the 32 MiB ring per record).
      const std::size_t qwords = (16u << 20) / 16 + 3;
      const auto big = qwords_of(qwords, 9);
      client::on_gif_render(3, big.data(), static_cast<std::uint32_t>(qwords));
    }
    if (frame == 6) {
      std::memset(reply.data(), 0xAB, reply.size());
      const bool served = client::read_fifo(reply.data(), 512);
      if (served)
        for (std::uint8_t byte : reply) require(byte == 0, "recorder readback is zero-filled");
      else
        for (std::uint8_t byte : reply) require(byte == 0xAB, "donor readback is left to the donor");
    }
    client::on_vsync((frame & 1) != 0);
  }
  client::on_gif_reset();
  client::finish(stdout);
}

int scenario_main(const std::string &scenario, std::uint32_t frames) {
  if (scenario == "donor") {
    const auto data = qwords_of(4, 1);
    require(!gt4::gs_stream::stream_renderer(), "donor by default");
    require(!gt4::gs_stream::on_gif_render(3, data.data(), 4), "donor GIF hook returns false");
    std::uint8_t reply[16] = {7};
    require(!gt4::gs_stream::read_fifo(reply, 1) && reply[0] == 7, "donor readback untouched");
    gt4::gs_stream::on_vsync(true);
    gt4::gs_stream::finish(stdout);
    return 0;
  }
  if (scenario == "early-exit" || scenario == "no-process" || scenario == "bad-renderer") {
    const auto data = qwords_of(1, 1);
    gt4::gs_stream::on_gif_render(3, data.data(), 1);  // refuses (exits) on first use
    return 0;
  }
  run_script(frames);
  if (scenario == "peer-death") return g_halts == 1 ? 0 : 5;
  return g_halts == 0 ? 0 : 6;
}

// --- parent side ---

std::wstring this_executable() {
  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::wstring(path, length);
}

struct Env {
  const char *name;
  std::string value;  // empty: unset
};

std::uint32_t run_scenario(const std::string &scenario, const std::vector<Env> &env, std::uint32_t frames = 10) {
  for (const char *name : {"GT4_RENDERER", "GT4_GS_PROCESS", "GT4_GS_CAPTURE", "GT4_GS_RECORD_OUTPUT",
                           "GT4_GS_CAPTURE_FRAMES", "GT4_GS_FRAMES_IN_FLIGHT", "GT4_FIXTURE_CHILD"})
    _putenv_s(name, "");
  for (const Env &item : env) _putenv_s(item.name, item.value.c_str());
  std::printf("[fixture] scenario %s\n", scenario.c_str());
  std::fflush(stdout);
  gs_stream::ipc::ChildProcess child = gs_stream::ipc::ChildProcess::spawn(
      this_executable(), L"--scenario " + gs_stream::ipc::widen(scenario) + L" " + std::to_wstring(frames));
  std::uint32_t code = 99;
  require(child.wait_exit(120000, &code), "scenario finished in time");
  for (const Env &item : env) _putenv_s(item.name, "");
  std::printf("[fixture] scenario %s exit code %u\n", scenario.c_str(), code);
  return code;
}

std::vector<char> read_all(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}

bool exists(const std::string &path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

void check_stream_file(const std::string &path, std::uint32_t frames) {
  FileReader reader;
  std::string error;
  require(reader.open(path, error), "stream file opens");
  require(std::string(reader.header().serial) == "SCUS-97328" && reader.header().first_frame == 0, "file header");
  RecordHeader header;
  std::vector<std::uint8_t> payload;
  std::uint64_t sequence = 0, gif = 0, more = 0, priv = 0, vsync = 0, fifo = 0, resets = 0, controls = 0;
  while (reader.next(header, payload)) {
    require(header.sequence == ++sequence, "file sequence numbers are consecutive");
    switch (static_cast<RecordType>(header.type)) {
      case RecordType::gif: {
        ++gif;
        if (header.flags & kRecordFlagMore) ++more;
        GifPayload g;
        std::memcpy(&g, payload.data(), sizeof g);
        require(g.path >= 1 && g.path <= 3 && g.bytes + sizeof g == payload.size(), "GIF payload");
        break;
      }
      case RecordType::priv: ++priv; break;
      case RecordType::vsync: {
        VsyncPayload v;
        std::memcpy(&v, payload.data(), sizeof v);
        require(v.frame == vsync && v.field == (vsync & 1) && (v.flags & kVsyncFlagFieldValid), "VSYNC frame and field");
        ++vsync;
        break;
      }
      case RecordType::read_fifo: {
        ReadFifoPayload r;
        std::memcpy(&r, payload.data(), sizeof r);
        require(r.qwords == 512 && r.request_id == 1, "READ_FIFO payload");
        ++fifo;
        break;
      }
      case RecordType::reset: ++resets; break;
      case RecordType::control: ++controls; break;
      default: throw std::runtime_error("unexpected record type in the file");
    }
  }
  // Per frame: 3 GIF, 2 PRIV, 1 VSYNC; plus the split chunk (2 records), 2 SetGsCrt PRIV, 1 READ_FIFO, 2 RESET.
  require(gif == 3ull * frames + 2 && more == 1, "GIF record count and split");
  require(priv == 2ull * frames + 2 && vsync == frames && fifo == 1 && resets == 2, "record counts");
  // The pcsx2 run also carries CONTROL shutdown at the end; the donor capture does not.
  require(controls <= 1, "control records");
  std::printf("[fixture] %s: %llu records ok\n", path.c_str(), static_cast<unsigned long long>(sequence));
}

void check_crt_table() {
  std::uint64_t smode1 = 0, smode2 = 0;
  require(gt4::gs_stream::map_set_gs_crt(1, 0x02, 1, smode1, smode2) && smode1 == 0x4100 && smode2 == 3, "NTSC 480i");
  require(gt4::gs_stream::map_set_gs_crt(1, 0x02, 0, smode1, smode2) && smode1 == 0x4100 && smode2 == 1, "NTSC field mode");
  require(gt4::gs_stream::map_set_gs_crt(0, 0x50, 1, smode1, smode2) && smode1 == 0x100 && smode2 == 0, "480p");
  require(gt4::gs_stream::map_set_gs_crt(1, 0x51, 1, smode1, smode2) && smode1 == 0xB0 && smode2 == 3, "1080i");
  require(gt4::gs_stream::map_set_gs_crt(0, 0x03, 0, smode1, smode2) && smode1 == 0x6100 && smode2 == 0, "PAL");
  require(gt4::gs_stream::map_set_gs_crt(0, 0x1A, 0, smode1, smode2) && smode1 == 0x78, "VESA");
  require(!gt4::gs_stream::map_set_gs_crt(0, 0x51, 1, smode1, smode2), "1080i must be interlaced");
  require(!gt4::gs_stream::map_set_gs_crt(1, 0x77, 0, smode1, smode2), "unknown mode refused");
  require(!gt4::gs_stream::map_set_gs_crt(1, 0x02, 2, smode1, smode2), "bad field refused");
  std::puts("[fixture] SetGsCrt table ok");
}

}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--scenario") return scenario_main(argv[2], std::stoul(argv[3]));
    if (argc >= 2 && std::string(argv[1]) == "--ipc") {
      // Fake GS process. Without GT4_FIXTURE_CHILD it exits at once (early-exit); with it, it posts READY and dies
      // after 20 records (peer-death).
      const char *mode = std::getenv("GT4_FIXTURE_CHILD");
      if (!mode || !*mode) return 7;
      auto session = ipc::Session::open_consumer(argv[2]);
      ReadyPayload ready{};
      ready.version_major = kVersionMajor;
      ready.consumer_pid = ipc::current_process_id();
      session->post_event(RecordType::ready, &ready, sizeof ready);
      ipc::RecordView view;
      for (int n = 0; n < 20; ++n) {
        session->next_command(view);
        session->release_command(view);
      }
      ExitProcess(9);
    }
    if (argc != 3) {
      std::fputs("usage: gs_stream_client_fixture <gs_stream_record.exe> <scratch directory>\n", stderr);
      return 2;
    }
    check_crt_table();
    const std::string recorder = argv[1];
    const std::string scratch = argv[2];
    CreateDirectoryA(scratch.c_str(), nullptr);
    const std::string donor_file = scratch + "\\donor-capture.gscap";
    const std::string pcsx2_capture = scratch + "\\pcsx2-capture.gscap";
    const std::string recorded = scratch + "\\pcsx2-recorded.gscap";
    for (const std::string &path : {donor_file, pcsx2_capture, recorded}) DeleteFileA(path.c_str());

    require(run_scenario("donor", {}) == 0, "donor scenario");
    require(!exists(donor_file), "donor without capture writes nothing");
    require(run_scenario("donor-capture", {{"GT4_RENDERER", "donor"}, {"GT4_GS_CAPTURE", donor_file}}) == 0,
            "donor capture scenario");
    require(run_scenario("pcsx2", {{"GT4_RENDERER", "pcsx2"}, {"GT4_GS_PROCESS", recorder},
                                   {"GT4_GS_RECORD_OUTPUT", recorded}, {"GT4_GS_CAPTURE", pcsx2_capture}}) == 0,
            "pcsx2 scenario");
    check_stream_file(donor_file, 10);
    check_stream_file(pcsx2_capture, 10);
    // The recorder's file also holds the CONTROL shutdown posted by finish() after the in-process capture closed.
    const std::vector<char> donor_bytes = read_all(donor_file), capture_bytes = read_all(pcsx2_capture);
    std::vector<char> recorded_bytes = read_all(recorded);
    require(donor_bytes == capture_bytes, "donor capture equals the pcsx2 in-process capture");
    require(recorded_bytes.size() == capture_bytes.size() + 48 &&
                std::equal(capture_bytes.begin(), capture_bytes.end(), recorded_bytes.begin()),
            "recorder file equals the in-process capture plus CONTROL shutdown");
    std::puts("[fixture] donor capture, pcsx2 capture and recorder file are byte-identical");

    require(run_scenario("bad-renderer", {{"GT4_RENDERER", "bogus"}}) == 1, "unknown renderer refused");
    require(run_scenario("no-process", {{"GT4_RENDERER", "pcsx2"}}) == 1, "missing GT4_GS_PROCESS refused");
    const std::string self(argv[0]);
    // early-exit: GT4_GS_PROCESS points at a program that exits at once (this fixture with an unknown argument list).
    require(run_scenario("early-exit", {{"GT4_RENDERER", "pcsx2"}, {"GT4_GS_PROCESS", self}}) == 1,
            "GS process exiting during the handshake is refused");
    require(run_scenario("peer-death", {{"GT4_RENDERER", "pcsx2"}, {"GT4_GS_PROCESS", self}, {"GT4_FIXTURE_CHILD", "1"}},
                         10) == 0,
            "GS process death mid-stream halts once");
    std::puts("[fixture] all checks passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[fixture] FAIL: %s\n", error.what());
    return 1;
  }
}
