// CoreGS 1.1 present-path post passes (TASK-028 / CoreGS 1.1).
//
// One compute entry point, "post", selected by params word 0 (the op). Every op reads one
// RGBA8 image (packed u32, red in the low byte) from `scratch` (buffer 0) or `temp` (buffer 1)
// and writes another. Persistent state lives in `history`: three slots of F words each
// (slot 0 = last even-row field, 1 = last odd-row field, 2 = last output), used by weave, blend
// and adaptive deinterlace. The host chains ops with one dispatch per op; the device orders them.
//
// Deinterlace follows docs/hw-renderer-spec chapter 13.4 (weave, bob, blend, adaptive) and the
// MIT CPU reference in native/post/post_reference.cpp, which the CTest post_fixture compares against.

struct Params {
    v: array<vec4<u32>, 16>,
}

@group(0) @binding(2) var<uniform> params: Params;
@group(0) @binding(5) var<storage, read_write> scratch: array<u32>;
@group(0) @binding(8) var<storage, read_write> history: array<u32>;
@group(0) @binding(9) var<storage, read_write> temp: array<u32>;

const OP_COPY: u32 = 0u;
const OP_WEAVE: u32 = 1u;
const OP_BOB: u32 = 2u;
const OP_BLEND_TAP: u32 = 3u;
const OP_ADAPTIVE: u32 = 4u;
const OP_HISTORY_COPY: u32 = 5u;
const OP_FXAA: u32 = 6u;
const OP_EASU: u32 = 7u;
const OP_RCAS: u32 = 8u;

// Parameter words (params word index).
const P_OP: u32 = 0u;
const P_W: u32 = 1u;        // input width
const P_H: u32 = 2u;        // input height
const P_IN: u32 = 3u;       // input buffer: 0 scratch, 1 temp
const P_OUT: u32 = 4u;      // output buffer: 0 scratch, 1 temp
const P_FIELD: u32 = 5u;    // row parity of the current field (0 or 1)
const P_SHIFT: u32 = 6u;    // bob shift in rows
const P_FLAGS: u32 = 7u;    // bit 0 has previous output, bit 1 has previous even, bit 2 has previous odd
const P_THRESHOLD: u32 = 8u; // adaptive threshold as f32 bits
const P_OW: u32 = 9u;       // output width
const P_OH: u32 = 10u;      // output height
const P_SLOT_WORDS: u32 = 11u; // F: words per history slot (W * H of the frame)
const P_SLOT: u32 = 12u;    // history slot for OP_HISTORY_COPY

fn prm(i: u32) -> u32 {
    return params.v[i / 4u][i % 4u];
}

fn prm_f(i: u32) -> f32 {
    return bitcast<f32>(prm(i));
}

fn load_px(which: u32, i: u32) -> u32 {
    if (which == 0u) {
        return scratch[i];
    }
    return temp[i];
}

fn store_px(which: u32, i: u32, value: u32) {
    if (which == 0u) {
        scratch[i] = value;
    } else {
        temp[i] = value;
    }
}

fn chan(c: u32, k: u32) -> u32 {
    return (c >> (8u * k)) & 255u;
}

