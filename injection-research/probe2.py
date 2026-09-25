# -*- coding: utf-8 -*-
# IDV 进程内探针 v2 —— 只读 + 文件命令通道
import sys, os, time, json, threading, traceback

D = "/sdcard/Android/data/com.netease.dwrg/files"
SNAP = D + "/idv_snap.json"
TL   = D + "/idv_timeline.jsonl"
ERR  = D + "/idv_err.txt"
CMD  = D + "/idv_cmd.py"        # 往这里写 Python，探针会执行
COUT = D + "/idv_cmd_out.txt"   # 执行结果

def _w(p, t, m="w"):
    try:
        f = open(p, m); f.write(t); f.close(); return True
    except Exception:
        return False

def _err(w):
    _w(ERR, "[%.1f] %s\n%s\n" % (time.time(), w, traceback.format_exc()), "a")

def S(fn, d=None):
    try:
        return fn()
    except Exception:
        return d

def R(v, n=240):
    try:
        s = repr(v)
    except Exception as e:
        s = "<repr fail %s>" % e.__class__.__name__
    return s if len(s) <= n else s[:n] + "...(%d)" % len(s)

def cname(o):
    return S(lambda: type(o).__name__, "?")

def idict(o):
    """只读实例 __dict__，不触发 property"""
    try:
        return object.__getattribute__(o, "__dict__")
    except Exception:
        return {}

def attr_names(o):
    return sorted([k for k in idict(o).keys() if isinstance(k, str)])

def nums(o):
    out = {}
    for k, v in list(idict(o).items()):
        if isinstance(k, str) and isinstance(v, (int, float)) and not isinstance(v, bool):
            out[k] = float(v)
    return out

def GK():
    import game_kernel as gk
    return gk

def get_mgr():
    gk = S(GK)
    if gk is None:
        return None
    m = S(lambda: getattr(gk, "unit_mgr", None))
    if m:
        return m
    p = S(lambda: getattr(gk, "g_unit", None))
    r = S(lambda: getattr(p, "room", None)) if p else None
    return S(lambda: getattr(r, "unit_mgr", None)) if r else None

def ubt(mgr):
    return S(lambda: getattr(mgr, "units_by_type", None)) or {}

def units_of(mgr, t):
    l = S(lambda: ubt(mgr).get(t))
    return S(lambda: list(l), []) if l is not None else []

def all_units(mgr):
    out = []
    t = ubt(mgr)
    for k in S(lambda: sorted([x for x in t.keys() if isinstance(x, int)]), []):
        for u in units_of(mgr, k):
            out.append((k, u))
    return out

# ---------- 命令通道 ----------
def run_cmd():
    src = S(lambda: open(CMD).read(), "")
    if not src or not src.strip():
        return
    try:
        os.remove(CMD)
    except Exception:
        _w(CMD, "")
    buf = []
    def P(*a):
        buf.append(" ".join(str(x) for x in a))
    mgr = get_mgr()
    g = {"__builtins__": __builtins__, "P": P, "OUT": buf,
         "GK": S(GK), "mgr": mgr, "S": S, "R": R, "json": json, "time": time,
         "idict": idict, "attr_names": attr_names, "nums": nums, "cname": cname,
         "units_of": (lambda t: units_of(mgr, t)), "all_units": (lambda: all_units(mgr)),
         "ubt": (lambda: ubt(mgr)),
         "me": S(lambda: getattr(S(GK), "g_unit", None))}
    t0 = time.time()
    try:
        exec(src, g)
    except Exception:
        buf.append(traceback.format_exc())
    _w(COUT, "# %.2f ms\n" % ((time.time() - t0) * 1000) + "\n".join(str(x) for x in buf))

# ---------- 快照 ----------
GKEY = "genius_id_lv_lst"
FLAG_HINTS = ("used", "ability", "trigger", "consume", "psv", "rescue",
              "save", "borrow", "revive", "dying", "hp")

def flags_of(u):
    o = {}
    d = idict(u)
    for k in sorted(d.keys()):
        if not isinstance(k, str):
            continue
        lk = k.lower()
        if any(h in lk for h in FLAG_HINTS):
            o[k] = R(d.get(k), 110)
    return o

