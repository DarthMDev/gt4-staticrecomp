//@section helpers
// Halt forensic dump (TASK-017, slice 204). Active only when GT4_HALT_DUMP is set to a nonzero value; unset or "0"
// leaves dump() exactly as the donor. Every line starts with [halt-dump]. Guest reads are bounds checked against
// guest RAM: an address outside it prints "unreadable" and is never read.
bool halt_dump_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("GT4_HALT_DUMP");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool halt_read32(std::uint32_t address, std::uint32_t &out) {
    if (address > kRamSize - 4u) return false;
    std::memcpy(&out, g_ram + address, sizeof out);
    return true;
}

void halt_print_word(std::uint32_t address) {
    std::uint32_t value = 0;
    if (halt_read32(address, value)) std::fprintf(stderr, "%08X", value);
    else std::fprintf(stderr, "unreadable");
}

// Guest range of the record copy loop in AutomobileControlRecord::Manager::load (func_00346CA0, TASK-017).
constexpr std::uint32_t kHaltCopyLow = 0x00346BB8u;
constexpr std::uint32_t kHaltCopyHigh = 0x00346EB0u;
constexpr std::uint32_t kHaltRecordStride = 0x10000u;

// Prints the Manager state of a thread whose saved pc lies in the copy loop, then checks the record window
// [base, base + n * 0x10000) against the stacks of all threads.
void halt_dump_copy(int id, const Reg128 *gpr) {
    const std::uint32_t s = gpr[16].w[0], p = gpr[17].w[0], i = gpr[18].w[0], n = gpr[19].w[0];
    const std::uint32_t src = gpr[3].w[0], dst = gpr[6].w[0];
    std::uint32_t base = 0;
    const bool has_base = halt_read32(s + 0x5Cu, base);
    std::fprintf(stderr, "[halt-dump] copy thread=%d s=0x%08X p=0x%08X i=%u n=%u src=0x%08X dst=0x%08X base=", id, s, p, i, n,
                 src, dst);
    if (has_base) std::fprintf(stderr, "0x%08X", base);
    else std::fprintf(stderr, "unreadable");
    std::fprintf(stderr, " data=");
    halt_print_word(s + 0x60u);
    std::fprintf(stderr, "\n[halt-dump] copy words at p:");
    for (std::uint32_t word = 0; word < 16; ++word) {
        std::fprintf(stderr, " ");
        halt_print_word(p + word * 4u);
    }
    std::fprintf(stderr, "\n");
    if (!has_base) {
        std::fprintf(stderr, "[halt-dump] overlap unknown copier=%d base unreadable\n", id);
        return;
    }
    const std::uint64_t low = base;
    const std::uint64_t high = low + static_cast<std::uint64_t>(n) * kHaltRecordStride;
    bool found = false;
    for (const auto &[other_id, other] : g_threads) {
        if (other.stack_size == 0) continue;
        const std::uint64_t stack_low = other.stack;
        const std::uint64_t stack_high = stack_low + other.stack_size;
        if (low < stack_high && stack_low < high) {
            found = true;
            std::fprintf(stderr, "[halt-dump] overlap thread=%d copier=%d base=0x%08X end=0x%llX stack=0x%08X..0x%llX\n", other_id,
                         id, base, static_cast<unsigned long long>(high), other.stack,
                         static_cast<unsigned long long>(stack_high));
        }
    }
    if (!found)
        std::fprintf(stderr, "[halt-dump] overlap none copier=%d base=0x%08X end=0x%llX\n", id, base,
                     static_cast<unsigned long long>(high));
}

void halt_dump_threads() {
    for (const auto &[id, t] : g_threads) {
        const bool running = id == g_current;
        const Reg128 *gpr = running ? g_cpu.r : t.context.r;
        const std::uint32_t pc = (running ? g_cpu.pc : t.context.pc) & ~kUnwind;
        std::fprintf(stderr,
                     "[halt-dump] thread id=%d status=%d prio=%d entry=0x%08X stack=0x%08X stack_size=0x%X pc=0x%08X "
                     "sp=0x%08X ra=0x%08X current=%d\n",
                     id, t.status, t.priority, t.entry, t.stack, t.stack_size, pc, gpr[29].w[0], gpr[31].w[0], running ? 1 : 0);
        for (int reg = 0; reg < 32; reg += 4)
            std::fprintf(stderr, "[halt-dump]   r%02d=%08X r%02d=%08X r%02d=%08X r%02d=%08X\n", reg, gpr[reg].w[0], reg + 1,
                         gpr[reg + 1].w[0], reg + 2, gpr[reg + 2].w[0], reg + 3, gpr[reg + 3].w[0]);
        if (pc >= kHaltCopyLow && pc < kHaltCopyHigh) halt_dump_copy(id, gpr);
    }
}

// The halted thread is the current one: its pc keeps the kUnwind bit that dispatch() sets at a halt.
void halt_dump_halted_stack() {
    if (current() == nullptr) {
        std::fprintf(stderr, "[halt-dump] halted thread none\n");
        return;
    }
    const std::uint32_t sp = g_cpu.r[29].w[0];
    std::fprintf(stderr, "[halt-dump] halted thread=%d sp=0x%08X pc=0x%08X\n", g_current, sp, g_cpu.pc);
    for (std::uint32_t row = 0; row < 256; row += 16) {
        std::fprintf(stderr, "[halt-dump] stack %08X:", sp + row);
        for (std::uint32_t column = 0; column < 16; column += 4) {
            std::fprintf(stderr, " ");
            halt_print_word(sp + row + column);
        }
        std::fprintf(stderr, "\n");
    }
}

void halt_dump() {
    if (!halt_dump_enabled()) return;
    std::fprintf(stderr, "[halt-dump] begin threads=%zu\n", g_threads.size());
    halt_dump_threads();
    halt_dump_halted_stack();
    std::fprintf(stderr, "[halt-dump] end\n");
}
//@section dump_call
    halt_dump();