fn pack4(r: u32, g: u32, b: u32, a: u32) -> u32 {
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

fn luma(c: u32) -> f32 {
    return dot(vec3<f32>(f32(chan(c, 0u)), f32(chan(c, 1u)), f32(chan(c, 2u))) / 255.0, vec3<f32>(0.299, 0.587, 0.114));
}

// True when any of R, G, B differs by more than the threshold (a fraction of 255).
fn exceeds(a: u32, b: u32, threshold: f32) -> bool {
    for (var k = 0u; k < 3u; k = k + 1u) {
        let d = abs(f32(chan(a, k)) - f32(chan(b, k))) / 255.0;
        if (d > threshold) {
            return true;
        }
    }
    return false;
}

// Rounded average, as average_byte() in the reference: (a + b + 1) / 2 per channel.
fn average(a: u32, b: u32) -> u32 {
    var result = 0u;
    for (var k = 0u; k < 4u; k = k + 1u) {
        result = result | (((chan(a, k) + chan(b, k) + 1u) / 2u) << (8u * k));
    }
    return result;
}

// Three-tap vertical filter (1/4, 1/2, 1/4) with integer rounding, as the reference blend.
fn tap3(a: u32, b: u32, c: u32) -> u32 {
    var result = 0u;
    for (var k = 0u; k < 4u; k = k + 1u) {
        let t = (chan(a, k) + 2u * chan(b, k) + chan(c, k) + 2u) / 4u;
        result = result | (t << (8u * k));
    }
    return result;
}

fn fxaa_sample(which: u32, x: f32, y: f32, w: u32, h: u32) -> vec3<f32> {
    // Bilinear sample at pixel-centre coordinates, clamped to the image (linear sampler, clamp to edge).
    let fx = x - 0.5;
    let fy = y - 0.5;
    let x0 = floor(fx);
    let y0 = floor(fy);
    let tx = fx - x0;
    let ty = fy - y0;
    let maxx = i32(w) - 1;
    let maxy = i32(h) - 1;
    let ix0 = clamp(i32(x0), 0, maxx);
    let ix1 = clamp(i32(x0) + 1, 0, maxx);
    let iy0 = clamp(i32(y0), 0, maxy);
    let iy1 = clamp(i32(y0) + 1, 0, maxy);
    let c00 = load_px(which, u32(iy0) * w + u32(ix0));
    let c10 = load_px(which, u32(iy0) * w + u32(ix1));
    let c01 = load_px(which, u32(iy1) * w + u32(ix0));
    let c11 = load_px(which, u32(iy1) * w + u32(ix1));
    let v00 = vec3<f32>(f32(chan(c00, 0u)), f32(chan(c00, 1u)), f32(chan(c00, 2u)));
    let v10 = vec3<f32>(f32(chan(c10, 0u)), f32(chan(c10, 1u)), f32(chan(c10, 2u)));
    let v01 = vec3<f32>(f32(chan(c01, 0u)), f32(chan(c01, 1u)), f32(chan(c01, 2u)));
    let v11 = vec3<f32>(f32(chan(c11, 0u)), f32(chan(c11, 1u)), f32(chan(c11, 2u)));
    return mix(mix(v00, v10, tx), mix(v01, v11, tx), ty) / 255.0;
}

fn fxaa_pixel(x: u32, y: u32, which: u32, w: u32, h: u32) -> u32 {
    // FXAA (edge-only, subpixel amount 0), in the form of the published FXAA 3.11 algorithm:
    // luma edge test on the 3x3 neighbourhood, gradient direction, two filtered taps.
    let center = load_px(which, y * w + x);
    let xf = f32(x) + 0.5;
    let yf = f32(y) + 0.5;
    let rgb_m = vec3<f32>(f32(chan(center, 0u)), f32(chan(center, 1u)), f32(chan(center, 2u))) / 255.0;
    let lum_m = luma(center);
    let lum_n = luma(load_px(which, u32(clamp(i32(y) - 1, 0, i32(h) - 1)) * w + x));
    let lum_s = luma(load_px(which, u32(clamp(i32(y) + 1, 0, i32(h) - 1)) * w + x));
    let lum_w = luma(load_px(which, y * w + u32(clamp(i32(x) - 1, 0, i32(w) - 1))));
    let lum_e = luma(load_px(which, y * w + u32(clamp(i32(x) + 1, 0, i32(w) - 1))));
    let ny = u32(clamp(i32(y) - 1, 0, i32(h) - 1));
    let sy = u32(clamp(i32(y) + 1, 0, i32(h) - 1));
    let wx = u32(clamp(i32(x) - 1, 0, i32(w) - 1));
    let ex = u32(clamp(i32(x) + 1, 0, i32(w) - 1));
    let lum_nw = luma(load_px(which, ny * w + wx));
    let lum_ne = luma(load_px(which, ny * w + ex));
    let lum_sw = luma(load_px(which, sy * w + wx));
    let lum_se = luma(load_px(which, sy * w + ex));
    let lum_min = min(lum_m, min(min(lum_n, lum_s), min(lum_w, lum_e)));
    let lum_max = max(lum_m, max(max(lum_n, lum_s), max(lum_w, lum_e)));
    let range = lum_max - lum_min;
    let threshold = max(0.0312, lum_max * 0.125);
    if (range < threshold) {
        return center;
    }
    // Edge direction from the corner sums (perpendicular to the edge).
    var dir = vec2<f32>(-((lum_nw + lum_ne) - (lum_sw + lum_se)), ((lum_nw + lum_sw) - (lum_ne + lum_se)));
    let dir_reduce = max((lum_nw + lum_ne + lum_sw + lum_se) * (0.25 * 0.125), 1.0 / 128.0);
    let rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, vec2<f32>(-8.0, -8.0), vec2<f32>(8.0, 8.0));
    let p1 = vec2<f32>(xf, yf) + dir * (1.0 / 3.0 - 0.5);
    let p2 = vec2<f32>(xf, yf) + dir * (2.0 / 3.0 - 0.5);
    let p3 = vec2<f32>(xf, yf) + dir * (-0.5);
    let p4 = vec2<f32>(xf, yf) + dir * (0.5);
    let rgb_a = 0.5 * (fxaa_sample(which, p1.x, p1.y, w, h) + fxaa_sample(which, p2.x, p2.y, w, h));
    let rgb_b = rgb_a * 0.5 + 0.25 * (fxaa_sample(which, p3.x, p3.y, w, h) + fxaa_sample(which, p4.x, p4.y, w, h));
    let lum_b = dot(rgb_b, vec3<f32>(0.299, 0.587, 0.114));
    var rgb_out = rgb_b;
    if (lum_b < lum_min || lum_b > lum_max) {
        rgb_out = rgb_a;
    }
    let r = u32(clamp(rgb_out.r * 255.0 + 0.5, 0.0, 255.0));
    let g = u32(clamp(rgb_out.g * 255.0 + 0.5, 0.0, 255.0));
    let b = u32(clamp(rgb_out.b * 255.0 + 0.5, 0.0, 255.0));
    return pack4(r, g, b, chan(center, 3u));
}

