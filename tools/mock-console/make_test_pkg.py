#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gera um .pkg PS4 sintético (cabeçalho + PARAM.SFO + ICON0.PNG).

Não contém nenhum conteúdo protegido: serve apenas para exercitar o
PkgInspector, o servidor HTTP local e a fila. A estrutura segue os offsets
confirmados em flatz/ps4_remote_pkg_installer/pkg.h e sfo.c.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import zlib

PKG_HEADER_SIZE = 0x2000
ENTRY_TABLE_OFFSET = 0x1000
ENTRY_SIZE = 0x20
ENTRY_ID_PARAM_SFO = 0x1000
ENTRY_ID_ICON0_PNG = 0x1200

CONTENT_TYPE_GD = 0x1A
CONTENT_TYPE_AC = 0x1B
FLAG_FIRST_PATCH = 0x00100000


def build_sfo(entries: dict[str, str]) -> bytes:
    keys = sorted(entries)
    header_size = 0x14
    index_size = len(keys) * 0x10

    key_table = bytearray()
    value_table = bytearray()
    index = bytearray()
    key_offsets, value_offsets, value_sizes = [], [], []

    for key in keys:
        key_offsets.append(len(key_table))
        key_table += key.encode("utf-8") + b"\0"
        value = entries[key].encode("utf-8") + b"\0"
        value_offsets.append(len(value_table))
        value_table += value
        value_sizes.append(len(value))
    while len(key_table) % 4:
        key_table += b"\0"

    key_table_offset = header_size + index_size
    value_table_offset = key_table_offset + len(key_table)

    for i, _ in enumerate(keys):
        index += struct.pack("<HHIII", key_offsets[i], 0x0204, value_sizes[i],
                             value_sizes[i], value_offsets[i])

    header = struct.pack("<4sIIII", b"\x00PSF", 0x00000101, key_table_offset,
                         value_table_offset, len(keys))
    return bytes(header + index + key_table + value_table)


def build_icon_png(size: int = 64, seed: int = 0) -> bytes:
    """Gera um ICON0.PNG válido (gradiente simples), sem dependências."""
    def chunk(kind: bytes, payload: bytes) -> bytes:
        data = kind + payload
        return (struct.pack(">I", len(payload)) + data
                + struct.pack(">I", binascii.crc32(data) & 0xFFFFFFFF))

    rows = bytearray()
    for y in range(size):
        rows.append(0)  # filtro "None"
        for x in range(size):
            rows += bytes(((x * 255) // size,
                           (y * 255) // size,
                           (seed + 90) % 256))

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def build_pkg(content_id: str, title: str, category: str, app_version: str,
              padding: int) -> bytes:
    title_id = content_id.split("-")[1].split("_")[0]
    sfo = build_sfo({
        "APP_VER": app_version,
        "CATEGORY": category,
        "CONTENT_ID": content_id,
        "TITLE": title,
        "TITLE_ID": title_id,
        "VERSION": app_version,
    })
    icon = build_icon_png(64, sum(content_id.encode("ascii")) % 200)

    if category == "ac":
        content_type, content_flags = CONTENT_TYPE_AC, 0
    elif category in ("gp", "gpd"):
        content_type, content_flags = CONTENT_TYPE_GD, FLAG_FIRST_PATCH
    else:
        content_type, content_flags = CONTENT_TYPE_GD, 0

    header = bytearray(PKG_HEADER_SIZE)
    header[0:4] = b"\x7fCNT"
    struct.pack_into(">I", header, 0x10, 2)                     # entry_count
    struct.pack_into(">I", header, 0x18, ENTRY_TABLE_OFFSET)    # entry_table_offset
    header[0x40:0x40 + len(content_id)] = content_id.encode("ascii")
    struct.pack_into(">I", header, 0x74, content_type)
    struct.pack_into(">I", header, 0x78, content_flags)

    payload = bytearray()
    offset = PKG_HEADER_SIZE
    for index, (entry_id, blob) in enumerate(
            ((ENTRY_ID_PARAM_SFO, sfo), (ENTRY_ID_ICON0_PNG, icon))):
        base = ENTRY_TABLE_OFFSET + index * ENTRY_SIZE
        struct.pack_into(">I", header, base + 0x00, entry_id)
        struct.pack_into(">I", header, base + 0x10, offset)
        struct.pack_into(">I", header, base + 0x14, len(blob))
        payload += blob
        offset += len(blob)

    body = bytes(header) + bytes(payload)
    if padding > 0:
        body += bytes((i * 7 + 13) & 0xFF for i in range(padding))
    body = bytearray(body)
    struct.pack_into(">Q", body, 0x430, len(body))
    return bytes(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--content-id", default="UP0001-CUSA12345_00-ORBISLINKTEST001")
    parser.add_argument("--title", default="Jogo de Teste")
    parser.add_argument("--category", default="gd", choices=["gd", "gp", "ac"])
    parser.add_argument("--app-version", default="01.00")
    parser.add_argument("--padding", type=int, default=1024 * 1024,
                        help="bytes de enchimento depois dos metadados")
    arguments = parser.parse_args()

    data = build_pkg(arguments.content_id, arguments.title, arguments.category,
                     arguments.app_version, arguments.padding)
    with open(arguments.output, "wb") as handle:
        handle.write(data)
    print(f"{arguments.output}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
