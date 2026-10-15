"""Synthetic frames: lerp (or extrapolate) matched primitives between two captured frames.

For a matched, substitutable pair (A, B) and a parameter t, each vertex of B is replaced by
  XYZ      linear in t (x, y, z, and fog for the F formats), rounded and clamped to the field
  RGBA     linear in t, clamped to 0..255
  ST       perspective-correct: S/Q, T/Q and 1/Q are linear in t; the new Q is 1/(1/Q)
The replaced vertex is written just before its kick, with the RGBAQ and ST values it needs,
and the latched RGBAQ and ST of B are restored after the primitive's last kick. Every other
event of frame B (uploads, register state, render-to-texture, privileged writes) is kept in
its original order. Primitives without a valid pair keep B's geometry.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass

from .gif import REG_RGBAQ, REG_ST, REG_XYZF2, REG_XYZF3
from .match import MatchResult, match_primitives, relaxed_key


def _f32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def _bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _clamp(value: float, low: float, high: float) -> int:
    return int(min(max(round(value), low), high))


@dataclass(frozen=True)
class VertexValue:
    xyz: int
    rgbaq: int
    st: int


def lerp_vertex(a, b, register: int, t: float):
    """Interpolated (VertexValue) between two Vertex records, or None when ST is undefined."""
    xa, ya, za = a.xyz & 0xFFFF, (a.xyz >> 16) & 0xFFFF, a.xyz >> 32
    xb, yb, zb = b.xyz & 0xFFFF, (b.xyz >> 16) & 0xFFFF, b.xyz >> 32
    x = _clamp(xa + (xb - xa) * t, 0, 0xFFFF)
    y = _clamp(ya + (yb - ya) * t, 0, 0xFFFF)
    if register in (REG_XYZF2, REG_XYZF3):
        za24, zb24 = za & 0xFFFFFF, zb & 0xFFFFFF
        z = _clamp(za24 + (zb24 - za24) * t, 0, 0xFFFFFF)
        fa, fb = a.xyz >> 56, b.xyz >> 56
        fog = _clamp(fa + (fb - fa) * t, 0, 0xFF)
        xyz = x | (y << 16) | (z << 32) | (fog << 56)
    else:
        z = _clamp(za + (zb - za) * t, 0, 0xFFFFFFFF)
        xyz = x | (y << 16) | (z << 32)

    qa = _f32(a.rgbaq >> 32)
    qb = _f32(b.rgbaq >> 32)
    if not (math.isfinite(qa) and math.isfinite(qb)) or qa == 0.0 or qb == 0.0:
        return None
    inverse_w = (1.0 - t) / qa + t / qb
    if not (math.isfinite(inverse_w) and inverse_w > 0.0):
        return None
    q_new = 1.0 / inverse_w
    rgba = 0
    for shift in (0, 8, 16, 24):
        ca = (a.rgbaq >> shift) & 0xFF
        cb = (b.rgbaq >> shift) & 0xFF
        rgba |= _clamp(ca + (cb - ca) * t, 0, 0xFF) << shift
    rgbaq = rgba | (_bits(q_new) << 32)

    sa, ta = _f32(a.st & 0xFFFFFFFF), _f32(a.st >> 32)
    sb, tb = _f32(b.st & 0xFFFFFFFF), _f32(b.st >> 32)
    s_over_q = (1.0 - t) * (sa / qa) + t * (sb / qb)
    t_over_q = (1.0 - t) * (ta / qa) + t * (tb / qb)
    s_new = s_over_q * q_new
    t_new = t_over_q * q_new
    if not all(math.isfinite(v) for v in (s_new, t_new)):
        return None
    st = _bits(s_new) | (_bits(t_new) << 32)
    return VertexValue(xyz=xyz, rgbaq=rgbaq, st=st)


# Page-selecting state: the draw targets (FRAME_1/2, ZBUF_1/2) and the second display buffer
# address (DISPFB2, GS privileged 0x12000090). The menu alternates two pages from frame to frame,
# so these values follow the frame parity, not the later frame.
PAGE_REGISTERS = (0x4C, 0x4D, 0x4E, 0x4F)
PAGE_PRIVILEGED = (0x12000090,)


@dataclass
class SynthesisResult:
    events: list
    match: MatchResult
    substituted: int
    rejected: int  # matched but not substitutable (shared, or invalid ST / Q)
    page_remapped: int = 0


def page_key(event):
    if event[0] == "op" and event[2] in PAGE_REGISTERS:
        return ("op", event[2])
    if event[0] == "P" and event[1] in PAGE_PRIVILEGED:
        return ("P", event[1])
    return None


def remap_page_state(events_b: list, events_page: list) -> tuple:
    """Replace the page-selecting writes of B by the values of the same-parity frame.

    The k-th page write of one kind in B takes the k-th value of that kind in events_page.
    Returns (events, number of writes replaced).
    """
    values: dict = {}
    for event in events_page:
        key = page_key(event)
        if key is not None:
            values.setdefault(key, []).append(event)
    seen: dict = {}
    out: list = []
    replaced = 0
    for event in events_b:
        key = page_key(event)
        if key is None or key not in values:
            out.append(event)
            continue
        position = seen.get(key, 0)
        seen[key] = position + 1
        if position >= len(values[key]):
            out.append(event)
            continue
        source = values[key][position]
        if event[0] == "op":
            out.append(("op", event[1], event[2], source[3]))
        else:
            out.append(("P", event[1], source[2]))
        replaced += 1
    return out, replaced


def synthesize(events_b: list, prims_a: list, prims_b: list, t: float,
               events_page: list | None = None, key=None) -> SynthesisResult:
    """Rewrite the events of frame B for the geometry interpolated at parameter t from frame A.

    events_page: events of the frame with the target's parity (N-2 for target N); its page
    writes replace B's. Without it, B's page writes are kept.
    """
    page_replaced = 0
    if events_page is not None:
        events_b, page_replaced = remap_page_state(events_b, events_page)
    match = match_primitives(prims_a, prims_b, key or relaxed_key)
    replacements: dict = {}
    after: dict = {}
    substituted = 0
    rejected = 0
    for index_b, index_a in match.pairs.items():
        pb = prims_b[index_b]
        pa = prims_a[index_a]
        if pb.shared:
            rejected += 1
            continue
        values = []
        for vertex_a, vertex_b in zip(pa.vertices, pb.vertices):
            value = lerp_vertex(vertex_a, vertex_b, vertex_b.register, t)
            if value is None:
                break
            values.append(value)
        if len(values) != len(pb.vertices):
            rejected += 1
            continue
        last = pb.vertices[-1]
        for vertex, value in zip(pb.vertices, values):
            replacements[vertex.event_index] = ("vertex", vertex, value)
        after[last.event_index] = (last.rgbaq, last.st)
        substituted += 1

    events: list = []
    for index, event in enumerate(events_b):
        if index in replacements:
            _, vertex, value = replacements[index]
            _, path, register, _old, drawing = event
            events.append(("op", path, REG_RGBAQ, value.rgbaq))
            events.append(("op", path, REG_ST, value.st))
            events.append(("kick", path, register, value.xyz, drawing))
        else:
            events.append(event)
        if index in after:
            rgbaq, st = after[index]
            path = event[1]
            events.append(("op", path, REG_RGBAQ, rgbaq))
            events.append(("op", path, REG_ST, st))
    return SynthesisResult(events=events, match=match, substituted=substituted, rejected=rejected,
                           page_remapped=page_replaced)
