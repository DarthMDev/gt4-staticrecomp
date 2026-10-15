"""Primitive assembly from decoded GS events, with the drawing context of each primitive.

A primitive is formed by vertex kicks (XYZ registers) following a PRIM write, as in the GS
user manual: point, line, line strip, triangle, triangle strip, triangle fan, sprite.
Strips and fans share vertices between primitives; those primitives are flagged `shared`
and are never substituted by the interpolation (the vertex kicks cannot belong to two
different synthetic positions).
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .gif import (
    KICK_REGISTERS,
    REG_FRAME_1,
    REG_FRAME_2,
    REG_PRIM,
    REG_RGBAQ,
    REG_ST,
    REG_TEX0_1,
    REG_TEX0_2,
    REG_ZBUF_1,
    REG_ZBUF_2,
)

PRIM_POINT = 0
PRIM_LINE = 1
PRIM_LINE_STRIP = 2
PRIM_TRIANGLE = 3
PRIM_TRIANGLE_STRIP = 4
PRIM_TRIANGLE_FAN = 5
PRIM_SPRITE = 6
PRIM_NAMES = {
    PRIM_POINT: "point",
    PRIM_LINE: "line",
    PRIM_LINE_STRIP: "line_strip",
    PRIM_TRIANGLE: "triangle",
    PRIM_TRIANGLE_STRIP: "triangle_strip",
    PRIM_TRIANGLE_FAN: "triangle_fan",
    PRIM_SPRITE: "sprite",
}
SHARED_KINDS = (PRIM_LINE_STRIP, PRIM_TRIANGLE_STRIP, PRIM_TRIANGLE_FAN)


@dataclass
class Vertex:
    event_index: int  # index of the kick event in the frame's event list
    register: int  # XYZ2, XYZF2, XYZ3 or XYZF3
    xyz: int  # the 64-bit register value at the kick
    rgbaq: int  # latched RGBAQ (Q in the high half) at the kick
    st: int  # latched ST at the kick


@dataclass
class Primitive:
    frame: int
    kind: int
    key: tuple  # (kind, TEX0, FRAME, ZBUF) of the active context
    vertices: list = field(default_factory=list)
    shared: bool = False

    @property
    def name(self) -> str:
        return PRIM_NAMES[self.kind]


class PrimitiveBuilder:
    """Feeds events of successive frames; keeps the register state across frames."""

    def __init__(self) -> None:
        self.prim = 0
        self.tex0 = [0, 0]
        self.frame_reg = [0, 0]
        self.zbuf = [0, 0]
        self.rgbaq = 0
        self.st = 0
        self.window: list = []
        self.kicks_since_prim = 0

    def context_key(self) -> tuple:
        context = (self.prim >> 9) & 1
        return (self.prim & 7, self.tex0[context], self.frame_reg[context], self.zbuf[context])

    def build(self, frame: int, events: list) -> list:
        primitives: list = []
        for index, event in enumerate(events):
            if event[0] == "kick":
                self._kick_event(frame, index, event, primitives)
                continue
            if event[0] != "op":
                continue
            _, _path, register, value = event
            if register == REG_PRIM:
                self.prim = value & 0x7FF
                self.window = []
                self.kicks_since_prim = 0
            elif register == REG_TEX0_1:
                self.tex0[0] = value
            elif register == REG_TEX0_2:
                self.tex0[1] = value
            elif register == REG_FRAME_1:
                self.frame_reg[0] = value
            elif register == REG_FRAME_2:
                self.frame_reg[1] = value
            elif register == REG_ZBUF_1:
                self.zbuf[0] = value
            elif register == REG_ZBUF_2:
                self.zbuf[1] = value
            elif register == REG_RGBAQ:
                self.rgbaq = value
            elif register == REG_ST:
                self.st = value
        return primitives

    def _kick_event(self, frame: int, index: int, event: tuple, out: list) -> None:
        _, _path, register, value, drawing = event
        if drawing:  # XYZ3/XYZF3 and ADC kicks never draw
            self._kick(frame, index, register, value, out)

    def _kick(self, frame: int, index: int, register: int, value: int, out: list) -> None:
        vertex = Vertex(index, register, value, self.rgbaq, self.st)
        kind = self.prim & 7
        self.kicks_since_prim += 1
        self.window.append(vertex)
        if kind == PRIM_POINT:
            self._emit(frame, kind, [vertex], out)
            self.window = []
        elif kind in (PRIM_LINE, PRIM_SPRITE):
            if len(self.window) == 2:
                self._emit(frame, kind, list(self.window), out)
                self.window = []
        elif kind == PRIM_TRIANGLE:
            if len(self.window) == 3:
                self._emit(frame, kind, list(self.window), out)
                self.window = []
        elif kind == PRIM_LINE_STRIP:
            if len(self.window) >= 2:
                self._emit(frame, kind, self.window[-2:], out, shared=True)
                self.window = self.window[-2:]
        elif kind == PRIM_TRIANGLE_STRIP:
            if len(self.window) >= 3:
                self._emit(frame, kind, self.window[-3:], out, shared=True)
                self.window = self.window[-2:]
        elif kind == PRIM_TRIANGLE_FAN:
            if len(self.window) >= 3:
                self._emit(frame, kind, [self.window[0], self.window[-2], self.window[-1]], out,
                           shared=True)
                self.window = [self.window[0], self.window[-1]]

    def _emit(self, frame: int, kind: int, vertices: list, out: list, shared: bool = False) -> None:
        out.append(Primitive(frame=frame, kind=kind, key=self.context_key(),
                             vertices=list(vertices), shared=shared or kind in SHARED_KINDS))
