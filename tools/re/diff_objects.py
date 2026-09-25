# -*- coding: utf-8 -*-
"""
字段判别器：给两组对象，自动找出"取值完全不重叠"的字段偏移。

这是整套流程里最有产出的一个工具。典型场景：界面上画出了本不该出现的东西，
你手上有它的地址，同时知道哪些是真的 —— 让它把两组的每个 4 字节列对比一遍，
取值集合不相交的那些偏移就是候选判据。

用法:
    python diff_objects.py --filter <类名子串> --list              # 先肉眼看
    python diff_objects.py --filter <类名子串> --bad 0x... 0x...   # 做差分
    python diff_objects.py --filter <类名子串> --bad 0x... --samples 4   # 多次验证

**一定要多次采样**：单次找出的"判别字段"很可能只是巧合，或者只是个随时间
变化的状态位。判据选错的代价是线上继续出错，而且很难复现。
"""
import os, sys, time, struct, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config as C
from memread import Mem, TMP
from probe import discover, find_anchors, read_objects, class_names, coord, exists

OBJ_SPAN = 0x300            # 每个对象比对多少字节


def snapshot(pid, seg, filt, refresh):
    m = Mem(pid, refresh=refresh)
    m.quiet = True
    blob = m.dump_range(seg[0], seg[1], os.path.join(TMP, "seg_%d.bin" % pid), False)
    heads = find_anchors(blob, seg[0])["array"]
    if not heads:
        return m, [], {}
    r = read_objects(m, heads[0])
    objs = (r or {}).get("objs", [])
    names = class_names(m, objs)
    m.fetch([(m.q(o + C.OFF["coord_ptr"]) & C.PTR_MASK) + k
             for o in objs if m.q(o + C.OFF["coord_ptr"]) for k in (0, C.OFF["coord_x"])], "")
    sel = [o for o in objs if filt.lower() in names.get(o, "").lower()] if filt else objs
    return m, sel, names


def show(m, o, names):
    xyz = coord(m, o)
    st = m.d(o + C.OFF["state"])
    ty = m.f(o + C.OFF["type"])
    return "0x%X %s 已实例化=%-3s 状态=%-10s 类型=%-9s  %s" % (
        o,
        ("(%8.1f,%7.2f,%8.1f)" % tuple(xyz)) if xyz else "坐标N/A               ",
        "是" if exists(m, o) else "否",
        ("0x%X" % st) if st is not None else "N/A",
        ("%.4g" % ty) if ty is not None else "N/A",
        (names.get(o, "(类名未读出)"))[:46])


def fmt_set(vs):
    out = []
    for v in sorted(vs)[:3]:
        fv = struct.unpack("<f", struct.pack("<I", v))[0]
        out.append("0x%X%s" % (v, ("(%.4g)" % fv) if -1e9 < fv < 1e9 else ""))
    return " ".join(out) + ("..." if len(vs) > 3 else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="", help="按类名子串筛选对象")
    ap.add_argument("--bad", nargs="*", default=[], help="已知'假'的对象地址(十六进制)")
    ap.add_argument("--list", action="store_true", help="只列出，不做差分")
    ap.add_argument("--samples", type=int, default=1, help="重复采样次数")
    args = ap.parse_args()
    bad_addrs = {int(x, 16) for x in args.bad}

    pid, base, seg, _ = discover()
    print("pid=%d" % pid)

    for s in range(args.samples):
        m, sel, names = snapshot(pid, seg, args.filter, refresh=(s == 0))
        n_exist = sum(1 for o in sel if exists(m, o))
        print("\n===== 采样 %d/%d : 匹配 %d 个, 其中已实例化 %d 个 ====="
              % (s + 1, args.samples, len(sel), n_exist))

        if args.list:
            for o in sel:
                print("  " + show(m, o, names))
            time.sleep(2)
            continue

        good = [o for o in sel if o not in bad_addrs]
        bad = [o for o in sel if o in bad_addrs]
        if not bad:
            print("  指定地址不在本次对象表里(可能已重开局)。当前列表：")
            for o in sel:
                print("  " + show(m, o, names))
            time.sleep(2)
            continue
        print("  真实 %d 个 / 可疑 %d 个" % (len(good), len(bad)))

        blobs = {o: b for o, b in ((o, m.get(o, OBJ_SPAN)) for o in sel) if b}
        print("  --- 取值完全不重叠的字段(候选判据) ---")
        for off in range(0, OBJ_SPAN - 4, 4):
            gv = {struct.unpack_from("<I", blobs[o], off)[0] for o in good if o in blobs}
            bv = {struct.unpack_from("<I", blobs[o], off)[0] for o in bad if o in blobs}
            if gv and bv and not (gv & bv):
                tag = "  <- 真实组取值唯一，最像判据" if len(gv) == 1 else ""
                print("    +0x%03X  真实={%s}  可疑={%s}%s" % (off, fmt_set(gv), fmt_set(bv), tag))
        time.sleep(2)

    print("\n提示：候选里优先挑**语义上属于'存在性/实例化'**的字段（比如一个实例化后才非空的指针），")
    print("      不要挑'刚好能分开'的状态位 —— 状态会随时间变化，判据迟早失效。")


if __name__ == "__main__":
    main()