def probe_unit(u):
    r = {"cls": cname(u), "uid": R(S(lambda: getattr(u, "uid", None)), 30)}
    g = S(lambda: getattr(u, GKEY, "<missing>"))
    r["g_len"] = S(lambda: len(g), -1)
    r["g"] = R(g, 300)
    r["g_in_dict"] = GKEY in idict(u)
    r["support"] = R(S(lambda: getattr(u, "support_skill_id", None)), 40)
    r["flags"] = flags_of(u)
    return r

_prev = {}
def diff(key, o, now):
    cur = nums(o); out = []
    old = _prev.get(key)
    if old:
        t0, p = old
        dt = now - t0
        if dt > 0.3:
            for k, v in cur.items():
                if k in p and v != p[k]:
                    out.append({"a": k, "p": p[k], "n": v, "r": round((v - p[k]) / dt, 3)})
                elif k not in p:
                    out.append({"a": k, "p": None, "n": v, "r": 0})
            for k in p:
                if k not in cur:
                    out.append({"a": k, "p": p[k], "n": None, "r": 0})
    _prev[key] = (now, cur)
    return out[:60]

_n = 0
_seen_types = set()

def snapshot():
    global _n
    _n += 1
    now = time.time()
    o = {"i": _n, "t": round(now, 2)}
    mgr = get_mgr()
    if mgr is None:
        o["mgr"] = None
        return o
    o["mgr"] = R(mgr, 70)
    t = ubt(mgr)
    keys = S(lambda: sorted([x for x in t.keys() if isinstance(x, int)]), [])
    o["types"] = {}
    for k in keys:
        us = units_of(mgr, k)
        c = {}
        for u in us:
            n = cname(u)
            c[n] = c.get(n, 0) + 1
        o["types"][str(k)] = {"n": len(us), "c": c}
        sig = (k, tuple(sorted(c.keys())))
        if sig not in _seen_types:      # 只在首次出现时 dump 属性名，控制体积
            _seen_types.add(sig)
            o["types"][str(k)]["attrs"] = attr_names(us[0])[:500] if us else []
    gk = S(GK)
    me = S(lambda: getattr(gk, "g_unit", None))
    o["me"] = {"cls": cname(me), "uid": R(S(lambda: getattr(me, "uid", None)), 30)}
    players = units_of(mgr, 1) + units_of(mgr, 2)
    if me is not None and all(me is not p for p in players):
        players.append(me)
    o["units"] = [probe_unit(u) for u in players[:12]]
    o["ud"] = {}
    for u in players[:12]:
        uid = R(S(lambda u=u: getattr(u, "uid", None)), 30)
        d = diff("u:" + uid, u, now)
        if d:
            o["ud"][uid] = d
    h = units_of(mgr, 1)
    if h:
        sm = S(lambda: getattr(h[0], "skill_mgr", None))
        if sm is not None:
            o["sm"] = {"btn": R(S(lambda: getattr(sm, "skill_btn_status", None)), 150),
                       "last": R(S(lambda: getattr(sm, "last_cast_skill_id", None)), 20),
                       "d": diff("sm", sm, now)}
            sd = S(lambda: getattr(sm, "skill_dict", None)) or {}
            sk = {}
            for sid, s in S(lambda: list(sd.items())[:30], []):
                e = {"cls": cname(s)}
                for k in ("_cd_delta", "cd_time", "csv_cd_time", "game_start_cd"):
                    e[k] = S(lambda k=k, s=s: getattr(s, k, None))
                dd = diff("s:%s" % sid, s, now)
                if dd:
                    e["d"] = dd
                sk[str(sid)] = e
            o["sk"] = sk
    return o

def loop():
    time.sleep(2.0)
    while True:
        try:
            run_cmd()
        except Exception:
            _err("cmd")
        try:
            s = snapshot()
            txt = json.dumps(s, ensure_ascii=False, default=str)
            _w(SNAP, txt); _w(TL, txt + "\n", "a")
        except Exception:
            _err("snap")
        time.sleep(2.0)

try:
    _w(ERR, "probe2 boot %.1f py=%s\n" % (time.time(), sys.version.split()[0]), "a")
    _t = threading.Thread(target=loop); _t.daemon = True; _t.start()
    _w(ERR, "probe2 thread started\n", "a")
except Exception:
    _err("boot")
