#!/usr/bin/env python3
"""Generate Strappy's private webview Font Awesome codepoint map.

This only generates renderer plumbing. The assistant-facing
Resources/FontAwesomeIcons.json is a checked-in metadata resource.
"""

import argparse
import json
import os
import struct
import sys


STYLES = (
    ("solid", "FA7-Solid-900.otf"),
    ("regular", "FA7-Regular-400.otf"),
    ("brands", "FA7-Brands-400.otf"),
)


def u16(data, offset):
    return struct.unpack(">H", data[offset:offset + 2])[0]


def u32(data, offset):
    return struct.unpack(">I", data[offset:offset + 4])[0]


def tables(data):
    count = u16(data, 4)
    output = {}
    offset = 12
    for _ in range(count):
        output[data[offset:offset + 4]] = u32(data, offset + 8)
        offset += 16
    return output


def load_cmap(data, offset):
    count = u16(data, offset + 2)
    subtable = None
    for index in range(count):
        record = offset + 4 + index * 8
        platform_id = u16(data, record)
        encoding_id = u16(data, record + 2)
        candidate = offset + u32(data, record + 4)
        if (platform_id == 3 and encoding_id in (1, 10)) or platform_id == 0:
            subtable = candidate
    if subtable is None:
        raise ValueError("cmap has no supported subtable")

    fmt = u16(data, subtable)
    mapping = {}
    if fmt == 4:
        segx2 = u16(data, subtable + 6)
        seg_count = segx2 // 2
        end_offset = subtable + 14
        start_offset = end_offset + segx2 + 2
        delta_offset = start_offset + segx2
        range_offset = delta_offset + segx2
        for segment in range(seg_count):
            end = u16(data, end_offset + segment * 2)
            start = u16(data, start_offset + segment * 2)
            delta = u16(data, delta_offset + segment * 2)
            range_value = u16(data, range_offset + segment * 2)
            for codepoint in range(start, end + 1):
                if codepoint == 0xFFFF:
                    continue
                if range_value == 0:
                    glyph = (codepoint + delta) & 0xFFFF
                else:
                    glyph = u16(
                        data,
                        range_offset + segment * 2 + range_value +
                        (codepoint - start) * 2)
                    if glyph:
                        glyph = (glyph + delta) & 0xFFFF
                if glyph:
                    mapping[codepoint] = glyph
    elif fmt == 12:
        group_count = u32(data, subtable + 12)
        for index in range(group_count):
            group_offset = subtable + 16 + index * 12
            start_code = u32(data, group_offset)
            end_code = u32(data, group_offset + 4)
            start_glyph = u32(data, group_offset + 8)
            for codepoint in range(start_code, end_code + 1):
                mapping[codepoint] = start_glyph + (codepoint - start_code)
    else:
        raise ValueError("unsupported cmap format: %d" % fmt)
    return mapping


def cff_index(data, offset):
    count = u16(data, offset)
    if count == 0:
        return [], offset + 2
    offset_size = data[offset + 2]
    base = offset + 3
    offsets = []
    for index in range(count + 1):
        pointer = base + index * offset_size
        value = 0
        for byte in data[pointer:pointer + offset_size]:
            value = (value << 8) | byte
        offsets.append(value)
    data_base = base + (count + 1) * offset_size - 1
    items = [
        data[data_base + offsets[index]:data_base + offsets[index + 1]]
        for index in range(count)
    ]
    return items, data_base + offsets[-1]


def cff_dict(blob):
    operators = {}
    operands = []
    index = 0
    while index < len(blob):
        byte = blob[index]
        if byte <= 21:
            operator = byte
            index += 1
            if byte == 12:
                operator = 1200 + blob[index]
                index += 1
            operators[operator] = operands
            operands = []
        elif byte == 28:
            operands.append(struct.unpack(">h", blob[index + 1:index + 3])[0])
            index += 3
        elif byte == 29:
            operands.append(struct.unpack(">i", blob[index + 1:index + 5])[0])
            index += 5
        elif byte == 30:
            index += 1
            while index < len(blob):
                high = blob[index] >> 4
                low = blob[index] & 0xF
                index += 1
                if high == 0xF or low == 0xF:
                    break
            operands.append(0.0)
        elif 32 <= byte <= 246:
            operands.append(byte - 139)
            index += 1
        elif 247 <= byte <= 250:
            operands.append((byte - 247) * 256 + blob[index + 1] + 108)
            index += 2
        elif 251 <= byte <= 254:
            operands.append(-(byte - 251) * 256 - blob[index + 1] - 108)
            index += 2
        else:
            index += 1
    return operators


