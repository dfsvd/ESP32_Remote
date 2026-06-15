#!/usr/bin/env python3
"""
地图瓦片下载工具 - 用于评估存储需求

下载指定区域的瓦片，统计总大小和数量。
支持高德地图、OpenStreetMap 等源。

用法:
    # 评估深圳市范围 (Z12-Z17)
    python download_tiles.py --lat 22.54 --lon 114.06 --radius 20 --z-min 12 --z-max 17

    # 评估一个飞场 (5km半径)
    python download_tiles.py --lat 22.537 --lon 113.979 --radius 3 --z-min 15 --z-max 17

    # 只统计不下载
    python download_tiles.py --lat 22.54 --lon 114.06 --radius 10 --z-min 12 --z-max 17 --dry-run
"""

import os
import math
import argparse
import urllib.request
import urllib.error
import time

# 瓦片源定义
TILE_SOURCES = {
    'gaode': {
        'url': 'https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}',
        'subdomains': ['1', '2', '3', '4'],
        'label': '高德地图',
    },
    'gaode_sat': {
        'url': 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
        'subdomains': ['1', '2', '3', '4'],
        'label': '高德卫星',
    },
    'osm': {
        'url': 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',
        'subdomains': ['a', 'b', 'c'],
        'label': 'OpenStreetMap',
    },
}


def lon_to_tilex(lon, z):
    """经度转瓦片X"""
    return int(math.floor((lon + 180.0) / 360.0 * (1 << z)))


def lat_to_tiley(lat, z):
    """纬度转瓦片Y"""
    lat_rad = math.radians(lat)
    return int(math.floor((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * (1 << z)))


def tilex_to_lon(x, z):
    """瓦片X转经度 (左上角)"""
    return x / (1 << z) * 360.0 - 180.0


def tiley_to_lat(y, z):
    """瓦片Y转纬度 (左上角)"""
    n = math.pi - 2.0 * math.pi * y / (1 << z)
    return math.degrees(math.atan(math.sinh(n)))


def tile_bounds_for_area(center_lat, center_lon, radius_km, z):
    """
    计算覆盖指定圆形区域所需的瓦片范围
    radius_km: 半径(公里)
    """
    # 1度经度 ≈ 111km * cos(lat)
    # 1度纬度 ≈ 111km
    deg_per_km_lat = 1.0 / 111.0
    deg_per_km_lon = 1.0 / (111.0 * math.cos(math.radians(center_lat)))

    lat_delta = radius_km * deg_per_km_lat
    lon_delta = radius_km * deg_per_km_lon

    min_lat = center_lat - lat_delta
    max_lat = center_lat + lat_delta
    min_lon = center_lon - lon_delta
    max_lon = center_lon + lon_delta

    x_min = lon_to_tilex(min_lon, z)
    x_max = lon_to_tilex(max_lon, z)
    y_min = lat_to_tiley(max_lat, z)  # Y 轴反转: 北→小, 南→大
    y_max = lat_to_tiley(min_lat, z)

    # 确保 x_min < x_max, y_min < y_max
    if x_min > x_max:
        x_min, x_max = x_max, x_min
    if y_min > y_max:
        y_min, y_max = y_max, y_min

    return x_min, x_max, y_min, y_max


def format_size(bytes_):
    """人类可读的文件大小"""
    if bytes_ < 1024:
        return f"{bytes_}B"
    elif bytes_ < 1024 * 1024:
        return f"{bytes_/1024:.1f}KB"
    else:
        return f"{bytes_/1024/1024:.1f}MB"


def download_tile(url, retries=3):
    """下载一张瓦片，返回字节数据"""
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={
                'User-Agent': 'Mozilla/5.0 (compatible; TileDownloader/1.0)',
                'Referer': 'https://www.amap.com/',
            })
            with urllib.request.urlopen(req, timeout=10) as resp:
                return resp.read()
        except Exception as e:
            if attempt < retries - 1:
                time.sleep(1)
            else:
                raise e
    return None


