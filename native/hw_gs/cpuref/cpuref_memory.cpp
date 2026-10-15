// SPDX-License-Identifier: MIT
// Copyright 2026 GT4Recomp contributors.
// Written from docs/hw-renderer-spec (clean room, decision 0069): chapters 02.7, 10.2.
#include "cpuref_memory.hpp"

namespace hwgs::cpuref {

namespace {

// TRXPOS.DIR (two bits) to the memory model's direction enum.
memory::TransferDirection DirectionOf(uint32_t dir) {
    return static_cast<memory::TransferDirection>(dir & 3u);
}

}  // namespace

LocalMemorySink::LocalMemorySink() = default;

void LocalMemorySink::InvalidateDestination(const UploadCommit&) {
    // The CPU reference has no caches to drop: the SLM is always the truth.
}

void LocalMemorySink::CommitUpload(const UploadCommit& commit, const uint8_t* bytes, size_t byteCount) {
    const auto psm = memory::decodePsm(commit.dpsm);
    if (!psm) {
        return;  // invalid PSM: the transfer is dropped (counted by the front end's rules)
    }
    const memory::BufferView destination{commit.dbp, commit.dbw, *psm};
    const memory::TransferRect rect{commit.dsax, commit.dsay, commit.rrw, commit.rrh};
    memory::hostToLocal(local_, destination, rect, std::span<const uint8_t>(bytes, byteCount));
    ++uploadCount;
}

void LocalMemorySink::LocalToLocalMove(const LocalMoveCommit& move) {
    const auto sourcePsm = memory::decodePsm(move.spsm);
    const auto destinationPsm = memory::decodePsm(move.dpsm);
    if (!sourcePsm || !destinationPsm) {
        return;
    }
    memory::localToLocal(local_,
                         memory::BufferView{move.sbp, move.sbw, *sourcePsm}, move.ssax, move.ssay,
                         memory::BufferView{move.dbp, move.dbw, *destinationPsm}, move.dsax, move.dsay,
                         move.rrw, move.rrh, DirectionOf(move.dir));
    ++moveCount;
}

void LocalMemorySink::BeginLocalToHost(const LocalToHostSetup& setup) {
    pending_ = setup;
    ++readCount;
}

std::vector<uint8_t> LocalMemorySink::TakeLocalToHostData() {
    std::vector<uint8_t> data;
    if (!pending_) {
        return data;
    }
    const LocalToHostSetup setup = *pending_;
    pending_.reset();
    const auto psm = memory::decodePsm(setup.spsm);
    if (!psm) {
        return data;
    }
    const memory::BufferView source{setup.sbp, setup.sbw, *psm};
    const memory::TransferRect rect{setup.ssax, setup.ssay, setup.rrw, setup.rrh};
    memory::localToHost(local_, source, rect, data);  // 4-bit sources are refused (data stays empty)
    return data;
}

}  // namespace hwgs::cpuref