def glyph_names(data, cff_offset):
    pointer = cff_offset + data[cff_offset + 2]
    _, pointer = cff_index(data, pointer)
    top_dicts, pointer = cff_index(data, pointer)
    strings, pointer = cff_index(data, pointer)
    top = cff_dict(top_dicts[0])
    glyph_count = u16(data, cff_offset + top[17][0])
    charset_offset = cff_offset + top[15][0]
    glyph_to_sid = {0: 0}
    fmt = data[charset_offset]
    pointer = charset_offset + 1
    glyph = 1
    if fmt == 0:
        while glyph < glyph_count:
            glyph_to_sid[glyph] = u16(data, pointer)
            pointer += 2
            glyph += 1
    elif fmt in (1, 2):
        while glyph < glyph_count:
            first = u16(data, pointer)
            pointer += 2
            if fmt == 1:
                left = data[pointer]
                pointer += 1
            else:
                left = u16(data, pointer)
                pointer += 2
            for suffix in range(left + 1):
                if glyph >= glyph_count:
                    break
                glyph_to_sid[glyph] = first + suffix
                glyph += 1
    else:
        raise ValueError("unsupported CFF charset format: %d" % fmt)

    def sid_name(sid):
        if sid == 0:
            return ".notdef"
        if sid < 391:
            return "std%d" % sid
        index = sid - 391
        if index < len(strings):
            return strings[index].decode("latin1")
        return "sid%d" % sid

    return {glyph_id: sid_name(sid) for glyph_id, sid in glyph_to_sid.items()}


def load_font_icons(path):
    with open(path, "rb") as handle:
        data = handle.read()
    table_map = tables(data)
    cmap = load_cmap(data, table_map[b"cmap"])
    names = glyph_names(data, table_map[b"CFF "])

    icons = {}
    for codepoint, glyph in cmap.items():
        name = names.get(glyph)
        if not name or name == ".notdef":
            continue
        if not (0xE000 <= codepoint <= 0xF8FF):
            continue
        if name not in icons or codepoint < icons[name]:
            icons[name] = codepoint
    return icons


def js_quote(value):
    return json.dumps(value, separators=(",", ":"))


def write_webview_map(path, by_style):
    lines = [
        "    \"var faIconMap={\",",
    ]
    for style_index, (style, _) in enumerate(STYLES):
        mapping = by_style.get(style, {})
        suffix = "," if style_index < (len(STYLES) - 1) else ""
        lines.append("    \"'%s':{\"" % style)
        entries = [
            "%s:'%04X'" % (js_quote(name), mapping[name])
            for name in sorted(mapping)
        ]
        for start in range(0, len(entries), 10):
            chunk = ",".join(entries[start:start + 10])
            if start + 10 < len(entries):
                chunk += ","
            lines.append("    %s," % js_quote(chunk))
        lines.append("    \"}%s\"," % suffix)

    lines.append("    \"};\",")

    with open(path, "w", encoding="utf-8") as handle:
        handle.write("/* Generated by tools/generate_fontawesome_webview_map.py. */\n")
        handle.write("\n".join(lines))
        handle.write("\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font-dir", required=True)
    parser.add_argument("--webview-map", required=True)
    args = parser.parse_args()

    by_style = {}
    for style, filename in STYLES:
        path = os.path.join(args.font_dir, filename)
        if not os.path.exists(path):
            sys.exit("font not found: %s" % path)
        by_style[style] = load_font_icons(path)

    write_webview_map(args.webview_map, by_style)
    print("wrote %d Font Awesome webview icons" %
          sum(len(mapping) for mapping in by_style.values()))


if __name__ == "__main__":
    main()
