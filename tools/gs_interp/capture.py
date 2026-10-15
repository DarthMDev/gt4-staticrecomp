"""Capture file reader: records, frames and byte offsets of GT4_PGS_CAPTURE streams.

Record layout (little-endian), as written by native/parallel_gs_backend.cpp:
    'G' u8 path, u32 bytes, payload   GIF packet
    'P' u32 address, u64 value        privileged register write
    'R' u32 qwords                    LOCAL->HOST FIFO read
    'V' u64 frame                     end of frame
"""

from __future__ import annotations

import mmap
import struct
from dataclasses import dataclass, field
from typing import Iterator

from .gif import GifStream, encode_events

MAX_GIF_BYTES = 64 * 1024 * 1024


class CaptureError(ValueError):
    """A malformed capture. The message names the byte offset."""


@dataclass(frozen=True)
class Record:
    tag: str
    offset: int
    end: int
    path: int = 0
    payload: bytes = b""
    address: int = 0
    value: int = 0
    frame: int = 0


def iter_records(data) -> Iterator[Record]:
    """Yield every record of a capture; data is bytes-like (mmap or bytes)."""
    size = len(data)
    position = 0
    while position < size:
        offset = position
        tag = chr(data[position])
        position += 1
        if tag == "G":
            if position + 5 > size:
                raise CaptureError(f"offset {offset}: truncated GIF header")
            path = data[position]
            (count,) = struct.unpack_from("<I", data, position + 1)
            position += 5
            if path > 3 or count > MAX_GIF_BYTES or position + count > size:
                raise CaptureError(f"offset {offset}: bad GIF packet (path {path}, {count} bytes)")
            payload = bytes(data[position:position + count])
            position += count
            yield Record("G", offset, position, path=path, payload=payload)
        elif tag == "P":
            if position + 12 > size:
                raise CaptureError(f"offset {offset}: truncated privileged write")
            address, value = struct.unpack_from("<IQ", data, position)
            position += 12
            yield Record("P", offset, position, address=address, value=value)
        elif tag == "R":
            if position + 4 > size:
                raise CaptureError(f"offset {offset}: truncated FIFO read")
            (qwords,) = struct.unpack_from("<I", data, position)
            position += 4
            yield Record("R", offset, position, value=qwords)
        elif tag == "V":
            if position + 8 > size:
                raise CaptureError(f"offset {offset}: truncated frame end")
            (frame,) = struct.unpack_from("<Q", data, position)
            position += 8
            yield Record("V", offset, position, frame=frame)
        else:
            raise CaptureError(f"offset {offset}: unknown record tag {ord(tag):#04x}")


@dataclass
class FrameStream:
    """One frame: its ordered GS events and the byte range of its records in the capture."""

    number: int
    start: int
    end: int
    events: list = field(default_factory=list)


def decode_frames(path: str, wanted: set | None = None, last: int | None = None):
    """Decode a capture into frames, yielding FrameStream objects in order.

    wanted: frame numbers whose events are kept (others yield an empty event list).
    last: stop after the frame with this number.
    The GIF latch and the byte ranges are tracked for every frame, so state carries over.
    """
    stream = GifStream()
    with open(path, "rb") as handle, mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as data:
        frame_start = 0
        events: list = []
        for record in iter_records(data):
            if record.tag == "G":
                produced = stream.feed(record.path, record.payload)
                events.extend(produced)
            elif record.tag == "P":
                events.append(("P", record.address, record.value))
            elif record.tag == "R":
                events.append(("R", record.value))
            elif record.tag == "V":
                keep = wanted is None or record.frame in wanted
                yield FrameStream(record.frame, frame_start, record.end,
                                  events if keep else [])
                frame_start = record.end
                events = []
                if last is not None and record.frame >= last:
                    return


def frame_index(path: str) -> list:
    """(frame number, start byte, end byte) of every frame, in order."""
    index = []
    with open(path, "rb") as handle, mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as data:
        for record in iter_records(data):
            if record.tag == "V":
                index.append((record.frame, record.offset, record.end))
    return index


def encode_frame_records(events: list, frame_number: int) -> bytes:
    """Serialize one frame's events followed by its 'V' record as capture bytes."""
    out = bytearray()
    for record in encode_events_mixed(events):
        out += serialize_record(record)
    out += serialize_record(("V", frame_number))
    return bytes(out)


def encode_events_mixed(events: list) -> list:
    """Split events at P/R markers so that GIF runs are encoded in order."""
    records: list = []
    chunk: list = []
    for event in events:
        if event[0] in ("P", "R"):
            records.extend(encode_events(chunk))
            chunk = []
            records.append(event)
        else:
            chunk.append(event)
    records.extend(encode_events(chunk))
    return records


def serialize_record(record: tuple) -> bytes:
    kind = record[0]
    if kind == "G":
        _, path, payload = record
        return b"G" + bytes([path]) + struct.pack("<I", len(payload)) + payload
    if kind == "P":
        _, address, value = record
        return b"P" + struct.pack("<IQ", address, value)
    if kind == "R":
        return b"R" + struct.pack("<I", record[1])
    if kind == "V":
        return b"V" + struct.pack("<Q", record[1])
    raise ValueError(f"unknown record kind {kind}")
