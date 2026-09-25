# -*- coding: utf-8 -*-
"""本地配置 —— 已被 .gitignore 忽略，不要提交。"""

# adb 可执行文件路径。adb 在 PATH 里就保持 "adb"；
# 否则改成本机完整路径，通常是 <用户目录>\AppData\Local\Android\Sdk\platform-tools\adb.exe
ADB = r"adb"
PACKAGE = "com.netease.dwrg"
MODULE = "libclient.so"

ANCHOR_SIG = 0x656A624F72655028

ARRAY_MARK_U64 = 16384
ARRAY_CHECK_U32_AT = -8
ARRAY_CHECK_U32 = 257
ARRAY_CHECK_F32_AT = -16
ARRAY_HEAD_DELTA = 56

CHAIN_WINDOW = (0x300, 0x500, 8)
CHAIN_HOPS = (0xa58,)
MATRIX_DELTA = 0x2c0

OFF = {
    "coord_ptr":  0x28,
    "coord_x":    0xa0,
    "state":      0x70,
    "type":       0x1a0,
    "exists":     0x30,
    "name_chain": 0xf8,
}
NAME_HOPS = (0x0, 0x8, 0x20, 0x20)
NAME_STR_AT = 0x8
NAME_LEN_AT = 0x10

PTR_MIN = 0x5000000000
PTR_MAX = 0x8000000000
PTR_MASK = 0x00FFFFFFFFFFFFFF