// ---- AMD FidelityFX Super Resolution 1.0: EASU (edge-adaptive upscale) and RCAS (robust contrast
// adaptive sharpening). Transcribed from ffx_fsr1.h (MIT, AMD, copyright 2021; vendored at
// native/third_party/fsr1, see native/THIRD_PARTY.md). Constants come from the host (FsrEasuCon,
// FsrRcasCon) in the parameter record: EASU con0..con3 at words 16..31, RCAS sharpness at word 16.
// The gather order is the one textureGather returns: (bottom-left, bottom-right, top-right, top-left)
// of the 2x2 block at floor(p * size - 0.5), with clamp-to-edge.

fn fsr_texel(which: u32, x: i32, y: i32, w: u32, h: u32) -> vec3<f32> {
    let cx = u32(clamp(x, 0, i32(w) - 1));
    let cy = u32(clamp(y, 0, i32(h) - 1));
    let c = load_px(which, cy * w + cx);
    return vec3<f32>(f32(chan(c, 0u)), f32(chan(c, 1u)), f32(chan(c, 2u))) / 255.0;
}

// Gather of one channel at continuous position p (texel units from the normalised coordinate).
fn fsr_gather(which: u32, p: vec2<f32>, w: u32, h: u32, channel: u32) -> vec4<f32> {
    let x0 = i32(floor(p.x * f32(w) - 0.5));
    let y0 = i32(floor(p.y * f32(h) - 0.5));
    let bl = fsr_texel(which, x0, y0 + 1, w, h)[channel];
    let br = fsr_texel(which, x0 + 1, y0 + 1, w, h)[channel];
    let tr = fsr_texel(which, x0 + 1, y0, w, h)[channel];
    let tl = fsr_texel(which, x0, y0, w, h)[channel];
    return vec4<f32>(bl, br, tr, tl);
}

