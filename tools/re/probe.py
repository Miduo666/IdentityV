# -*- coding: utf-8 -*-
"""
活体探针：目标更新后第一个该跑的东西。不改代码、不重编译，纯只读验证。

它自己发现 pid / 模块基址 / 数据段，在数据段里定位两个锚点，把指针链走一遍并打分，
再把对象表列出来。一次运行就能回答"到底是哪一层坏了"。

用法:
    python probe.py            # 全量
    python probe.py quick      # 只验锚点和指针链，几秒
    python probe.py --redump   # 强制重新 dump 数据段（默认复用本地缓存）

前提: 设备已 root，目标在**前台**运行（后台时工作集被换出，读取会被安全检查拒绝）。
所有目标相关常量来自 config.py，本文件不含任何目标特定值。
"""
import os, sys, struct, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config as C
from memread import Mem, sh, PAGE, TMP, pid_of

HERE = os.path.dirname(os.path.abspath(__file__))


def discover():
    """找 pid、模块基址、以及扫描用的可写数据段"""
    pid = pid_of(C.PACKAGE)
    if not pid:
        sys.exit("目标没在运行")
    maps = sh("cat /proc/%d/maps" % pid).decode(errors="replace").splitlines()
    base, seg, seen = None, None, False
    for ln in maps:
        p = ln.split()
        if not p or "-" not in p[0]:
            continue
        a, b = (int(x, 16) for x in p[0].split("-"))
        if C.MODULE in ln:
            if base is None and "r-xp" in ln:
                base = a
            seen = True
            continue
        # 模块行之后第一个足够大的 rw 段 —— 匿名 .bss，运行期数据都在这
        if seen and seg is None and "rw" in p[1] and (b - a) // PAGE >= 2800:
            seg = (a, b)
    if base is None or seg is None:
        sys.exit("没找到模块基址或数据段")
    return pid, base, seg, maps


def find_anchors(blob, seg_start):
    """在数据段里定位两个锚点。返回 {"root": [...], "array": [...]}"""
    out = {"root": [], "array": []}
    sig = struct.pack("<Q", C.ANCHOR_SIG)
    o = blob.find(sig)
    while o != -1:
        if (seg_start + o) % 8 == 0:          # 目标代码只扫 8 字节对齐
            out["root"].append(seg_start + o)
        o = blob.find(sig, o + 1)
    for p in range(16, len(blob) - 64, 8):
        if struct.unpack_from("<Q", blob, p)[0] != C.ARRAY_MARK_U64:
            continue
        if struct.unpack_from("<I", blob, p + C.ARRAY_CHECK_U32_AT)[0] != C.ARRAY_CHECK_U32:
            continue
        if abs(struct.unpack_from("<f", blob, p + C.ARRAY_CHECK_F32_AT)[0] - 1.0) > 1e-9:
            continue
        out["array"].append(seg_start + p + C.ARRAY_HEAD_DELTA)
    return out


def ptr_ok(v):
    return v is not None and C.PTR_MIN < (v & C.PTR_MASK) < C.PTR_MAX


def resolve_chain(m, anchor):
    """在窗口里找能走通**整条**链的槽位。
    只校验第一跳的话，浮点垃圾会混进来 —— 必须走完全程再认。"""
    lo, hi, step = C.CHAIN_WINDOW
    m.fetch([anchor + o for o in range(lo, hi, step)], "")
    hits = []
    for off in range(lo, hi, step):
        v = m.q(anchor + off)
        if not ptr_ok(v):
            continue
        cur, ok = v & C.PTR_MASK, True
        for hop in C.CHAIN_HOPS:
            m.fetch([cur + hop], "")
            nxt = m.q(cur + hop)
            if not ptr_ok(nxt):
                ok = False
                break
            cur = nxt & C.PTR_MASK
        if ok:
            hits.append((off, v & C.PTR_MASK, cur + C.MATRIX_DELTA))
    return hits


def read_objects(m, head):
    m.fetch([head, head + 8], "")
    a, e = m.q(head), m.q(head + 8)
    if a is None or e is None:
        return None
    a &= C.PTR_MASK
    e &= C.PTR_MASK
    n = (e - a) // 8 if e > a else -1
    if not (0 < n < 20000):
        return {"count": n, "objs": []}
    m.fetch([a + i * 8 for i in range(n)], "")
    objs = [m.q(a + i * 8) & C.PTR_MASK for i in range(n) if ptr_ok(m.q(a + i * 8))]
    m.fetch([o + k for o in objs for k in (0, 0x100, 0x200)], "")
    return {"count": n, "objs": objs}


