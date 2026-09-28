#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ROM 目录扫描工具 —— 递归扫描目录下所有 .nes 文件，提取 iNES/NES2.0 头的基础属性并导出 CSV。

用法:
    python tools/nes_scan.py                          # 扫描 D:\\NES, 输出到当前目录 nes_roms.csv
    python tools/nes_scan.py D:\\NES                  # 指定扫描目录
    python tools/nes_scan.py D:\\NES -o roms.csv      # 指定输出文件
    python tools/nes_scan.py D:\\NES --ext nes,unf    # 指定扩展名(默认 nes)

输出 CSV 为 UTF-8 带 BOM(Excel 双击不乱码), 列为:
    path,name,size_bytes,prg_kb,chr_kb,chr_file_kb,mapper,mirror,battery,trainer,tv,nes20,prg_crc32,status,remark

说明:
    prg_crc32 与模拟器 core/rom.c 的 crc32_p 一致(标准 CRC-32: 多项式 0xEDB88320, 初值 0xFFFFFFFF,
    结果取反), 只在 PRG 区(跳过 16 字节头与可选 512 字节 trainer)上计算, 可直接用于 Mapper ID 修正表。
"""

import argparse
import csv
import os
import sys
import zlib

# ---------------------------------------------------------------- 常量

HEADER_SIZE = 16          # iNES 头字节数
PRG_BLOCK = 0x4000        # PRG 每块 16KB
CHR_BLOCK = 0x2000        # CHR 每块 8KB
TRAINER_SIZE = 0x200      # trainer(金手指) 512 字节
READ_CHUNK = 0x10000      # 计算 CRC 时的读取块大小 64KB
PROGRESS_STEP = 500       # 每扫描多少个文件打印一次进度

NES_TAG = b"NES\x1a"

# 解析状态
ST_OK = "OK"
ST_OPEN_FAIL = "OPEN_FAIL"      # 文件打不开(权限/路径过长/损坏)
ST_BAD_HEADER = "BAD_HEADER"    # 文件不足 16 字节, 读不出头
ST_BAD_TAG = "BAD_TAG"          # 头不是 "NES^Z", 不是 iNES 格式
ST_ZERO_PRG = "ZERO_PRG"        # 头声明 PRG 块数为 0
ST_TRUNCATED = "TRUNCATED"      # 文件长度小于头声明的容量(截断/残缺)

CSV_COLUMNS = [
    "path", "name", "size_bytes", "prg_kb", "chr_kb", "chr_file_kb",
    "mapper", "mirror", "battery", "trainer", "tv", "nes20",
    "prg_crc32", "status", "remark",
]


# ---------------------------------------------------------------- 工具函数

def to_long_path(path):
    """
    在 Windows 上把路径转成扩展长度形式(\\\\?\\ 前缀), 规避 260 字符路径上限。

    \\\\?\\ 前缀要求绝对路径且不做归一化, 因此先 os.path.abspath() 再补前缀;
    非 Windows 或已经是扩展形式时原样返回。
    """
    if os.name != "nt":
        return path

    abs_path = os.path.abspath(path)
    if abs_path.startswith("\\\\?\\"):
        return abs_path
    return "\\\\?\\" + abs_path


def strip_long_path(path):
    """去掉 \\\\?\\ 前缀, 用于 CSV 输出(用户看到的是普通路径)。"""
    if path.startswith("\\\\?\\"):
        return path[4:]
    return path


def iter_files(root):
    """
    递归遍历 root 下所有文件, 产出 (真实路径, 可显示路径, 文件名)。

    采用迭代式栈遍历: 目录层级很深时不会触发递归深度限制;
    单个目录无权限时跳过并继续, 不让整个扫描中断。
    """
    stack = [root]

    while stack:
        current = stack.pop()
        try:
            entries = list(os.scandir(current))
        except OSError:
            sys.stderr.write("skip unreadable dir: %s\n" % strip_long_path(current))
            continue

        for entry in entries:
            try:
                if entry.is_dir(follow_symlinks=False):
                    stack.append(entry.path)
                elif entry.is_file(follow_symlinks=False):
                    yield entry.path, strip_long_path(entry.path), entry.name
            except OSError:
                # 单个条目取属性失败(如断链的符号链接), 忽略即可
                continue


def calc_prg_crc32(file_path, data_offset, prg_size):
    """
    流式计算 PRG 区 CRC32, 返回 (crc32 字符串, 是否完整读到 prg_size 字节)。

    分块读取, 不把整个 PRG 读进内存; 读不满说明文件被截断, 此时返回的 CRC 不完整,
    由调用方按 status 标记处理。
    """
    crc = 0
    read_total = 0

    try:
        with open(file_path, "rb") as fp:
            fp.seek(data_offset)
            while read_total < prg_size:
                want = min(READ_CHUNK, prg_size - read_total)
                chunk = fp.read(want)
                if not chunk:
                    break
                crc = zlib.crc32(chunk, crc)
                read_total += len(chunk)
    except OSError:
        return "", False

    if read_total != prg_size:
        return "", False
    return "%08X" % (crc & 0xFFFFFFFF), True


# ---------------------------------------------------------------- 单个 ROM 解析

def parse_rom(file_path, display_path, name):
    """
    解析单个 NES 文件, 返回一行 CSV 数据(dict)。

    头布局(iNES):
        0-3   标记 "NES^Z"
        4     PRG 块数(每块 16KB)
        5     CHR 块数(每块 8KB)
        6     bit0 镜像(1=垂直) / bit1 电池记忆 / bit2 trainer / bit3 四屏 / bit4-7 mapper 低 4 位
        7     bit0 PAL / bit4-7 mapper 高 4 位 / bit2-3 == 0b10 表示 NES2.0
    """
    row = {
        "path": display_path,
        "name": name,
        "size_bytes": "",
        "prg_kb": "",
        "chr_kb": "",
        "chr_file_kb": "",
        "mapper": "",
        "mirror": "",
        "battery": "",
        "trainer": "",
        "tv": "",
        "nes20": "",
        "prg_crc32": "",
        "status": ST_OPEN_FAIL,
        "remark": "",
    }

    try:
        file_size = os.path.getsize(file_path)
    except OSError:
        return row
    row["size_bytes"] = file_size

    try:
        with open(file_path, "rb") as fp:
            header = fp.read(HEADER_SIZE)
    except OSError:
        return row

    if len(header) < HEADER_SIZE:
        row["status"] = ST_BAD_HEADER
        return row

    if header[0:4] != NES_TAG:
        row["status"] = ST_BAD_TAG
        return row

    prg_blocks = header[4]
    chr_blocks = header[5]
    flag1 = header[6]
    flag2 = header[7]

    if prg_blocks == 0:
        row["status"] = ST_ZERO_PRG
        return row

    # NES2.0: flag2 的 bit2-3 == 0b10, mapper 号扩到 12 位(header[8] 低 4 位为高 4 位)
    nes20 = (flag2 & 0x0C) == 0x08
    if nes20:
        mapper = (flag1 >> 4) | (flag2 & 0xF0) | ((header[8] & 0x0F) << 8)
    else:
        mapper = (flag1 >> 4) | (flag2 & 0xF0)

    # 四屏优先于 bit0; 四屏时 bit0 无意义
    if flag1 & 0x08:
        mirror = "FourScreen"
    elif flag1 & 0x01:
        mirror = "Vertical"
    else:
        mirror = "Horizontal"

    has_trainer = 1 if (flag1 & 0x04) else 0
    prg_size = prg_blocks * PRG_BLOCK
    chr_size = chr_blocks * CHR_BLOCK
    data_offset = HEADER_SIZE + (TRAINER_SIZE if has_trainer else 0)

    row["prg_kb"] = prg_size // 1024
    row["chr_kb"] = chr_size // 1024
    row["mapper"] = mapper
    row["mirror"] = mirror
    row["battery"] = 1 if (flag1 & 0x02) else 0
    row["trainer"] = has_trainer
    row["nes20"] = 1 if nes20 else 0

    # 制式: NES2.0 看 header[12] 低 2 位, iNES 看 flag2 bit0
    if nes20:
        row["tv"] = ("NTSC", "PAL", "Dual", "Dendy")[(header[12] & 0x03)]
    else:
        row["tv"] = "PAL" if (flag2 & 0x01) else "NTSC"

    # 文件实际剩余的 CHR 容量(头里的 8 位块数常被 dump 写小, core/rom.c 会按文件修正)
    chr_file_size = file_size - data_offset - prg_size
    row["chr_file_kb"] = chr_file_size // 1024 if chr_file_size >= 0 else -1

    remarks = []
    if file_size < data_offset + prg_size + chr_size:
        row["status"] = ST_TRUNCATED
        remarks.append("file shorter than header declares")
    else:
        row["status"] = ST_OK

    crc_text, complete = calc_prg_crc32(file_path, data_offset, prg_size)
    if complete:
        row["prg_crc32"] = crc_text
    else:
        remarks.append("PRG data incomplete, CRC skipped")

    # CHR 实际容量是头声明的 2 的幂倍 -> 与 core/rom.c 的 VROM 修正逻辑一致
    if chr_size > 0 and chr_file_size > chr_size and chr_file_size % CHR_BLOCK == 0 and chr_file_size % chr_size == 0:
        ratio = chr_file_size // chr_size
        if ratio & (ratio - 1) == 0:
            remarks.append("CHR header %dK < file %dK (rom.c auto-fix)" % (chr_size // 1024, chr_file_size // 1024))

    # 非 NES2.0 时 header[8..15] 应为 0, 非 0 多为工具残留(如 "DiskDude!")
    if not nes20 and any(header[i] != 0 for i in range(8, HEADER_SIZE)):
        remarks.append("nonzero reserved bytes")

    row["remark"] = "; ".join(remarks)
    return row


# ---------------------------------------------------------------- 主流程

def main():
    parser = argparse.ArgumentParser(description="扫描目录下所有 NES ROM 并把基础属性导出为 CSV")
    parser.add_argument("root", nargs="?", default=r"D:\NES", help="扫描根目录(默认 D:\\NES)")
    parser.add_argument("-o", "--output", default="nes_roms.csv", help="输出 CSV(默认当前目录 nes_roms.csv)")
    parser.add_argument("--ext", default="nes", help="要收录的扩展名, 逗号分隔(默认 nes)")
    args = parser.parse_args()

    root = args.root
    if not os.path.isdir(root):
        sys.stderr.write("directory not found: %s\n" % root)
        return 1

    exts = set(e.strip().lower().lstrip(".") for e in args.ext.split(",") if e.strip())
    scan_root = to_long_path(root)

    rows = []
    total = 0

    for real_path, display_path, name in iter_files(scan_root):
        if exts and os.path.splitext(name)[1].lower().lstrip(".") not in exts:
            continue
        rows.append(parse_rom(real_path, display_path, name))
        total += 1
        if total % PROGRESS_STEP == 0:
            sys.stdout.write("scanned %d files...\n" % total)
            sys.stdout.flush()

    # 按路径排序, 保证多次扫描结果顺序稳定、便于 diff
    rows.sort(key=lambda r: r["path"].lower())

    try:
        with open(args.output, "w", newline="", encoding="utf-8-sig") as fp:
            writer = csv.DictWriter(fp, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            writer.writerows(rows)
    except OSError as err:
        sys.stderr.write("write csv failed: %s\n" % err)
        return 1

    # 汇总: 状态分布 + mapper 分布(按数量倒序)
    status_count = {}
    mapper_count = {}
    for row in rows:
        status_count[row["status"]] = status_count.get(row["status"], 0) + 1
        if row["status"] in (ST_OK, ST_TRUNCATED) and row["mapper"] != "":
            mapper_count[row["mapper"]] = mapper_count.get(row["mapper"], 0) + 1

    sys.stdout.write("total: %d -> %s\n" % (total, os.path.abspath(args.output)))
    for status in sorted(status_count, key=lambda s: -status_count[s]):
        sys.stdout.write("  %-12s %d\n" % (status, status_count[status]))
    sys.stdout.write("mappers: %d kinds\n" % len(mapper_count))
    for mapper in sorted(mapper_count, key=lambda m: (-mapper_count[m], m)):
        sys.stdout.write("  mapper %-4s %d\n" % (mapper, mapper_count[mapper]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
