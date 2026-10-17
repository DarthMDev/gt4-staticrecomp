#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert a GT4Recomp GS capture (.gscap) into a GS dump file (.gs) for offline replay.

Input (.gscap, little-endian). Version 2 (TASK-035, GT4_GS_CAPTURE or tools/gs_stream_record) starts
with the 64-byte "GSSTREAM" header and carries the GS stream protocol records (docs/GS-STREAM-PROTOCOL.md):
GIF parts flagged MORE are joined; PRIV, VSYNC (frame and field) and READ_FIFO map to the version 1 records
below; RESET and CONTROL are counted and not written. Version 1 (GT4_PGS_CAPTURE, task/012; see
docs/GS-PCSX2-POC.md) has no header:

    'G' u8 path, u32 bytes, data     GIF packet bytes (multiple of 16)
    'P' u32 guest address, u64 value privileged GS register write (0x12000000-0x12001FFF)
    'R' u32 qwords                   local-to-host FIFO read request
    'V' u64 frame                    end of a frame (one per VSync)

Output (.gs): the GS dump layout that GS dump replayers read. The layout facts used here
are described in docs/GS-PCSX2-POC.md (section "Dump file layout"); this module is
written from that description, not from any GPL source:

    u32 0xFFFFFFFF                         "new header" marker
    u32 header_size                        size of the header block below
    GSDumpHeader (9 x u32, 36 bytes)       version, state size, serial, CRC, screenshot fields
    serial bytes                           e.g. SCUS-97328
    state bytes (state_size)               GS freeze blob; empty = reset GS
    8192 bytes                             initial privileged register block
    packets, in capture order:
      0x00 u8 path, u32 size, data         transfer (path 3 = "Path1New", used for every packet)
      0x02 u32 qwords                      ReadFIFO request
      0x03 8192 bytes                      privileged register block (before each VSync)
      0x01 u8 field                        VSync

Before each VSync the current 8 KiB privileged block (kept from the 'P' records) is written
as a register packet. The CSR FIELD bit (0x2000 of the CSR word at offset 0x1000) is set from
the frame number, because the real GS toggles it at each VSync and the capture does not record
it. --field-phase selects which parity is odd.

Command line:
    python tools/gscap_to_gsdump/gscap_to_gsdump.py in.gscap out.gs [--serial SCUS-97328]
        [--crc 0] [--field-phase 0|1] [--field-from-stream] [--max-frames N] [--stats-json stats.json]
