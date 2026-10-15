// Spliced after plan_decodes(prims) in flush_batch (scripts/prepare_external_vulkan.py). Read-only.
if (gs_prim_stats::stats_enabled()) {
    const std::uint32_t batch_setups = static_cast<std::uint32_t>(b.setups.size() / kSetupWords);
    gs_prim_stats::note_batch(b.prims.data(), prims, b.setups.data(), kSetupWords);
    // Stale decode: a decoded texture (sub-sample 0 only) whose pages this batch writes.
    const Range target = pages(b.fbp, b.fbw, b.fpsm, b.minx, b.miny, b.maxx, b.maxy);
    for (std::uint32_t i = 0; i < batch_setups; ++i) {
        const std::uint32_t *w = b.setups.data() + i * kSetupWords;
        if (!(w[0] & 2u) || w[24] == 0xFFFFFFFFu) continue;
        const std::uint32_t tw = w[15] & 0xFFu, th = w[15] >> 8;
        const Range texture = pages(w[12], w[13], w[14], 0, 0, (1 << tw) - 1, (1 << th) - 1);
        if (texture.lo < target.hi && target.lo < texture.hi) gs_prim_stats::note_stale_decode();
    }
}
