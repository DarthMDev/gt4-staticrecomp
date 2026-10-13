"""Bounded serialized CAR4/MDLS/Tex1 header probe; no geometry or relocation.

Original implementation from pinned format facts in docs/NENKAI-REFERENCES.md.
The caller must separately verify/extract its input. This is not a renderer or
a complete asset validator. Fixed arrays and selected header starts only;
material, geometry and texture subrecord extents remain unknown.
"""

from __future__ import annotations

import struct

MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_MODELS = 4096
MAX_SHAPES = 8192
MAX_MATERIALS = 4096
MAX_COLORS = 256
MAX_TEXTURE_LODS = 32
MAX_TEXTURE_REFERENCES = 2048


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def span(offset: int, size: int, limit: int, minimum: int = 0) -> None:
    require(offset >= minimum and size >= 0 and offset + size <= limit,
            "model-relative extent exceeds its container")


def u32(data, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def table(data, offset: int, count: int, stride: int, limit: int, minimum: int) -> None:
    if count:
        span(offset, count * stride, limit, minimum)


def probe_tex1(data, base: int, model_end: int) -> dict:
    span(base, 0x30, model_end)
    require(data[base:base + 4] == b"Tex1", "expected Tex1 header")
    size = u32(data, base + 0x0C)
    texture_count, transfer_count = struct.unpack_from("<HH", data, base + 0x14)
    require(texture_count <= 4096 and transfer_count <= 4096, "Tex1 count cap exceeded")
    texture_map, transfers, clut_patches = struct.unpack_from("<III", data, base + 0x18)
    animation_raw, unknown_raw = struct.unpack_from("<II", data, base + 0x24)
    if size == 0:
        # Observed in selected GT4 LOD slots. Only the containing MDLS bounds
        # this header; no zero-size body or runtime meaning has been qualified.
        require(not any((texture_count, transfer_count, texture_map, transfers,
                         clut_patches, animation_raw, unknown_raw)),
                "zero-size Tex1 with subrecord references is unsupported")
        return {"file_offset": base, "declared_size": 0,
                "texture_count": 0, "transfer_count": 0,
                "texture_map_offset": 0, "transfer_info_offset": 0,
                "clut_patch_offset": 0, "pixel_and_record_decoding": "Unknown",
                "extent_classification": "Header only; zero-size slot semantics Unknown"}
    require(size >= 0x30, "Tex1 declared size is smaller than header")
    span(base, size, model_end)
    # Detailed PGLU/transfer/CLUT record layouts are outside this probe.
    for offset, count in ((texture_map, texture_count), (transfers, transfer_count)):
        if count:
            span(offset, 1, size, 0x30)
    if clut_patches:
        span(clut_patches, 1, size, 0x30)
    return {"file_offset": base, "declared_size": size,
            "texture_count": texture_count, "transfer_count": transfer_count,
            "texture_map_offset": texture_map, "transfer_info_offset": transfers,
            "clut_patch_offset": clut_patches,
            "animation_offset_raw": animation_raw, "unknown_offset_raw": unknown_raw,
            "pixel_and_record_decoding": "Unknown",
            "extent_classification": "Declared container; subrecord extents Unknown"}


def probe_mdls(data, base: int) -> dict:
    span(base, 0x80, len(data))
    require(data[base:base + 4] == b"MDLS", "expected MDLS header")
    size = u32(data, base + 0x10)
    require(size >= 0x80, "MDLS declared size is smaller than header")
    span(base, size, len(data))
    models, shapes, materials, texture_lods, colors = struct.unpack_from("<5H", data, base + 0x16)
    require(models <= MAX_MODELS and shapes <= MAX_SHAPES and materials <= MAX_MATERIALS,
            "MDLS model/shape/material cap exceeded")
    require(colors <= MAX_COLORS and texture_lods <= MAX_TEXTURE_LODS
            and colors * texture_lods <= MAX_TEXTURE_REFERENCES, "MDLS texture count cap exceeded")
    model_table, shape_table, material_table, color_table = struct.unpack_from("<4I", data, base + 0x38)
    table(data, model_table, models, 0x28, size, 0x80)
    table(data, shape_table, shapes, 4, size, 0x80)
    if materials:
        span(material_table, 1, size, 0x80)
    model_records = []
    for index in range(models):
        record = base + model_table + index * 0x28
        bounds_count = data[record + 1]
        bounds_offset = u32(data, record + 4)
        table(data, bounds_offset, bounds_count, 12, size, 0x80)
        commands = u32(data, record + 0x18)
        if commands:
            span(commands, 1, size, 0x80)
        # Signed VM words include -1 in serialized models. Their bytecode base
        # and extent are not qualified; never dereference them as MDLS pointers.
        constructor, update, callback = struct.unpack_from("<3i", data, record + 0x1C)
        model_records.append({"index": index, "bounds_count": bounds_count,
                              "bounds_offset": bounds_offset, "commands_offset": commands,
                              "constructor_vm_raw": constructor, "update_vm_raw": update,
                              "callback_vm_raw": callback, "vm_word_semantics": "Unknown"})
    shape_offsets = []
    for index in range(shapes):
        offset = u32(data, base + shape_table + index * 4)
        span(offset, 1, size, 0x80)
        shape_offsets.append(offset)
    textures = []
    if colors and texture_lods:
        table(data, color_table, colors, 4, size, 0x80)
        for color in range(colors):
            bucket = u32(data, base + color_table + color * 4)
            table(data, bucket, texture_lods, 4, size, 0x80)
            for lod in range(texture_lods):
                texture = u32(data, base + bucket + lod * 4)
                span(texture, 0x30, size, 0x80)
                header = probe_tex1(data, base + texture, base + size)
                textures.append({"color": color, "lod": lod, "model_relative_offset": texture,
                                 **header})
    return {"file_offset": base, "declared_size": size, "model_count": models,
            "shape_count": shapes, "material_count": materials,
            "texture_lods_per_color": texture_lods, "color_count": colors,
            "model_table_offset": model_table, "shape_table_offset": shape_table,
            "material_table_offset": material_table, "color_table_offset": color_table,
            "models": model_records, "shape_offsets": shape_offsets, "textures": textures,
            "relocation_base_raw": u32(data, base + 0x0C), "relocation_applied": False,
            "geometry_material_vm_and_pixels": "Unknown"}


def probe_asset(data: bytes) -> dict:
    require(4 <= len(data) <= MAX_FILE_BYTES, "asset input cap or short signature")
    if data[:4] == b"CAR4":
        span(0, 0x3C, len(data))
        model_base = u32(data, 0x18)
        span(model_base, 0x80, len(data), 0x3C)
        container = {"kind": "CAR4", "main_model_offset": model_base,
                     "relocate_pointer_raw": u32(data, 4),
                     "relocation_size_raw": u32(data, 8),
                     "other_car_subcontainers": "Unknown"}
    elif data[:4] == b"MDLS":
        model_base = 0
        container = {"kind": "MDLS"}
    else:
        raise ValueError("unsupported asset signature; expected CAR4 or MDLS")
    return {"classification": "Serialized header/table probe only; not full model validation",
            "input_bytes": len(data), "container": container,
            "main_model": probe_mdls(data, model_base)}
