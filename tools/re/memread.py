# -*- coding: utf-8 -*-
"""
目标进程安全内存读取器（PC 侧，需要设备 root）。与具体目标无关，可复用。

为什么不直接 dd /proc/<pid>/mem —— 三条安全策略：

  1) 地址必须落在 /proc/<pid>/maps 里一个可读(r)且非 ---p 的段内，否则拒读。
  2) 读之前查 /proc/<pid>/pagemap 的 present 位，只读**已常驻**的页。
     读非常驻页会让内核替目标进程把页 fault 回来，这是目标可观测的行为。
  3) maps 每次运行重新拉取。过期的 maps 会把目标新分配的区域误判成"未映射"，
     表现是"一部分数据读不出来"，极易被误判成偏移算错了。

pagemap 位含义: bit63=present(常驻), bit62=swapped(被换到 zram)。
应用切后台会被冻结、工作集换出，此时全是 swapped，本工具会拒读。
**前台活跃时工作集本来就常驻，读取零缺页** —— 那才是正确的读取窗口。

注意: 临时文件走纯 ASCII 目录。Windows 上 adb 对含非 ASCII 字符的本地路径会
静默失败（pull 不报错但文件不存在）。
"""
import os, struct, subprocess

try:
    from config import ADB
except ImportError:                                  # 没有 config.py 时退回 PATH
    ADB = "adb"

PAGE = 4096
TMP = os.path.join(os.environ.get("TEMP") or os.path.expanduser("~"), "re_procmem")
os.makedirs(TMP, exist_ok=True)


def sh(cmd):
    """以 root 执行一条设备命令，返回 stdout"""
    return subprocess.run([ADB, "shell", "su -c '%s'" % cmd], capture_output=True).stdout


def _batch(lines, name):
    """把一批 dd 打包成一个脚本跑，只来回一次。
    逐条 adb 调用每次 100~300ms，几百页就是几分钟；打包后是秒级。"""
    sp = os.path.join(TMP, name + ".sh")
    bp = os.path.join(TMP, name + ".bin")
    open(sp, "w", newline="\n").write("".join(lines))
    subprocess.run([ADB, "push", sp, "/data/local/tmp/%s.sh" % name], capture_output=True)
    sh("sh /data/local/tmp/{0}.sh > /data/local/tmp/{0}.bin; chmod 666 /data/local/tmp/{0}.bin".format(name))
    if os.path.exists(bp):
        os.remove(bp)
    r = subprocess.run([ADB, "pull", "/data/local/tmp/%s.bin" % name, bp], capture_output=True)
    if not os.path.exists(bp):
        raise RuntimeError("adb pull 失败: %s" % (r.stderr or r.stdout).decode(errors="replace")[:200])
    return open(bp, "rb").read()


def pid_of(package):
    out = sh("pidof %s" % package).decode(errors="replace").split()
    return int(out[0]) if out else None


class Mem:
    def __init__(self, pid, refresh=True):
        self.pid = pid
        self.maps = []
        self.cache = {}
        self.quiet = False
        mp = os.path.join(TMP, "maps%d.txt" % pid)
        if refresh or not os.path.exists(mp):
            raw = sh("cat /proc/%d/maps" % pid)
            if raw and len(raw) > 1000:
                open(mp, "wb").write(raw)
        for line in open(mp, encoding="utf-8", errors="replace"):
            p = line.split()
            if not p or "-" not in p[0]:
                continue
            a, b = p[0].split("-")
            self.maps.append((int(a, 16), int(b, 16), p[1], line.rstrip()))

    def _say(self, s):
        if not self.quiet:
            print(s)

    def region(self, addr):
        for a, b, perm, ln in self.maps:
            if a <= addr < b:
                return (a, b, perm, ln)
        return None

    def check(self, addr, n=8):
        r = self.region(addr)
        if r is None:
            return False, "地址未映射"
        if r[2][0] != 'r':
            return False, "段不可读(perm=%s)" % r[2]
        if addr + n > r[1]:
            return False, "跨段边界"
        return True, r[2]

    def fetch(self, addrs, note=""):
        """把这些地址所在的页读进缓存。做映射检查 + 常驻检查，不合格的跳过。"""
        pages = sorted({a // PAGE for a in addrs} - set(self.cache))
        if not pages:
            return
        good = []
        for pg in pages:
            ok, why = self.check(pg * PAGE, PAGE)
            if ok:
                good.append(pg)
            else:
                self._say("  [跳过] 页 0x%X : %s" % (pg * PAGE, why))
        if not good:
            return

        pm = _batch(["dd if=/proc/%d/pagemap bs=8 skip=%d count=1 2>/dev/null\n" % (self.pid, p)
                     for p in good], "pm")
        resident = []
        for i, pg in enumerate(good):
            if (i + 1) * 8 > len(pm):
                break
            ent = struct.unpack_from("<Q", pm, i * 8)[0]
            if ent >> 63:
                resident.append(pg)
            else:
                self._say("  [跳过] 页 0x%X %s，读它会触发缺页"
                          % (pg * PAGE, "已换出zram(目标多半在后台)" if (ent >> 62) & 1 else "非常驻"))
        if not resident:
            return

        blob = _batch(["dd if=/proc/%d/mem bs=4096 skip=%d count=1 2>/dev/null\n" % (self.pid, p)
                       for p in resident], "rd")
        for i, pg in enumerate(resident):
            chunk = blob[i * PAGE:(i + 1) * PAGE]
            if len(chunk) == PAGE:
                self.cache[pg] = chunk
        self._say("  [已读] %d 页 %s" % (len(resident), note))

    def dump_range(self, start, end, path, force=False):
        """整段落盘（一次 dd），用于离线分析大块数据"""
        npg = (end - start) // PAGE
        if force or not os.path.exists(path) or os.path.getsize(path) != npg * PAGE:
            sh("dd if=/proc/%d/mem of=/data/local/tmp/rng.bin bs=4096 skip=%d count=%d 2>/dev/null; "
               "chmod 666 /data/local/tmp/rng.bin" % (self.pid, start // PAGE, npg))
            subprocess.run([ADB, "pull", "/data/local/tmp/rng.bin", path], capture_output=True)
        return open(path, "rb").read()

    def get(self, addr, n):
        out = b""
        while n > 0:
            pg = addr // PAGE
            if pg not in self.cache:
                return None
            o = addr % PAGE
            k = min(n, PAGE - o)
            out += self.cache[pg][o:o + k]
            addr += k
            n -= k
        return out

    def q(self, addr):
        b = self.get(addr, 8)
        return None if b is None else struct.unpack("<Q", b)[0]

    def d(self, addr):
        b = self.get(addr, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def f(self, addr):
        b = self.get(addr, 4)
        return None if b is None else struct.unpack("<f", b)[0]