def main():
    parser = argparse.ArgumentParser(description='下载地图瓦片用于离线地图')
    parser.add_argument('--lat', type=float, required=True, help='中心纬度')
    parser.add_argument('--lon', type=float, required=True, help='中心经度')
    parser.add_argument('--radius', type=float, default=5, help='覆盖半径(公里)，默认5')
    parser.add_argument('--z-min', type=int, default=12, help='最小缩放级别，默认12')
    parser.add_argument('--z-max', type=int, default=17, help='最大缩放级别，默认17')
    parser.add_argument('--source', choices=TILE_SOURCES.keys(), default='gaode',
                        help='瓦片源，默认gaode')
    parser.add_argument('--output', default='./tiles', help='输出目录')
    parser.add_argument('--dry-run', action='store_true', help='只统计不下载')
    args = parser.parse_args()

    source = TILE_SOURCES[args.source]
    print(f"瓦片源: {source['label']}")
    print(f"中心点: {args.lat}, {args.lon}")
    print(f"覆盖半径: {args.radius}km")
    print(f"缩放范围: Z{args.z_min} - Z{args.z_max}")
    print()

    total_tiles = 0
    total_bytes = 0
    zoom_stats = []

    for z in range(args.z_min, args.z_max + 1):
        x_min, x_max, y_min, y_max = tile_bounds_for_area(
            args.lat, args.lon, args.radius, z)
        count = (x_max - x_min + 1) * (y_max - y_min + 1)
        total_tiles += count

        print(f"  Z{z:2d}:  x[{x_min}..{x_max}] y[{y_min}..{y_max}]  {count} tiles")

        if not args.dry_run:
            # 创建目录
            z_dir = os.path.join(args.output, str(z))
            os.makedirs(z_dir, exist_ok=True)

            tiles_in_zoom = 0
            bytes_in_zoom = 0

            for x in range(x_min, x_max + 1):
                x_dir = os.path.join(z_dir, str(x))
                os.makedirs(x_dir, exist_ok=True)

                for y in range(y_min, y_max + 1):
                    filepath = os.path.join(x_dir, f"{y}.png")
                    if os.path.exists(filepath):
                        size = os.path.getsize(filepath)
                        bytes_in_zoom += size
                        tiles_in_zoom += 1
                        continue

                    # 选择子域名
                    sd = source['subdomains'][(x + y) % len(source['subdomains'])]
                    url = source['url'].replace('{s}', sd).replace('{z}', str(z)).replace('{x}', str(x)).replace('{y}', str(y))

                    try:
                        data = download_tile(url)
                        if data and len(data) > 100:
                            with open(filepath, 'wb') as f:
                                f.write(data)
                            bytes_in_zoom += len(data)
                            tiles_in_zoom += 1
                            print(f"    ✓ Z{z}/{x}/{y}.png  ({format_size(len(data))})")
                        else:
                            print(f"    ✗ Z{z}/{x}/{y}.png  (空数据)")
                    except Exception as e:
                        print(f"    ✗ Z{z}/{x}/{y}.png  ({e})")

                    time.sleep(0.1)  # 避免请求过快

            zoom_stats.append((z, tiles_in_zoom, bytes_in_zoom))
            print(f"  → Z{z} 完成: {tiles_in_zoom} tiles, {format_size(bytes_in_zoom)}")
            print()
        else:
            print()

    print("=" * 50)
    print(f"总计: {total_tiles} 张瓦片")

    if args.dry_run:
        # 估算大小 (平均 15KB/张)
        est_size = total_tiles * 15 * 1024
        print(f"估算大小: ~{format_size(est_size)} (按平均15KB/张估算)")
        print(f"实际大小取决于地图内容(城市区域PNG通常10-30KB)")
        print(f"建议存储: ", end="")
        if est_size < 8 * 1024 * 1024:
            print(f"✅ 8MB flash 足够")
        elif est_size < 128 * 1024 * 1024:
            print(f"✅ 128MB TF卡 足够")
        elif est_size < 512 * 1024 * 1024:
            print(f"✅ 512MB TF卡 足够")
        else:
            print(f"✅ 建议 1GB 以上 TF卡")
    else:
        total_downloaded = sum(s[1] for s in zoom_stats)
        total_bytes = sum(s[2] for s in zoom_stats)
        print(f"已下载: {total_downloaded} 张, {format_size(total_bytes)}")
        print(f"输出目录: {os.path.abspath(args.output)}")


if __name__ == '__main__':
    main()
