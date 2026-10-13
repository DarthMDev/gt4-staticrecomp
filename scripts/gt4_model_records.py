"""Bound fixed serialized records; do not decode VIF commands or texture pixels.

Original implementation from pinned format facts. Verified input is a caller
precondition. Shape +4, material semantics and transfer storage sizes are opaque.
"""

from __future__ import annotations

import hashlib
import struct

import gt4_model_probe as headers

MAX_DESCRIPTORS_PER_SHAPE = 4096
MAX_TOTAL_DESCRIPTORS = 65536
MAX_TOTAL_TEXTURE_RECORDS = 16384
MATERIAL_STRIDE = 0x50
TEXTURE_STRIDE = 0x28
TRANSFER_STRIDE = 0x0C


def flags_metadata(flags: int) -> dict:
    texture = flags & 0x1FF
    material = flags >> 9
    return {"flags_raw": flags,
            "texture_kind": "none" if texture == 0 else "external" if texture == 511 else "local",
            "texture_index": texture - 1 if 1 <= texture <= 510 else None,
            "material_index": material - 1 if material else None}


def record_hash(data: bytes, start: int, size: int) -> str:
    return hashlib.sha256(data[start:start + size]).hexdigest()


def inventory_asset(data: bytes) -> dict:
    header = headers.probe_asset(data)
    model = header["main_model"]
    base, size = model["file_offset"], model["declared_size"]
    shapes = []
    total_descriptors = 0
    for index, relative in enumerate(model["shape_offsets"]):
        headers.span(relative, 0x10, size, 0x80)
        shape_base = base + relative
        flags, unknown_byte, count, vertices, triangles = struct.unpack_from("<BBHHH", data, shape_base + 8)
        headers.require(count <= MAX_DESCRIPTORS_PER_SHAPE, "shape descriptor count cap exceeded")
        total_descriptors += count
        headers.require(total_descriptors <= MAX_TOTAL_DESCRIPTORS, "aggregate descriptor cap exceeded")
        descriptor_end = 0x10 + count * 8
        headers.span(relative, descriptor_end, size, 0x80)
        descriptors = []
        for descriptor_index in range(count):
            packet, quadwords, packed = struct.unpack_from("<IHH", data, shape_base + 0x10 + descriptor_index * 8)
            packet_size = quadwords * 16
            if packet_size:
                headers.require(packet >= descriptor_end, "packet overlaps its shape header/descriptors")
                headers.span(relative + packet, packet_size, size, 0x80)
            # Zero-QWC descriptors access no packet bytes. Preserve their raw
            # offset without asserting any pointer or runtime semantics.
            descriptors.append({"index": descriptor_index, "shape_relative_offset": packet,
                                "quadwords": quadwords, "packet_bytes": packet_size,
                                "model_relative_offset": relative + packet if packet_size else None,
                                "absolute_alignment_16": (shape_base + packet) % 16 == 0 if packet_size else None,
                                "packet_body_decoded": False, **flags_metadata(packed)})
        shapes.append({"index": index, "model_relative_offset": relative,
                       "relocator_raw": headers.u32(data, shape_base),
                       "word_04_raw": headers.u32(data, shape_base + 4),
                       "flags_raw": flags, "unknown_byte_raw": unknown_byte,
                       "strip_vertices_raw": vertices, "triangles_raw": triangles,
                       "descriptor_count": count, "descriptors": descriptors})
    material_count = model["material_count"]
    material_offset = model["material_table_offset"]
    headers.table(data, material_offset, material_count, MATERIAL_STRIDE, size, 0x80)
    materials = [{"index": index, "record_sha256": record_hash(data, base + material_offset + index * MATERIAL_STRIDE, MATERIAL_STRIDE)}
                 for index in range(material_count)]
    texture_sets = []
    total_texture_records = 0
    for texture in model["textures"]:
        entry = {"color": texture["color"], "lod": texture["lod"],
                 "model_relative_offset": texture["model_relative_offset"],
                 "declared_size": texture["declared_size"], "textures": [], "transfers": []}
        if not texture["declared_size"]:
            entry["classification"] = "Header only; zero-size semantics Unknown"
            texture_sets.append(entry)
            continue
        texture_count, transfer_count = texture["texture_count"], texture["transfer_count"]
        total_texture_records += texture_count + transfer_count
        headers.require(total_texture_records <= MAX_TOTAL_TEXTURE_RECORDS, "aggregate texture record cap exceeded")
        texture_size, texture_base = texture["declared_size"], texture["file_offset"]
        texture_offset, transfer_offset = texture["texture_map_offset"], texture["transfer_info_offset"]
        headers.table(data, texture_offset, texture_count, TEXTURE_STRIDE, texture_size, 0x30)
        headers.table(data, transfer_offset, transfer_count, TRANSFER_STRIDE, texture_size, 0x30)
        entry["textures"] = [{"index": index,
                              "record_sha256": record_hash(data, texture_base + texture_offset + index * TEXTURE_STRIDE, TEXTURE_STRIDE)}
                             for index in range(texture_count)]
        for index in range(transfer_count):
            offset, block, width_buffer, pixel_format, width, height = struct.unpack_from("<IHBBHH", data, texture_base + transfer_offset + index * TRANSFER_STRIDE)
            # A transfer is not necessarily a complete linear texture. No bpp
            # formula is used to read or validate its unqualified storage span.
            headers.span(offset, 1, texture_size, 0x30)
            entry["transfers"].append({"index": index, "data_offset_raw": offset,
                                       "block_raw": block, "buffer_width_raw": width_buffer,
                                       "pixel_format_raw": pixel_format,
                                       "width_raw": width, "height_raw": height,
                                       "data_extent": "Unknown; start only checked", "pixels_decoded": False})
        entry["classification"] = "Fixed tables bounded; pixel/CLUT bodies Unknown"
        texture_sets.append(entry)
    return {"classification": "Fixed record inventory only; not renderable geometry or decoded images",
            "main_model_offset": base, "main_model_bytes": size,
            "shapes": shapes, "material_record_count": material_count,
            "materials": materials, "texture_sets": texture_sets,
            "total_descriptors": total_descriptors,
            "total_texture_records": total_texture_records,
            "geometry_vif_material_semantics_pixels_and_live_use": "Unknown"}
