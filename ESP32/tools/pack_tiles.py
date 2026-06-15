#!/usr/bin/env python3
"""
将下载的瓦片打包为 C 源码, 编译进固件。
用法:
    python pack_tiles.py --input ./tiles --output ../components/RC/tile_data --z-max 16
"""
import os, sys, struct, argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', default='./tiles', help='瓦片目录')
    parser.add_argument('--output', default='../components/RC/tile_data', help='输出路径(不含扩展名)')
    parser.add_argument('--z-max', type=int, default=16, help='最大缩放级别(含)')
    args = parser.parse_args()

    tiles = []  # [(z, x, y, path)]
    tiles_dir = Path(args.input)
    for z_dir in sorted(tiles_dir.iterdir()):
        if not z_dir.is_dir(): continue
        try:
            z = int(z_dir.name)
        except ValueError:
            continue
        if z > args.z_max:
            print(f"  跳过 Z{z} (超过 --z-max={args.z_max})")
            continue
        for x_dir in sorted(z_dir.iterdir()):
            if not x_dir.is_dir(): continue
            try: x = int(x_dir.name)
            except ValueError: continue
            for f in sorted(x_dir.iterdir()):
                if f.suffix.lower() != '.png': continue
                try: y = int(f.stem)
                except ValueError: continue
                tiles.append((z, x, y, str(f)))

    tiles.sort(key=lambda t: (t[0], t[1], t[2]))
    print(f"瓦片总数: {len(tiles)}")

    # 收集数据并构建偏移表
    pool = bytearray()
    entries = []
    for z, x, y, path in tiles:
        with open(path, 'rb') as f:
            data = f.read()
        entry = {
            'z': z, 'x': x, 'y': y,
            'offset': len(pool),
            'size': len(data),
        }
        pool.extend(data)
        entries.append(entry)

    print(f"数据池大小: {len(pool)} bytes ({len(pool)/1024/1024:.1f}MB)")

    # 写入 .h 文件
    h_path = Path(args.output + '.h')
    with open(h_path, 'w', encoding='utf-8') as f:
        f.write('#pragma once\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'#define TILE_COUNT {len(entries)}\n\n')
        f.write('typedef struct {\n')
        f.write('    uint8_t z;\n')
        f.write('    uint8_t _pad;\n')
        f.write('    uint16_t _pad2;\n')
        f.write('    uint32_t x;\n')
        f.write('    uint32_t y;\n')
        f.write('    uint32_t offset;\n')
        f.write('    uint32_t size;\n')
        f.write('} tile_entry_t;\n\n')
        f.write('extern const tile_entry_t tile_entries[TILE_COUNT];\n')
        f.write('extern const uint8_t tile_pool[];\n')
        f.write('extern const unsigned int tile_pool_size;\n')
        f.write('const uint8_t *tile_find(uint8_t z, uint32_t x, uint32_t y, uint32_t *out_size);\n')

    # 写入 .c 文件
    c_path = Path(args.output + '.c')
    with open(c_path, 'w', encoding='utf-8') as f:
        f.write('#include "tile_data.h"\n')
        f.write('#include <stddef.h>\n\n')
        f.write(f'const unsigned int tile_pool_size = {len(pool)};\n\n')

        # 查找表
        f.write('const tile_entry_t tile_entries[TILE_COUNT] = {\n')
        for e in entries:
            f.write(f'    {{.z={e["z"]}, .x={e["x"]}U, .y={e["y"]}U, .offset={e["offset"]}U, .size={e["size"]}U}},\n')
        f.write('};\n\n')

        # 数据池 (每行16字节)
        f.write('const uint8_t tile_pool[] = {\n')
        for i in range(0, len(pool), 16):
            chunk = pool[i:i+16]
            hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f'    {hex_str},\n')
        f.write('};\n\n')

        # 二分查找函数
        f.write('const uint8_t *tile_find(uint8_t z, uint32_t x, uint32_t y, uint32_t *out_size) {\n')
        f.write('    int lo = 0, hi = TILE_COUNT - 1;\n')
        f.write('    while (lo <= hi) {\n')
        f.write('        int mid = (lo + hi) / 2;\n')
        f.write('        const tile_entry_t *e = &tile_entries[mid];\n')
        f.write('        if (z < e->z) hi = mid - 1;\n')
        f.write('        else if (z > e->z) lo = mid + 1;\n')
        f.write('        else if (x < e->x) hi = mid - 1;\n')
        f.write('        else if (x > e->x) lo = mid + 1;\n')
        f.write('        else if (y < e->y) hi = mid - 1;\n')
        f.write('        else if (y > e->y) lo = mid + 1;\n')
        f.write('        else { *out_size = e->size; return &tile_pool[e->offset]; }\n')
        f.write('    }\n')
        f.write('    return NULL;\n')
        f.write('}\n')

    print(f"已生成: {h_path}")
    print(f"已生成: {c_path}")
    print(f"请在 CMakeLists.txt 中添加 tile_data.c")

if __name__ == '__main__':
    main()
