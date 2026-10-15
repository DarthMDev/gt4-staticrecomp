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
    }
}
