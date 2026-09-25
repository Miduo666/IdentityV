# -*- coding: utf-8 -*-
"""
本地配置模板。复制为 config.py 后填写，config.py 已被 .gitignore 忽略。

这些值全部是**目标相关**的，随目标版本变化，属于各人自己的分析成果，
所以不进仓库。工具本身不含任何目标特定常量。
"""

# --- 环境 ---
ADB = r"adb"                    # adb 可执行文件路径，在 PATH 里就保持 "adb"
PACKAGE = ""                    # 目标包名
MODULE = ""                     # 主模块 .so 文件名

# --- 扫描锚点 ---
# 在模块的可写数据段里定位根对象用的 8 字节签名（小端 u64）。
# 怎么找：见 tools/re/README.md 的"锚点从哪来"。
ANCHOR_SIG = 0x0000000000000000

# 数组头签名：(u64 值, 前若干字节的校验) —— 按自己的目标填
ARRAY_MARK_U64 = 0               # 命中的 u64 值
ARRAY_CHECK_U32_AT = -8          # 相对命中处的偏移，该处应为 ARRAY_CHECK_U32
ARRAY_CHECK_U32 = 0
ARRAY_CHECK_F32_AT = -16         # 该处应为 1.0f
ARRAY_HEAD_DELTA = 0             # 命中处 + 该值 = 数组头(头[0]=起始指针, 头[8]=结束指针)

# --- 指针链 ---
CHAIN_WINDOW = (0x0, 0x0, 8)     # (起, 止, 步长) 在签名之后这个窗口里找根指针槽位
CHAIN_HOPS = ()                  # 根 -> ... -> 目标矩阵 的逐跳偏移，如 (0xa58,)
MATRIX_DELTA = 0                 # 最后一跳再加这个值 = 矩阵首地址

# --- 对象结构体偏移 ---
OFF = {
    "coord_ptr":   0x00,         # -> 坐标子对象
    "coord_x":     0x00,         # 坐标子对象内，三个连续 float
    "state":       0x00,         # 状态字
    "type":        0x00,         # 类型标记(float)
    "exists":      0x00,         # 存在性：实例化后才非空的指针
    "name_chain":  0x00,         # 类名链入口
}
NAME_HOPS = ()                   # 类名链逐跳偏移
NAME_STR_AT = 0x00               # 链末端对象里，字符串指针的偏移
NAME_LEN_AT = 0x00               # 链末端对象里，字符串长度的偏移

# --- 指针合法性 ---
# 上下界都要卡。只卡下界的话，浮点垃圾会被当成合法指针，制造大量假候选。
PTR_MIN = 0x5000000000
PTR_MAX = 0x8000000000
PTR_MASK = 0x00FFFFFFFFFFFFFF    # 有些引擎给指针高位打标签，读出来要掩掉
