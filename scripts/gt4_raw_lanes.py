"""Extract ordinary UNPACK input integers; no geometry or VU interpretation.

Width, component count, post-fill source count and signedness are explicit
caller preconditions. Preserve 32-bit patterns and keep alignment bytes opaque.
"""

from __future__ import annotations

import struct

MAX_SOURCE_VECTORS = 256
MAX_LANES = 1024
MAX_DATA_BYTES = 4096


def decode_integer_lanes(payload: bytes, *, component_bits: int, components: int,
                         source_vectors: int, signed: bool,
                         alignment: bytes) -> tuple[tuple[int, ...], ...]:
    if not isinstance(payload, bytes) or not isinstance(alignment, bytes):
        raise ValueError("payload and alignment must be immutable bytes")
    if type(component_bits) is not int or component_bits not in (8, 16, 32):
        raise ValueError("only ordinary 8/16/32-bit lanes supported; packed forms unqualified")
    if type(components) is not int or not 1 <= components <= 4:
        raise ValueError("component count must be an integer in 1..4")
    if type(source_vectors) is not int or not 0 <= source_vectors <= MAX_SOURCE_VECTORS:
        raise ValueError("explicit source-vector count must be an integer in 0..256")
    if type(signed) is not bool:
        raise ValueError("explicit signedness must be a bool")
    lane_count = source_vectors * components
    data_bytes = lane_count * (component_bits // 8)
    if lane_count > MAX_LANES or data_bytes > MAX_DATA_BYTES:
        raise ValueError("lane extraction work cap exceeded")
    if len(payload) != data_bytes or len(alignment) != (-data_bytes) % 4:
        raise ValueError("exact payload and separate word-alignment extent required")
    # A 32-bit lane is a bit pattern regardless of the signedness assumption.
    # Float conversion, channel naming and VIF mode/mask effects are separate.
    lane_format = {8: "b" if signed else "B", 16: "h" if signed else "H", 32: "I"}[component_bits]
    vector_format = "<" + lane_format * components
    return tuple(struct.iter_unpack(vector_format, payload))
