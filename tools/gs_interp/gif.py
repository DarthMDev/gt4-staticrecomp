"""GIF packet decoding and encoding for GT4_PGS_CAPTURE streams.

The record layout is the one written by the donor-side capture
(native/parallel_gs_backend.cpp, StreamCapture). The GIF tag layout follows the GS user
manual: NLOOP 0..14, EOP 15, PRE 46, PRIM 47..57, FLG 58..59, NREG 60..63, with NREG 0
meaning 16. Each GIF path keeps its own byte stream: a tag or its data may continue into
the next packet of the same path, so decoding buffers bytes per path.

The packed-format rules (descriptors, ADC bit 111, the Q latch reset at each tag with
NLOOP > 0, Q sanitising) follow the TASK-026 clean-room GIF parser
(native/hw_gs/frontend/hw_gif_parser.cpp on task/027-hw-renderer).

Decoded form is a flat list of events:
  ('op', path, register, value64)             one GS register write
  ('kick', path, register, value64, drawing)  an XYZ kick; drawing is False for XYZ3/XYZF3
                                              and for XYZ2/XYZF2 with the ADC bit set
  ('img', path, tag16, data)                  an IMAGE-mode transfer, copied verbatim

Encoding writes 'op' events as A+D quads and 'kick' events as PACKED quads with their own
descriptor, so the ADC and drawing semantics survive a round trip.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

FLG_PACKED = 0
FLG_REGLIST = 1
FLG_IMAGE = 2

REG_PRIM = 0x00
REG_RGBAQ = 0x01
REG_ST = 0x02
REG_UV = 0x03
REG_XYZF2 = 0x04
REG_XYZ2 = 0x05
REG_FOG = 0x0A
REG_XYZF3 = 0x0C
REG_XYZ3 = 0x0D
REG_AD = 0x0E
REG_NOP = 0x0F
REG_TEX0_1 = 0x06
REG_TEX0_2 = 0x07
REG_FRAME_1 = 0x4C
REG_FRAME_2 = 0x4D
REG_ZBUF_1 = 0x4E
REG_ZBUF_2 = 0x4F

KICK_REGISTERS = (REG_XYZF2, REG_XYZ2, REG_XYZF3, REG_XYZ3)
GIF_PATH_COUNT = 4
U64_MASK = 0xFFFFFFFFFFFFFFFF
ONE_BITS = 0x3F800000
MIN_NORMAL_BITS = 0x00800000
MAX_FINITE_BITS = 0x7F7FFFFF


@dataclass(frozen=True)
class GifTag:
    raw: bytes
    nloop: int
    eop: bool
    pre: bool
    prim: int
    flg: int
    nreg: int
    regs: tuple

    @property
    def data_bytes(self) -> int:
        if self.flg == FLG_IMAGE:
            return self.nloop * 16
        if self.flg == FLG_PACKED:
            return self.nloop * self.nreg * 16
        return self.nloop * ((self.nreg + 1) // 2) * 16


def parse_tag(raw: bytes) -> GifTag:
    if len(raw) != 16:
        raise ValueError("a GIF tag is 16 bytes")
    lo, hi = struct.unpack("<QQ", raw)
    nreg = ((lo >> 60) & 0xF) or 16
    regs = tuple((hi >> (4 * index)) & 0xF for index in range(nreg))
    return GifTag(
        raw=bytes(raw),
        nloop=lo & 0x7FFF,
        eop=bool((lo >> 15) & 1),
        pre=bool((lo >> 46) & 1),
        prim=(lo >> 47) & 0x7FF,
        flg=(lo >> 58) & 3,
        nreg=nreg,
        regs=regs,
    )


def _is_nan_bits(bits: int) -> bool:
    return (bits & 0x7F800000) == 0x7F800000 and (bits & 0x007FFFFF) != 0


def sanitise_st_q(bits: int) -> int:
    """Q from a packed ST quad: zero becomes the smallest normal, NaN the largest finite."""
    if bits == 0:
        return MIN_NORMAL_BITS
    return MAX_FINITE_BITS if _is_nan_bits(bits) else bits


def sanitise_ad_q(bits: int) -> int:
    """Q from an A+D RGBAQ write: zero becomes 1.0, NaN the largest finite."""
    if bits == 0:
        return ONE_BITS
    return MAX_FINITE_BITS if _is_nan_bits(bits) else bits


class GifStream:
    """Decodes GIF packets of all four paths into events, with the Q latch."""

    def __init__(self) -> None:
        self._buffers = [bytearray() for _ in range(GIF_PATH_COUNT)]
        self.q_bits = ONE_BITS  # latch used by packed RGBA quads

    def pending_bytes(self, path: int) -> int:
        return len(self._buffers[path])

    def feed(self, path: int, payload: bytes) -> list:
        if not 0 <= path < GIF_PATH_COUNT:
            raise ValueError(f"GIF path {path} out of range")
        buffer = self._buffers[path]
        buffer += payload
        events: list = []
        position = 0
        while len(buffer) - position >= 16:
            tag = parse_tag(bytes(buffer[position:position + 16]))
            body = tag.data_bytes
            if len(buffer) - position - 16 < body:
                break
            data = bytes(buffer[position + 16:position + 16 + body])
            position += 16 + body
            if tag.flg == FLG_IMAGE:
                events.append(("img", path, tag.raw, data))
                continue
            if tag.nloop > 0:
                self.q_bits = ONE_BITS
            if tag.pre and tag.flg == FLG_PACKED:
                events.append(("op", path, REG_PRIM, tag.prim))
            if tag.flg == FLG_PACKED:
                self._decode_packed(path, tag, data, events)
            else:
                self._decode_reglist(path, tag, data, events)
        del buffer[:position]
        return events

    def _decode_packed(self, path: int, tag: GifTag, data: bytes, events: list) -> None:
        offset = 0
        for _ in range(tag.nloop):
            for register in tag.regs:
                self._decode_quad(path, register, data[offset:offset + 16], events)
                offset += 16

    def _decode_quad(self, path: int, register: int, quad: bytes, events: list) -> None:
        low, high = struct.unpack("<QQ", quad)
        words = struct.unpack("<IIII", quad)
        if register == REG_PRIM:
            events.append(("op", path, REG_PRIM, low & 0x7FF))
        elif register == REG_RGBAQ:
            rgba = ((words[0] & 0xFF) | ((words[1] & 0xFF) << 8)
                    | ((words[2] & 0xFF) << 16) | ((words[3] & 0xFF) << 24))
            events.append(("op", path, REG_RGBAQ, rgba | (self.q_bits << 32)))
        elif register == REG_ST:
            events.append(("op", path, REG_ST, low))
            self.q_bits = sanitise_st_q(words[2])
        elif register == REG_UV:
            events.append(("op", path, REG_UV, (words[0] & 0x3FFF) | ((words[1] & 0x3FFF) << 16)))
        elif register in KICK_REGISTERS:
            x = words[0] & 0xFFFF
            y = words[1] & 0xFFFF
            if register in (REG_XYZF2, REG_XYZF3):
                z = (words[2] >> 4) & 0xFFFFFF
                fog = (words[3] >> 4) & 0xFF
                value = x | (y << 16) | (z << 32) | (fog << 56)
            else:
                value = x | (y << 16) | (words[2] << 32)
            adc = (words[3] >> 15) & 1
            drawing = register in (REG_XYZF2, REG_XYZ2) and not adc
            events.append(("kick", path, register, value, drawing))
        elif register == REG_FOG:
            events.append(("op", path, REG_FOG, ((words[3] >> 4) & 0xFF) << 56))
        elif register in (0x06, 0x07, 0x08, 0x09):
            events.append(("op", path, register, low))
        elif register == REG_AD:
            address = high & 0x7F
            data = low
            if address == REG_RGBAQ:
                data = (data & 0xFFFFFFFF) | (sanitise_ad_q(data >> 32) << 32)
            events.append(("op", path, address, data))
        # Descriptors 0x0B and 0x0F (NOP) write nothing.

    def _decode_reglist(self, path: int, tag: GifTag, data: bytes, events: list) -> None:
        offset = 0
        per_loop = ((tag.nreg + 1) // 2) * 16
        for _ in range(tag.nloop):
            for index, register in enumerate(tag.regs):
                value = struct.unpack_from("<Q", data, offset + 8 * index)[0]
                if register in KICK_REGISTERS:
                    drawing = register in (REG_XYZF2, REG_XYZ2)
                    events.append(("kick", path, register, value, drawing))
                elif register not in (REG_NOP, REG_AD):
                    events.append(("op", path, register, value))
            offset += per_loop


def _packed_tag(nloop: int, register: int) -> bytes:
    """A PACKED tag with one descriptor, NLOOP items, EOP clear."""
    return struct.pack("<QQ", (nloop & 0x7FFF) | (1 << 60), register)


def _kick_quad(register: int, value: int, drawing: bool) -> bytes:
    x = value & 0xFFFF
    y = (value >> 16) & 0xFFFF
    adc = 0 if (drawing or register in (REG_XYZF3, REG_XYZ3)) else 1
    if register in (REG_XYZF2, REG_XYZF3):
        z = ((value >> 32) & 0xFFFFFF) << 4
        fog = ((value >> 56) & 0xFF) << 4
        return struct.pack("<IIII", x, y, z, fog | (adc << 15))
    return struct.pack("<IIII", x, y, (value >> 32) & 0xFFFFFFFF, adc << 15)


def encode_events(events: list) -> list:
    """Turn decoded events into capture records.

    Returns ('G', path, payload) packets, ('P', address, value) and ('R', qwords) passthroughs,
    in their original order. A packet holds consecutive GIF events of one path: one A+D tag per
    run of 'op' events and one PACKED tag per 'kick'. The last tag of each packet has EOP set.
    """
    records: list = []
    state = {"payload": bytearray(), "path": None, "last_tag": None}
    op_run: list = []

    def flush_ops() -> None:
        for start in range(0, len(op_run), 0x7FFF):
            chunk = op_run[start:start + 0x7FFF]
            state["last_tag"] = len(state["payload"])
            state["payload"].extend(_packed_tag(len(chunk), REG_AD))
            for register, value in chunk:
                state["payload"].extend(struct.pack("<QQ", value, register))
        op_run.clear()

    def close_packet() -> None:
        flush_ops()
        payload = state["payload"]
        if payload:
            position = state["last_tag"]
            lo, hi = struct.unpack_from("<QQ", payload, position)
            struct.pack_into("<QQ", payload, position, lo | (1 << 15), hi)
            records.append(("G", state["path"], bytes(payload)))
        state["payload"] = bytearray()
        state["path"] = None
        state["last_tag"] = None

    for event in events:
        kind = event[0]
        if kind in ("op", "kick"):
            path = event[1]
            if (state["payload"] or op_run) and path != state["path"]:
                close_packet()
            state["path"] = path
            if kind == "op":
                op_run.append((event[2], event[3]))
            else:
                _, _, register, value, drawing = event
                flush_ops()
                state["last_tag"] = len(state["payload"])
                state["payload"].extend(_packed_tag(1, register))
                state["payload"].extend(_kick_quad(register, value, drawing))
        elif kind == "img":
            close_packet()
            _, path, tag_raw, data = event
            records.append(("G", path, tag_raw + data))
        else:
            close_packet()
            records.append(event)
    close_packet()
    return records
