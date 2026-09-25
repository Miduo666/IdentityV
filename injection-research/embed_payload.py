# -*- coding: utf-8 -*-
"""把 Python 载荷装进 hook so，生成 EmbeddedHookSo.h。用法见同目录 SKILL.md。

  python embed_payload.py inspect <x.so | EmbeddedHookSo.h>
  python embed_payload.py build --so <base.so> --payload <x.py> --out <EmbeddedHookSo.h>
                                [--set Ensure=0x... --set RunStringFlags=0x... ...]
"""
import argparse, re, sys

SO_SIZE = 976280
PAYLOAD_START = 0x1B33C           # .rodata 里 PY_SCRIPT_CONTENT 的起点
PAYLOAD_REGION = 561747           # 原作者载荷长度，其后一字节是 NUL，这是可用上限

# 名字 -> (movz 位置, movk 位置, 目标寄存器)
SLOTS = {
    "Ensure":         (0xB5644, 0xB5650, 8),
    "RunStringFlags": (0xB5648, 0xB5654, 25),
    "SimpleString":   (0xB564C, 0xB5658, 9),
    "ErrPrint":       (0xB5660, 0xB566C, 8),
}
RELEASE_ADD = 0xB5680             # add x22, x21, #0xb8  (Release = Ensure + 0xB8)
RELEASE_ADD_INSN = 0x9102E2B6


def load(path):
    data = open(path, "rb").read()
    if path.lower().endswith(".h") or not data.startswith(b"\x7fELF"):
        text = data.decode("utf-8", "ignore")
        body = text[text.index("{") + 1: text.index("};")]
        data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
    if not data.startswith(b"\x7fELF"):
        sys.exit("不是 ELF: " + path)
    if len(data) != SO_SIZE:
        sys.exit("大小 %d != %d，不是这套 hook so" % (len(data), SO_SIZE))
    return bytearray(data)


def u32(b, a):
    return int.from_bytes(b[a:a + 4], "little")


def put32(b, a, v):
    b[a:a + 4] = v.to_bytes(4, "little")


def check_slot(b, name):
    lo, hi, rd = SLOTS[name]
    wlo, whi = u32(b, lo), u32(b, hi)
    if (wlo & 0xFFE0001F) != (0x52800000 | rd) or (whi & 0xFFE0001F) != (0x72A00000 | rd):
        sys.exit("%s 现场编码不对 (%08x/%08x)，不是预期的 movz/movk w%d，拒绝继续" % (name, wlo, whi, rd))
    return ((wlo >> 5) & 0xFFFF) | (((whi >> 5) & 0xFFFF) << 16)


def payload_info(b):
    end = b.index(b"\x00", PAYLOAD_START)
    head = bytes(b[PAYLOAD_START:PAYLOAD_START + 80]).decode("utf-8", "replace").split("\n")
    return end - PAYLOAD_START, [l for l in head if l.strip()][:2]


def inspect(b):
    for name in SLOTS:
        print("  %-15s 0x%X" % (name, check_slot(b, name)))
    ok = u32(b, RELEASE_ADD) == RELEASE_ADD_INSN
    print("  Release         = Ensure + 0xB8  (%s)" % ("指令在" if ok else "指令被改过!"))
    n, head = payload_info(b)
    print("  载荷 %d 字节: %s" % (n, " / ".join(head)))


def write_header(b, path):
    lines = ["#ifndef EMBEDDED_HOOK_SO_H", "#define EMBEDDED_HOOK_SO_H", "",
             "alignas(16) static const unsigned char EMBEDDED_HOOK_SO[] = {"]
    for i in range(0, len(b), 12):
        lines.append("    " + " ".join("0x%02x," % x for x in b[i:i + 12]))
    lines += ["};", "", "static const unsigned int EMBEDDED_HOOK_SO_SIZE = %d;" % len(b), "", "#endif", ""]
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))


def build(args):
    b = load(args.so)
    if b[PAYLOAD_START + PAYLOAD_REGION] != 0:
        sys.exit("载荷区末尾不是 NUL，基底 so 布局对不上")
    for item in args.set or []:
        name, _, val = item.partition("=")
        if name not in SLOTS:
            sys.exit("未知位点 %s，可选: %s" % (name, ", ".join(SLOTS)))
        check_slot(b, name)
        v = int(val, 16)
        lo, hi, rd = SLOTS[name]
        put32(b, lo, 0x52800000 | ((v & 0xFFFF) << 5) | rd)
        put32(b, hi, 0x72A00000 | (((v >> 16) & 0xFFFF) << 5) | rd)
    src = open(args.payload, "rb").read()
    if b"\x00" in src:
        sys.exit("载荷里有 NUL 字节")
    compile(src.decode("utf-8"), args.payload, "exec")   # 语法错误在这里暴露，别等上机
    if len(src) + 1 > PAYLOAD_REGION + 1:
        sys.exit("载荷 %d 字节，超过上限 %d" % (len(src), PAYLOAD_REGION))
    b[PAYLOAD_START:PAYLOAD_START + len(src)] = src
    b[PAYLOAD_START + len(src)] = 0
    write_header(b, args.out)
    print("已写出", args.out)
    inspect(load(args.out))                               # 回读校验


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("inspect"); p.add_argument("file")
    p = sub.add_parser("build")
    p.add_argument("--so", required=True, help="基底 so（已打好偏移补丁的那份，如 1_patched.so）")
    p.add_argument("--payload", required=True, help="要装进去的 Python 源码")
    p.add_argument("--out", required=True, help="输出的 EmbeddedHookSo.h")
    p.add_argument("--set", action="append", help="改偏移，如 Ensure=0x3C214DC，可重复")
    a = ap.parse_args()
    if a.cmd == "inspect":
        inspect(load(a.file))
    else:
        build(a)


if __name__ == "__main__":
    main()