def class_names(m, objs):
    """走类名链。读出率通常只有 60~75%：链上任何一跳的页不常驻就读不到，
    而且已经被销毁的对象其链路内存已部分回收，重试多少轮都补不回来。
    **能用偏移判定就别依赖类名。**"""
    lvl = [(o, m.q(o + C.OFF["name_chain"])) for o in objs]
    for hop in C.NAME_HOPS:
        m.fetch([(v & C.PTR_MASK) + hop for _, v in lvl if v], "")
        lvl = [(o, (m.q((v & C.PTR_MASK) + hop) if v else None)) for o, v in lvl]
    m.fetch([(v & C.PTR_MASK) for _, v in lvl if v], "")
    strs = []
    for o, v in lvl:
        if not v:
            continue
        p = v & C.PTR_MASK
        s, L = m.q(p + C.NAME_STR_AT), m.q(p + C.NAME_LEN_AT)
        if s and L and 0 < (L & 0xFFFFFFFF) < 512:
            strs.append((o, s & C.PTR_MASK, L & 0xFFFFFFFF))
    m.fetch([a + k for _, a, L in strs for k in (0, L)], "")
    out = {}
    for o, a, L in strs:
        b = m.get(a, L)
        if b:
            out[o] = b.decode("utf-8", "replace")
    return out


def coord(m, o):
    c = m.q(o + C.OFF["coord_ptr"])
    if not c:
        return None
    cc = c & C.PTR_MASK
    v = [m.f(cc + C.OFF["coord_x"] + i * 4) for i in range(3)]
    return None if any(x is None for x in v) else v


def exists(m, o):
    """本局真实存在 = 实例化后才挂上的那个指针非空。
    注意不要拿'类型'字段当存在性用，未实例化的候选对象类型也是对的。"""
    return ptr_ok(m.q(o + C.OFF["exists"]))


def main():
    quick = "quick" in sys.argv
    redump = "--redump" in sys.argv

    pid, base, seg, maps = discover()
    print("pid=%d 基址=0x%X 数据段=0x%X-0x%X (%d页) maps=%d行"
          % (pid, base, seg[0], seg[1], (seg[1] - seg[0]) // PAGE, len(maps)))

    m = Mem(pid)
    blob = m.dump_range(seg[0], seg[1], os.path.join(TMP, "seg_%d.bin" % pid), redump)
    a = find_anchors(blob, seg[0])
    print("\n[锚点] 根签名 %d 处, 数组签名 %d 处" % (len(a["root"]), len(a["array"])))
    for x in a["root"]:
        print("   根签名 0x%X (模块相对 +0x%X)" % (x, x - base))
    for x in a["array"]:
        print("   数组头 0x%X (模块相对 +0x%X)" % (x, x - base))
    if not a["root"]:
        print("   !! 根签名消失 —— 这才是真正的引擎大改，需要活体数值扫描重找")

    target = None
    for anchor in a["root"]:
        hits = resolve_chain(m, anchor)
        print("\n[指针链] 锚点 0x%X 窗口内可用槽位 %d 个" % (anchor, len(hits)))
        for off, root, dst in hits:
            print("   +0x%03X -> 根=0x%X -> 目标=0x%X" % (off, root, dst))
        if len(hits) > 1 and len({h[2] for h in hits}) == 1:
            print("   (多个槽位指向同一目标：正常，是同一数组的多个元素，任选其一)")
        if hits:
            target = hits[-1][2]

    if target:
        m.fetch([target, target + 0x40], "")
        row = [m.f(target + i * 4) for i in range(16)]
        if all(r is not None for r in row):
            print("\n[目标矩阵] 0x%X" % target)
            for r in range(4):
                print("   " + " ".join("%11.4f" % row[r * 4 + c] for c in range(4)))

    if quick:
        return

    for head in a["array"]:
        r = read_objects(m, head)
        if not r or not r["objs"]:
            print("\n[数组 0x%X] 不可用 (count=%s)" % (head, r["count"] if r else "?"))
            continue
        objs = r["objs"]
        names = class_names(m, objs)
        m.fetch([(m.q(o + C.OFF["coord_ptr"]) & C.PTR_MASK) + k
                 for o in objs if m.q(o + C.OFF["coord_ptr"]) for k in (0, C.OFF["coord_x"])], "")
        alive = [o for o in objs if exists(m, o)]
        print("\n[数组 0x%X] count=%d 有效指针=%d 类名读出=%d 实例化=%d"
              % (head, r["count"], len(objs), len(names), len(alive)))

        dist = collections.Counter()
        for o in objs:
            v = m.f(o + C.OFF["type"])
            if v is not None:
                dist[round(v, 1)] += 1
        print("   类型字段值分布:", dict(dist.most_common(6)))

        byname = collections.Counter()
        for o in alive:
            nm = names.get(o, "(类名未读出)")
            byname[nm.split("\\")[-1] if "\\" in nm else nm] += 1
        print("   实例化对象按类名统计(前12):")
        for k, v in byname.most_common(12):
            print("      %-52s x%d" % (k[:52], v))


if __name__ == "__main__":
    main()