"""

import argparse
import json
import struct
import sys
from dataclasses import dataclass, field
from typing import BinaryIO, Iterator, Optional, Union

PRIVILEGED_BASE = 0x12000000
PRIVILEGED_SIZE = 0x2000  # 8 KiB, PS2MEM_GS register block
CSR_OFFSET = 0x1000
CSR_FIELD_BIT = 0x2000
NEW_HEADER_MARKER = 0xFFFFFFFF
GS_STATE_VERSION = 9  # GSState::STATE_VERSION at PCSX2 685637d (GS/GSState.h:327)
GSDUMP_HEADER_SIZE = 36  # nine u32 fields
TRANSFER_PATH_NEW = 3  # transfer path byte used for every GS packet, see module docstring
DEFAULT_SERIAL = "SCUS-97328"

PACKET_TRANSFER = 0x00
PACKET_VSYNC = 0x01
PACKET_READ_FIFO = 0x02
PACKET_REGISTERS = 0x03


class CaptureError(ValueError):
    """Raised for a truncated or malformed capture."""


@dataclass
class GsCapRecord:
    kind: str
    path: int = 0
    data: bytes = b""
    address: int = 0
    value: int = 0
    qwords: int = 0
    frame: int = 0
    field: Optional[int] = None  # version 2 VSYNC: the display field of the frame ending here (protocol 1.1)


# --- .gscap version 2 (TASK-035): the GS stream protocol written to a file --------------------------------
# Layout from native/gs_stream/gs_stream_protocol.h and docs/GS-STREAM-PROTOCOL.md (MIT, our own format):
#   FileHeader (64 bytes): char magic[8] = "GSSTREAM", u16 file_version = 2, u16 protocol_major,
#     u16 protocol_minor, u16 header_bytes = 64, u32 record_header_bytes = 16, u32 flags, char serial[16],
#     u64 first_frame, 16 reserved bytes
#   records: u16 type, u16 flags, u32 payload_bytes, u64 sequence, payload, zero padding to 16 bytes
V2_MAGIC = b"GSSTREAM"
V2_FILE_HEADER = struct.Struct("<8sHHHHII16sQ16x")
V2_RECORD_HEADER = struct.Struct("<HHIQ")
V2_FLAG_MORE = 1
V2_GIF, V2_PRIV, V2_VSYNC, V2_READ_FIFO, V2_RESET, V2_CONTROL = 1, 2, 3, 4, 5, 6
V2_RESERVED = (7, 8, 9, 10)  # STATE_SAVE, STATE_LOAD, AUX_FRAME_BEGIN, AUX_FRAME_END


class _Prefixed:
    """A stream whose first bytes were already read (used to sniff the version 2 header)."""

    def __init__(self, prefix: bytes, stream: BinaryIO):
        self.prefix = prefix
        self.stream = stream
        self.position = 0

    def read(self, size: int) -> bytes:
        head = self.prefix[:size]
        self.prefix = self.prefix[size:]
        data = head + (self.stream.read(size - len(head)) if size > len(head) else b"")
        self.position += len(data)
        return data

    def tell(self) -> int:
        return self.position


def read_capture(stream: BinaryIO) -> Iterator[GsCapRecord]:
    """Yield the records of a .gscap stream (version 1 or 2) in file order."""
    head = stream.read(len(V2_MAGIC))
    if head == V2_MAGIC:
        yield from _read_capture_v2(stream)
    else:
        yield from _read_capture_v1(_Prefixed(head, stream))


def _read_capture_v2(stream: BinaryIO) -> Iterator[GsCapRecord]:
    """Version 2 records. GIF parts flagged MORE are joined; RESET ('X'), CONTROL ('C') and reserved ('S')
    records are yielded for counting and are not written to the dump."""
    header = V2_MAGIC + _read_exact(stream, V2_FILE_HEADER.size - len(V2_MAGIC))
    (_, file_version, major, _minor, header_bytes, record_header_bytes, _flags, _serial,
     _first_frame) = V2_FILE_HEADER.unpack(header)
    if file_version != 2 or major != 1 or header_bytes != 64 or record_header_bytes != 16:
        raise CaptureError(f"unsupported .gscap version 2 header (file {file_version}, protocol {major})")
    pending_gif = None  # [path, bytearray] while MORE parts arrive
    expected_sequence = None
    while True:
        raw = stream.read(V2_RECORD_HEADER.size)
        if not raw:
            if pending_gif is not None:
                raise CaptureError("truncated GIF record: last part has the MORE flag")
            return
        if len(raw) != V2_RECORD_HEADER.size:
            raise CaptureError("truncated record header")
        kind, flags, payload_bytes, sequence = V2_RECORD_HEADER.unpack(raw)
        if expected_sequence is not None and sequence != expected_sequence:
            raise CaptureError(f"record sequence {sequence} follows {expected_sequence - 1}")
        expected_sequence = sequence + 1
        padded = (payload_bytes + 15) & ~15
        payload = _read_exact(stream, padded)[:payload_bytes]
        if pending_gif is not None and kind != V2_GIF:
            raise CaptureError("a GIF record with the MORE flag is not followed by its next part")
        if kind == V2_GIF:
            if payload_bytes < 16:
                raise CaptureError("GIF record shorter than its header")
            path, _gif_flags, size = struct.unpack_from("<B3xII", payload, 0)
            if size != payload_bytes - 16 or size % 16:
                raise CaptureError("GIF record size mismatch")
            if pending_gif is None:
                pending_gif = [path, bytearray()]
            elif pending_gif[0] != path:
                raise CaptureError("GIF parts of different paths")
            pending_gif[1] += payload[16:]
            if not flags & V2_FLAG_MORE:
                yield GsCapRecord("G", path=pending_gif[0], data=bytes(pending_gif[1]))
                pending_gif = None
        elif kind == V2_PRIV:
            address, value = struct.unpack_from("<I4xQ", payload, 0)
            yield GsCapRecord("P", address=address, value=value)
        elif kind == V2_VSYNC:
            frame, field_bit = struct.unpack_from("<QB", payload, 0)
            yield GsCapRecord("V", frame=frame, field=field_bit)
        elif kind == V2_READ_FIFO:
            (qwords,) = struct.unpack_from("<I", payload, 0)
            yield GsCapRecord("R", qwords=qwords)
        elif kind == V2_RESET:
            yield GsCapRecord("X", value=struct.unpack_from("<I", payload, 0)[0])
        elif kind == V2_CONTROL:
            yield GsCapRecord("C", value=struct.unpack_from("<I", payload, 0)[0])
        elif kind in V2_RESERVED:
            yield GsCapRecord("S", value=kind)
        else:
            raise CaptureError(f"unknown version 2 record type {kind}")


def _read_capture_v1(stream) -> Iterator[GsCapRecord]:
    """Version 1 records: single-letter tags, no header."""
    while True:
        tag = stream.read(1)
        if not tag:
            return
        if tag == b"G":
            header = _read_exact(stream, 5)
            path, size = struct.unpack("<BI", header)
            data = _read_exact(stream, size)
            yield GsCapRecord("G", path=path, data=data)
        elif tag == b"P":
            address, value = struct.unpack("<IQ", _read_exact(stream, 12))
            yield GsCapRecord("P", address=address, value=value)
        elif tag == b"R":
            (qwords,) = struct.unpack("<I", _read_exact(stream, 4))
            yield GsCapRecord("R", qwords=qwords)
        elif tag == b"V":
            (frame,) = struct.unpack("<Q", _read_exact(stream, 8))
            yield GsCapRecord("V", frame=frame)
        else:
            raise CaptureError(f"unknown record tag {tag!r} at offset {stream.tell() - 1}")


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise CaptureError(f"truncated record: wanted {size} bytes, got {len(data)}")
    return data


@dataclass
class ConversionStats:
    frames: int = 0
    transfer_packets: int = 0
    transfer_bytes: int = 0
    fifo_reads: int = 0
    privileged_writes: int = 0
    privileged_writes_out_of_range: int = 0
    csr_writes: int = 0
    csr_reset_writes: int = 0
    records: dict = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {
            "frames": self.frames,
            "transfer_packets": self.transfer_packets,
            "transfer_bytes": self.transfer_bytes,
            "fifo_reads": self.fifo_reads,
            "privileged_writes": self.privileged_writes,
            "privileged_writes_out_of_range": self.privileged_writes_out_of_range,
            "csr_writes": self.csr_writes,
            "csr_reset_writes": self.csr_reset_writes,
            "records": dict(self.records),
        }


class DumpWriter:
    """Builds the .gs byte stream one packet at a time."""

    def __init__(self, out: BinaryIO, serial: str, crc: int, state: bytes, field_phase: int):
        if field_phase not in (0, 1):
            raise ValueError("field_phase must be 0 or 1")
        self.out = out
        self.field_phase = field_phase
        self.privileged = bytearray(PRIVILEGED_SIZE)
        self.stats = ConversionStats()
        serial_bytes = serial.encode("ascii")
        header = struct.pack(
            "<9I",
            GS_STATE_VERSION,  # state_version
            len(state),  # state_size
            GSDUMP_HEADER_SIZE,  # serial_offset
            len(serial_bytes),  # serial_size
            crc,  # crc
            0,  # screenshot_width
            0,  # screenshot_height
            GSDUMP_HEADER_SIZE + len(serial_bytes),  # screenshot_offset
            0,  # screenshot_size
        )
        header_block = header + serial_bytes
        out.write(struct.pack("<II", NEW_HEADER_MARKER, len(header_block)))
        out.write(header_block)
        out.write(state)
        out.write(bytes(self.privileged))  # initial privileged block: all zero

    def gif_packet(self, path: int, data: bytes) -> None:
        if not data:
            return
        self.out.write(struct.pack("<BBI", PACKET_TRANSFER, TRANSFER_PATH_NEW, len(data)))
        self.out.write(data)
        self.stats.transfer_packets += 1
        self.stats.transfer_bytes += len(data)

    def privileged_write(self, address: int, value: int) -> None:
        offset = address - PRIVILEGED_BASE
        if offset < 0 or offset + 8 > PRIVILEGED_SIZE:
            self.stats.privileged_writes_out_of_range += 1
            return
        self.stats.privileged_writes += 1
        if offset == CSR_OFFSET:
            self.stats.csr_writes += 1
            if value & 0x200:  # CSR RESET bit: a GS reset on real hardware; not replayed
                self.stats.csr_reset_writes += 1
        self.privileged[offset:offset + 8] = struct.pack("<Q", value)

    def fifo_read(self, qwords: int) -> None:
        if qwords == 0:
            return
        self.out.write(struct.pack("<BI", PACKET_READ_FIFO, qwords))
        self.stats.fifo_reads += 1

    def vsync(self, frame: int, recorded_field: Optional[int] = None) -> None:
        # The field comes from the frame parity (--field-phase), or from a version 2 VSYNC record when the
        # caller passes it (--field-from-stream).
        odd = (recorded_field & 1) if recorded_field is not None else (frame + self.field_phase) & 1
        csr = struct.unpack_from("<I", self.privileged, CSR_OFFSET)[0]
        csr = (csr | CSR_FIELD_BIT) if odd else (csr & ~CSR_FIELD_BIT & 0xFFFFFFFF)
        struct.pack_into("<I", self.privileged, CSR_OFFSET, csr)
        # VSync field byte as PCSX2 passes it: 0 when the field bit is set, else 1.
        field_byte = 0 if odd else 1
        self.out.write(bytes([PACKET_REGISTERS]))
        self.out.write(bytes(self.privileged))
        self.out.write(struct.pack("<BB", PACKET_VSYNC, field_byte))
        self.stats.frames += 1


def convert(
    source: Union[str, BinaryIO],
    destination: Union[str, BinaryIO],
    serial: str = DEFAULT_SERIAL,
    crc: int = 0,
    state: bytes = b"",
    field_phase: int = 0,
    max_frames: Optional[int] = None,
    field_from_stream: bool = False,
) -> ConversionStats:
    """Convert a capture. Paths may be strings or already open binary streams."""
    close_source = close_destination = False
    if isinstance(source, str):
        source = open(source, "rb")
        close_source = True
    if isinstance(destination, str):
        destination = open(destination, "wb")
        close_destination = True
    try:
        writer = DumpWriter(destination, serial, crc, state, field_phase)
        for record in read_capture(source):
            writer.stats.records[record.kind] = writer.stats.records.get(record.kind, 0) + 1
            if record.kind == "G":
                writer.gif_packet(record.path, record.data)
            elif record.kind == "P":
                writer.privileged_write(record.address, record.value)
            elif record.kind == "R":
                writer.fifo_read(record.qwords)
            elif record.kind == "V":
                writer.vsync(record.frame, record.field if field_from_stream else None)
                if max_frames is not None and writer.stats.frames >= max_frames:
                    break
        return writer.stats
    finally:
        if close_source:
            source.close()
        if close_destination:
            destination.close()


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("capture")
    parser.add_argument("output")
    parser.add_argument("--serial", default=DEFAULT_SERIAL)
    parser.add_argument("--crc", type=lambda text: int(text, 0), default=0)
    parser.add_argument("--state", help="file with a GS freeze blob to use as the initial state")
    parser.add_argument("--field-phase", type=int, choices=(0, 1), default=0)
    parser.add_argument("--max-frames", type=int)
    parser.add_argument("--field-from-stream", action="store_true",
                        help="version 2 captures: take the CSR FIELD bit from each VSYNC record")
    parser.add_argument("--stats-json")
    args = parser.parse_args(argv)
    state = b""
    if args.state:
        with open(args.state, "rb") as handle:
            state = handle.read()
    stats = convert(
        args.capture,
        args.output,
        serial=args.serial,
        crc=args.crc,
        state=state,
        field_phase=args.field_phase,
        max_frames=args.max_frames,
        field_from_stream=args.field_from_stream,
    )
    summary = stats.to_dict()
    text = json.dumps(summary, indent=2, sort_keys=True)
    if args.stats_json:
        with open(args.stats_json, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
