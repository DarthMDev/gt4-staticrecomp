"""Conditional VIF byte framing, without executing commands or decoding vectors.

The caller supplies an explicit initial CL/WL assumption. Matching boundaries
does not prove live cycle state, VU results, GT4 channels or GIF semantics.
"""

from __future__ import annotations

import struct

MAX_PACKET_BYTES = 1024 * 1024
MAX_COMMANDS = 65536
NO_PAYLOAD = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE",
              0x04: "ITOP", 0x05: "STMOD", 0x06: "MSKPATH3", 0x07: "MARK",
              0x10: "FLUSHE", 0x11: "FLUSH", 0x13: "FLUSHA", 0x14: "MSCAL",
              0x15: "MSCALF", 0x17: "MSCNT"}
FIXED_WORDS = {0x20: ("STMASK", 1), 0x30: ("STROW", 4), 0x31: ("STCOL", 4)}


def check_cycle(cycle) -> tuple[int, int]:
    if (not isinstance(cycle, tuple) or len(cycle) != 2
            or any(type(value) is not int or not 1 <= value <= 255 for value in cycle)):
        raise ValueError("explicit CL/WL in 1..255 required; zero-field semantics unqualified")
    return cycle


def frame_packet(data: bytes, *, initial_cycle: tuple[int, int]) -> dict:
    cycle = check_cycle(initial_cycle)
    if not 4 <= len(data) <= MAX_PACKET_BYTES or len(data) % 4:
        raise ValueError("packet byte cap or four-byte extent violated")
    cursor = 0
    commands = []
    while cursor < len(data):
        if len(commands) >= MAX_COMMANDS:
            raise ValueError("command count cap exceeded")
        immediate, number, raw_opcode = struct.unpack_from("<HBB", data, cursor)
        opcode = raw_opcode & 0x7F
        payload = 0
        extra = {}
        if opcode in NO_PAYLOAD:
            name = NO_PAYLOAD[opcode]
            if opcode == 0x01:
                cycle = check_cycle((immediate & 255, immediate >> 8))
        elif opcode in FIXED_WORDS:
            name, words = FIXED_WORDS[opcode]
            payload = words * 4
        elif opcode == 0x4A:
            name = "MPG"
            payload = (number or 256) * 8
        elif opcode in (0x50, 0x51):
            name = "DIRECT" if opcode == 0x50 else "DIRECTHL"
            payload = (immediate or 65536) * 16
        elif 0x60 <= opcode <= 0x7F:
            name = "UNPACK"
            width_code = opcode & 3
            if width_code == 3:
                raise ValueError(f"VL=3 UNPACK outside this qualified subset at byte {cursor}")
            components = ((opcode >> 2) & 3) + 1
            vector_count = number or 256
            cl, wl = cycle
            # Fill rows have no source vector. Counting selected rows directly
            # avoids interpreting destination addresses or executing VU writes.
            source_count = sum(1 for row in range(vector_count) if wl <= cl or row % wl < cl)
            payload = source_count * components * (4, 2, 1)[width_code]
            extra = {"vectors_raw_expanded": vector_count, "source_vectors": source_count,
                     "components": components, "component_bits": (32, 16, 8)[width_code],
                     "mask_enabled": bool(opcode & 0x10), "cycle_cl_wl": [cl, wl]}
        else:
            raise ValueError(f"unsupported VIF opcode 0x{opcode:02X} at byte {cursor}")
        aligned_payload = (payload + 3) & ~3
        end = cursor + 4 + aligned_payload
        if end > len(data):
            raise ValueError(f"{name} payload exceeds packet at byte {cursor}")
        commands.append({"byte_offset": cursor, "opcode": opcode, "name": name,
                         "immediate_raw": immediate, "number_raw": number,
                         "irq": bool(raw_opcode & 0x80), "payload_bytes": payload,
                         "aligned_payload_bytes": aligned_payload, "next_byte_offset": end,
                         **extra})
        cursor = end
    return {"classification": "Conditional standard VIF framing only; no execution or channel decoding",
            "initial_cycle_cl_wl": list(initial_cycle), "final_cycle_cl_wl": list(cycle),
            "packet_bytes": len(data), "command_count": len(commands), "commands": commands,
            "live_cycle_geometry_gif_and_vu_results": "Unknown"}