// APrxLoRcpF1, APrxMedRcpF1 and APrxLoRsqF1 of ffx_a.h (bit-trick approximations).
fn fsr_rcp_lo(a: f32) -> f32 {
    return bitcast<f32>(0x7ef07ebbu - bitcast<u32>(a));
}

fn fsr_rcp_med(a: f32) -> f32 {
    let b = bitcast<f32>(0x7ef19fffu - bitcast<u32>(a));
    return b * (-b * a + 2.0);
}

fn fsr_rsq_lo(a: f32) -> f32 {
    return bitcast<f32>(0x5f347d74u - (bitcast<u32>(a) >> 1u));
}

// FsrEasuSetF: the gradient direction and length contribution of one bilinear tap group.
fn fsr_easu_set(w: f32, la: f32, lb: f32, lc: f32, ld: f32, le: f32) -> vec3<f32> {
    let dc = ld - lc;
    let cb = lc - lb;
    var len_x = max(abs(dc), abs(cb));
    len_x = fsr_rcp_lo(len_x);
    let dir_x = ld - lb;
    len_x = clamp(abs(dir_x) * len_x, 0.0, 1.0);
    len_x = len_x * len_x;
    let ec = le - lc;
    let ca = lc - la;
    var len_y = max(abs(ec), abs(ca));
    len_y = fsr_rcp_lo(len_y);
    let dir_y = le - la;
    len_y = clamp(abs(dir_y) * len_y, 0.0, 1.0);
    len_y = len_y * len_y;
    return vec3<f32>(dir_x * w, dir_y * w, (len_x + len_y) * w);
}

// FsrEasuTapF: one tap of the 12-tap Lanczos-like filter; returns (weighted colour, weight).
fn fsr_easu_tap(off: vec2<f32>, dir: vec2<f32>, len: vec2<f32>, lob: f32, clp: f32, c: vec3<f32>) -> vec4<f32> {
    var v = vec2<f32>(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x);
    v = v * len;
    var d2 = v.x * v.x + v.y * v.y;
    d2 = min(d2, clp);
    var wb = (2.0 / 5.0) * d2 + (-1.0);
    var wa = lob * d2 + (-1.0);
    wb = wb * wb;
    wa = wa * wa;
    wb = (25.0 / 16.0) * wb + (-(25.0 / 16.0 - 1.0));
    let w = wb * wa;
    return vec4<f32>(c * w, w);
}

fn fsr_easu_pixel(x: u32, y: u32, which: u32, w: u32, h: u32) -> vec3<f32> {
    let con0 = vec4<f32>(prm_f(16u), prm_f(17u), prm_f(18u), prm_f(19u));
    let con1 = vec4<f32>(prm_f(20u), prm_f(21u), prm_f(22u), prm_f(23u));
    let con2 = vec4<f32>(prm_f(24u), prm_f(25u), prm_f(26u), prm_f(27u));
    let con3 = vec4<f32>(prm_f(28u), prm_f(29u), prm_f(30u), prm_f(31u));
    var pp = vec2<f32>(f32(x), f32(y)) * con0.xy + con0.zw;
    let fp = floor(pp);
    pp = pp - fp;
    let p0 = fp * con1.xy + con1.zw;
    let p1 = p0 + con2.xy;
    let p2 = p0 + con2.zw;
    let p3 = p0 + con3.xy;
    let bczz_r = fsr_gather(which, p0, w, h, 0u);
    let bczz_g = fsr_gather(which, p0, w, h, 1u);
    let bczz_b = fsr_gather(which, p0, w, h, 2u);
    let ijfe_r = fsr_gather(which, p1, w, h, 0u);
    let ijfe_g = fsr_gather(which, p1, w, h, 1u);
    let ijfe_b = fsr_gather(which, p1, w, h, 2u);
    let klhg_r = fsr_gather(which, p2, w, h, 0u);
    let klhg_g = fsr_gather(which, p2, w, h, 1u);
    let klhg_b = fsr_gather(which, p2, w, h, 2u);
    let zzon_r = fsr_gather(which, p3, w, h, 0u);
    let zzon_g = fsr_gather(which, p3, w, h, 1u);
    let zzon_b = fsr_gather(which, p3, w, h, 2u);
    let bczz_l = bczz_b * 0.5 + (bczz_r * 0.5 + bczz_g);
    let ijfe_l = ijfe_b * 0.5 + (ijfe_r * 0.5 + ijfe_g);
    let klhg_l = klhg_b * 0.5 + (klhg_r * 0.5 + klhg_g);
    let zzon_l = zzon_b * 0.5 + (zzon_r * 0.5 + zzon_g);
    let bL = bczz_l.x;
    let cL = bczz_l.y;
    let iL = ijfe_l.x;
    let jL = ijfe_l.y;
    let fL = ijfe_l.z;
    let eL = ijfe_l.w;
    let kL = klhg_l.x;
    let lL = klhg_l.y;
    let hL = klhg_l.z;
    let gL = klhg_l.w;
    let oL = zzon_l.z;
    let nL = zzon_l.w;
    let w_s = (1.0 - pp.x) * (1.0 - pp.y);
    let w_t = pp.x * (1.0 - pp.y);
    let w_u = (1.0 - pp.x) * pp.y;
    let w_v = pp.x * pp.y;
    var acc = fsr_easu_set(w_s, bL, eL, fL, gL, jL);
    acc = acc + fsr_easu_set(w_t, cL, fL, gL, hL, kL);
    acc = acc + fsr_easu_set(w_u, fL, iL, jL, kL, nL);
    acc = acc + fsr_easu_set(w_v, gL, jL, kL, lL, oL);
    var dir = acc.xy;
    var len = acc.z;
    let dir2 = dir * dir;
    var dir_r = dir2.x + dir2.y;
    let zro = dir_r < (1.0 / 32768.0);
    dir_r = fsr_rsq_lo(dir_r);
    if (zro) {
        dir_r = 1.0;
        dir.x = 1.0;
    }
    dir = dir * dir_r;
    len = len * 0.5;
    len = len * len;
    let stretch = (dir.x * dir.x + dir.y * dir.y) * fsr_rcp_lo(max(abs(dir.x), abs(dir.y)));
    let len2 = vec2<f32>(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
    let lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    let clp = fsr_rcp_lo(lob);
    let min4 = min(min(min(ijfe_r.z, klhg_r.w), ijfe_r.y), klhg_r.x);
    let min4_g = min(min(min(ijfe_g.z, klhg_g.w), ijfe_g.y), klhg_g.x);
    let min4_b = min(min(min(ijfe_b.z, klhg_b.w), ijfe_b.y), klhg_b.x);
    let max4 = max(max(max(ijfe_r.z, klhg_r.w), ijfe_r.y), klhg_r.x);
    let max4_g = max(max(max(ijfe_g.z, klhg_g.w), ijfe_g.y), klhg_g.x);
    let max4_b = max(max(max(ijfe_b.z, klhg_b.w), ijfe_b.y), klhg_b.x);
    // Tap colours as (R, G, B) from the gathers; offsets are relative to the resolve position.
    var ac = vec3<f32>(0.0, 0.0, 0.0);
    var aw = 0.0;
    var t = vec4<f32>(0.0);
    t = fsr_easu_tap(vec2<f32>(0.0, -1.0) - pp, dir, len2, lob, clp, vec3<f32>(bczz_r.x, bczz_g.x, bczz_b.x)); ac = ac + t.xyz; aw = aw + t.w; // b
    t = fsr_easu_tap(vec2<f32>(1.0, -1.0) - pp, dir, len2, lob, clp, vec3<f32>(bczz_r.y, bczz_g.y, bczz_b.y)); ac = ac + t.xyz; aw = aw + t.w; // c
    t = fsr_easu_tap(vec2<f32>(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3<f32>(ijfe_r.x, ijfe_g.x, ijfe_b.x)); ac = ac + t.xyz; aw = aw + t.w; // i
    t = fsr_easu_tap(vec2<f32>(0.0, 1.0) - pp, dir, len2, lob, clp, vec3<f32>(ijfe_r.y, ijfe_g.y, ijfe_b.y)); ac = ac + t.xyz; aw = aw + t.w; // j
    t = fsr_easu_tap(vec2<f32>(0.0, 0.0) - pp, dir, len2, lob, clp, vec3<f32>(ijfe_r.z, ijfe_g.z, ijfe_b.z)); ac = ac + t.xyz; aw = aw + t.w; // f
    t = fsr_easu_tap(vec2<f32>(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3<f32>(ijfe_r.w, ijfe_g.w, ijfe_b.w)); ac = ac + t.xyz; aw = aw + t.w; // e
    t = fsr_easu_tap(vec2<f32>(1.0, 1.0) - pp, dir, len2, lob, clp, vec3<f32>(klhg_r.x, klhg_g.x, klhg_b.x)); ac = ac + t.xyz; aw = aw + t.w; // k
    t = fsr_easu_tap(vec2<f32>(2.0, 1.0) - pp, dir, len2, lob, clp, vec3<f32>(klhg_r.y, klhg_g.y, klhg_b.y)); ac = ac + t.xyz; aw = aw + t.w; // l
    t = fsr_easu_tap(vec2<f32>(2.0, 0.0) - pp, dir, len2, lob, clp, vec3<f32>(klhg_r.z, klhg_g.z, klhg_b.z)); ac = ac + t.xyz; aw = aw + t.w; // h
    t = fsr_easu_tap(vec2<f32>(1.0, 0.0) - pp, dir, len2, lob, clp, vec3<f32>(klhg_r.w, klhg_g.w, klhg_b.w)); ac = ac + t.xyz; aw = aw + t.w; // g
    t = fsr_easu_tap(vec2<f32>(1.0, 2.0) - pp, dir, len2, lob, clp, vec3<f32>(zzon_r.z, zzon_g.z, zzon_b.z)); ac = ac + t.xyz; aw = aw + t.w; // o
    t = fsr_easu_tap(vec2<f32>(0.0, 2.0) - pp, dir, len2, lob, clp, vec3<f32>(zzon_r.w, zzon_g.w, zzon_b.w)); ac = ac + t.xyz; aw = aw + t.w; // n
    let lo = vec3<f32>(min4, min4_g, min4_b);
    let hi = vec3<f32>(max4, max4_g, max4_b);
    let avg = ac * (1.0 / aw);
    return min(hi, max(lo, avg));
}

fn fsr_rcas_pixel(x: u32, y: u32, which: u32, w: u32, h: u32) -> vec3<f32> {
    let con = prm_f(16u);
    let xi = i32(x);
    let yi = i32(y);
    let b = fsr_texel(which, xi, yi - 1, w, h);
    let d = fsr_texel(which, xi - 1, yi, w, h);
    let e = fsr_texel(which, xi, yi, w, h);
    let f = fsr_texel(which, xi + 1, yi, w, h);
    let hh = fsr_texel(which, xi, yi + 1, w, h);
    let mn4 = min(min(min(b, d), f), hh);
    let mx4 = max(max(max(b, d), f), hh);
    let hit_min = min(mn4, e) * (1.0 / (4.0 * mx4));
    let hit_max = (vec3<f32>(1.0) - max(mx4, e)) * (1.0 / (4.0 * mn4 + vec3<f32>(-4.0)));
    let lobe_c = max(-hit_min, hit_max);
    let lobe_m = max(max(lobe_c.x, lobe_c.y), lobe_c.z);
    let lobe = max(-(0.25 - 1.0 / 16.0), min(lobe_m, 0.0)) * con;
    let rcp_l = fsr_rcp_med(4.0 * lobe + 1.0);
    return (lobe * b + lobe * d + lobe * hh + lobe * f + e) * rcp_l;
}

fn fsr_store(which: u32, i: u32, c: vec3<f32>) {
    let r = u32(clamp(c.r * 255.0 + 0.5, 0.0, 255.0));
    let g = u32(clamp(c.g * 255.0 + 0.5, 0.0, 255.0));
    let b = u32(clamp(c.b * 255.0 + 0.5, 0.0, 255.0));
    store_px(which, i, pack4(r, g, b, 255u));
}

@compute @workgroup_size(8, 8)
fn post(@builtin(global_invocation_id) gid: vec3<u32>) {
    let op = prm(P_OP);
    let w = prm(P_W);
    let h = prm(P_H);
    let ow = prm(P_OW);
    let oh = prm(P_OH);
    if (gid.x >= ow || gid.y >= oh) {
        return;
    }
    let x = gid.x;
    let y = gid.y;
    let i = y * ow + x;
    let src_buf = prm(P_IN);
    let dst_buf = prm(P_OUT);
    let slot_words = prm(P_SLOT_WORDS);
    let prev_output_base = 2u * slot_words;
    if (op == OP_COPY) {
        store_px(dst_buf, i, load_px(src_buf, i));
    } else if (op == OP_WEAVE) {
        // Rows of the current field come from the input; the other rows keep the previous output.
        let field = prm(P_FIELD);
        let has_prev = (prm(P_FLAGS) & 1u) != 0u;
        var woven = load_px(src_buf, i);
        if (has_prev && (y & 1u) != field) {
            woven = history[prev_output_base + i];
        }
        history[prev_output_base + i] = woven;
        store_px(dst_buf, i, woven);
    } else if (op == OP_BOB) {
        let shift = i32(prm(P_SHIFT));
        let source_row = clamp(i32(y) - shift, 0, i32(h) - 1);
        store_px(dst_buf, i, load_px(src_buf, u32(source_row) * w + x));
    } else if (op == OP_BLEND_TAP) {
        // Input is the woven image in history slot 2 (written by the preceding weave dispatch).
        let up = u32(max(i32(y) - 1, 0));
        let down = u32(min(i32(y) + 1, i32(h) - 1));
        let a = history[prev_output_base + up * w + x];
        let b = history[prev_output_base + y * w + x];
        let c = history[prev_output_base + down * w + x];
        store_px(dst_buf, i, tap3(a, b, c));
    } else if (op == OP_ADAPTIVE) {
        let field = prm(P_FIELD);
        let flags = prm(P_FLAGS);
        let has_prev_output = (flags & 1u) != 0u;
        let has_prev_for_parity = ((flags >> (1u + field)) & 1u) != 0u;
        let current = load_px(src_buf, i);
        let current_row = (y & 1u) == field;
        let edge_row = y == 0u || y + 1u == h;
        var result_px = current;
        if (!(current_row || edge_row)) {
            let up = load_px(src_buf, (y - 1u) * w + x);
            let down = load_px(src_buf, (y + 1u) * w + x);
            let bob = average(up, down);
            var motion = true;
            if (has_prev_for_parity && has_prev_output) {
                let parity_base = field * slot_words;
                let prev_up = history[parity_base + (y - 1u) * w + x];
                let prev_down = history[parity_base + (y + 1u) * w + x];
                let static_line = history[prev_output_base + i];
                let threshold = prm_f(P_THRESHOLD);
                motion = exceeds(up, prev_up, threshold) || exceeds(down, prev_down, threshold) || exceeds(bob, static_line, threshold);
            }
            if (motion || !has_prev_output) {
                result_px = bob;
            } else {
                result_px = history[prev_output_base + i];
            }
        }
        history[prev_output_base + i] = result_px;
        store_px(dst_buf, i, result_px);
    } else if (op == OP_HISTORY_COPY) {
        history[prm(P_SLOT) * slot_words + i] = load_px(src_buf, i);
    } else if (op == OP_FXAA) {
        store_px(dst_buf, i, fxaa_pixel(x, y, src_buf, w, h));
    } else if (op == OP_EASU) {
        fsr_store(dst_buf, i, fsr_easu_pixel(x, y, src_buf, w, h));
    } else if (op == OP_RCAS) {
        fsr_store(dst_buf, i, fsr_rcas_pixel(x, y, src_buf, w, h));
    }
}
