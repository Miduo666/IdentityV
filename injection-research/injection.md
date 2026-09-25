# 注入子系统：ptrace + Dobby + 内嵌 CPython

> 路径占位符：`<007>` = `C:\Users\31297\Desktop\Code_For_id5\007开源骨骼进度绘制优化`，
> `<存档>` = `C:\Users\31297\Desktop\逆向存档`，`<NDK>` = `E:\android-ndk-r27d-windows\android-ndk-r27d`。

**状态：2026-09-22 在 `2026.0917_b6f53566` 上完整跑通。** 密码机进度已通过注入读出。
在这之前它一直被当成废案，废案的理由是错的（见最后一节"为什么以前以为它不行"）。

场景对象层（ESP 坐标）看 `idv-reverse-resume` 的 SKILL.md，跨进程读 CPython 看它的 `python-layer.md`（这两份不在本目录）。
**注入跑通之后，`python-layer.md` 那套跨进程走对象图的办法在注入路径下全部用不上**
—— 进程内直接 `import game_kernel` 就行，没有 dict 布局、没有防竞态三读、没有 zram 换出。

---

## 一、这套东西是什么

```
叠加层(qq.sh, root)
  └─ ptrace attach 游戏主线程
       ├─ 远程 mmap 一块 RWX
       ├─ 远程 open+dup2  把游戏 stdout/stderr 重定向到文件   ← 用户自己加的诊断
       ├─ 远程 dlopen("/data/local/tmp/.idv_hook_<pid>.so")
       ├─ 远程 dlsym("reload_hook") + 调用
       └─ detach, unlink 落地文件
                 │
                 ▼
     libgame_hook.so (976,280 字节, 内嵌在 EmbeddedHookSo.h)
       ├─ 静态初始化器启动 constructor → install_hook 线程
       ├─ 轮询 /proc/self/maps 等 libclient.so (301 次 ×100ms ≈ 30 秒超时)
       ├─ 按 5 个硬编码偏移算出 CPython API 地址
       ├─ Dobby inline-hook  PyRun_StringFlags
       └─ 游戏首次执行 Python 时触发 → 开线程 → PyGILState_Ensure
            → PyRun_SimpleString(PY_SCRIPT_CONTENT) → Release
                 │
                 ▼
     PY_SCRIPT_CONTENT: 561,747 字节**明文** Python (12853 行, 376 个函数)
       ├─ 采集: 骨骼/坐标/天赋/密码机-大门-椅子-板窗/buff/QTE/模仿者
       ├─ 淘金模式: 迷雾全开/种子预测/小地图打点/夜视/全地图几何
       └─ 主动改游戏: 强制无敌(伪造 SP_WILDMAN_EVENT)/快速翻窗/状态覆写/视角控制
       UDP 回传 127.0.0.1:55555(常规) 55557(骨骼)，控制入口 55556
```

**载荷是明文**：`.rodata` 偏移 `0x1B33C`，NUL 结尾。`strings` 就能全拿。
精确提取方式见第六节 —— **不要用 `extract_py.py`/`full_extracted_script.py`，那份是坏的**。

---

## 二、！！最大的坑：logcat 缓冲区只有 128KB ！！

**这一条是整件事卡了两个月的真因，排第一位。**

`.so` 的**全部**诊断走 `__android_log_print`，tag = **`GameHook`**。
它的动态导入表里**没有 `printf`/`puts`** —— 所以重定向 stdout/stderr 一个字都接不到。

而实测设备（Android 15）：

```
persist.logd.size.main = 131072   (128 KB，最小档)
实测 main 缓冲区只覆盖 ~88 秒
```

游戏日志量大，点完注入再去看 logcat，那几十行早被冲掉了。
**看到空 logcat ≠ hook 没跑。** 先放大缓冲区：

```powershell
& $adb logcat -G 16M          # 重启失效，每次排查前重设
& $adb logcat -g -b main      # 确认生效
& $adb logcat -c -b all       # 清空再测
& $adb logcat -d -b all -s GameHook:V -v time
```

---

## 三、诊断阶梯（停在哪一行 = 哪一步死的）

```
[*] Starting Hook Logic (Threaded Version)...    ← dlopen 成功, 静态初始化器跑了
[*] Waiting for libclient.so...
[*] Still waiting for libclient.so... (attempt %d)
Timeout waiting for libclient.so                 ← 30 秒没等到
[+] Found libclient.so base in maps: %p
[+] Calculated PyRun_StringFlags at %p           ← 这 5 行减去 base 就是当前用的偏移
[+] Calculated PyRun_SimpleString at %p
[+] Calculated PyGILState_Ensure at %p
[+] Calculated PyGILState_Release at %p
[+] Calculated PyErr_Print at %p
[+] Hooking PyRun_StringFlags at %p...
[+] PyRun_StringFlags Hook success               ← 只代表"字节写进去了", 不代表写对地方
[-] PyRun_StringFlags Hook failed: %d            ← -1 通常是"已经挂过了", 无害, 见下
[+] Hook installation complete. Waiting for trigger...
[!!!] PyRun_StringFlags HOOK TRIGGERED! [!!!]    ★ 这行出现 = 偏移真的对
[Thread] Acquiring GIL...                        ← 卡这 = GIL 拿不到
[Thread] Running Python script...
[Thread] Script execution success!               ★★ 载荷跑起来了
[Thread] Script execution failed with code: %d
[Thread] Printing Python traceback:              ← 正文在重定向的 .log 里, 不在 logcat
```

**正常会看到两遍启动**：`constructor`（静态初始化器，dlopen 时）和 `reload_hook`
（注入器显式调用）各跑一次 `install_hook`，后一次必然 `Hook failed: -1`。
**这是正常的，不是问题** —— Dobby 拒绝重复挂同一地址。

**实测时序（2026-09-22，成功那次）**：

```
Hook success        02:29:53.552
HOOK TRIGGERED      02:30:33.254   ← 等了 40 秒, 要游戏自己执行一次 Python
Acquiring GIL       02:30:33.554
Running script      02:30:33.561
Script success      02:30:33.858   ← 编译+执行 56 万字节只花 297 ms
```

**触发没有超时**，游戏不执行 Python 就永远不触发。点完注入要**进大厅/开局动一动**。
脚本执行只要 300ms，**不会造成可见卡顿** —— 卡死是别的问题。

---

## 四、5 个偏移：热更后必须重新定位

`.so` 里五个 CPython 函数地址全是**硬编码绝对偏移**，每次热更必废。

### 已验证的值

| 符号 | `2026.0917_b6f53566` | .so 原始值 | 差 |
|---|---|---|---|
| `PyGILState_Ensure` | `0x3C214DC` | 0x3C164DC | +0xB000 |
| `PyGILState_Release` | `0x3C21594` | 0x3C16594 | +0xB000（恒 = Ensure+0xB8）|
| `PyRun_StringFlags` | `0x3C82ED8` | 0x3C77ED8 | +0xB000 |
| `PyRun_SimpleString` | `0x3C844C4` | 0x3C794C4 | +0xB000 |
| `PyErr_Print` | `0x3C83220` | 0x3C7255C | **不是 +0xB000 —— 原值就是错的** |

`.so` 原始值对应的是某个中间热更版（**不是** `2026.0611.0155`，那版这 5 处落在函数尾声上）。

### 怎么重新找（照这个顺序，全部是证据不是猜）

1. **`PyGILState_Release`** —— 搜字符串 `"auto-releasing thread-state"`，
   全文件**唯一一处** ADRP+ADD xref，所在函数入口就是它。
2. **`PyGILState_Ensure`** —— 搜 `"Couldn't create autoTSSkey mapping"`（会有 4 处候选），
   取与 Release 相差 `0xB8` 的那个。两者相差 0xB8 是 `.so` 里 `add x22, x21, #0xb8` 写死的，
   **对不上就说明整个方案要改**。
3. **`PyRun_SimpleString`** —— 它内联了 `PyRun_SimpleStringFlags`，特征极明显：
   `adrp/add x0, "__main__"` → `bl PyImport_AddModule` → `bl PyModule_GetDict`
   → `mov w1,#0x101`(Py_file_input=257) → `mov x4,xzr`(flags=NULL) → `bl ???`
4. **`PyRun_StringFlags`** = 上一步那个 `bl` 的目标。**这是调用图证据，最硬**，
   比"往回扫函数入口"可靠得多。
5. **`PyErr_Print`** —— `PyRun_SimpleString` 里 `v==NULL` 分支：
   `ldr x0,[x8,#0x240]`(tstate) + `mov w1,#1` + `bl X` → `X` = `_PyErr_PrintEx`。
   再反查谁 **tail-call**（`b`，不是 `bl`）到 `X`，找紧跟在一条 `ret` 之后的三指令函数：
   `bl <取tstate>` / `mov w1,#1` / `b X` —— 那就是 `PyErr_Print(void)`。

⚠️ **不要用"往回扫 `stp x29,x30,[sp,#-N]!` 找函数入口"当最终判据。**
我用它把 `"Error in sys.excepthook:"` 归到了 `PyRun_StringFlags` 头上（实际属于
`_PyErr_PrintEx`，它的入口不是这个模式，被漏掉了）。差点据此推翻正确结论。
**调用点 > 字符串 xref > 函数入口回扫。**

脚本在 `scratchpad`（`findpy.py`）；numpy 向量化扫 4000 万条指令约几秒。

### 怎么打补丁（8 个位点，`.text` 的 `Off == Addr`，文件偏移 = 下表地址）

| 地址 | 原编码 | 含义 |
|---|---|---|
| `0xB5644` / `0xB5650` | `528c9b88` / `72a07828` | Ensure 低16 / 高16（w8）|
| `0xB5648` / `0xB5654` | `528fdb19` / `72a078f9` | PyRun_StringFlags（w25）|
| `0xB564C` / `0xB5658` | `52929889` / `72a078e9` | PyRun_SimpleString（w9）|
| `0xB5660` / `0xB566C` | `5284ab88` / `72a078e8` | PyErr_Print（w8）|
| `0xB5680` | `9102e2b6` | `add x22,x21,#0xb8`（Release 相对 Ensure，一般不用改）|

编码：`movz w<rd>,#imm16` = `0x52800000|(imm<<5)|rd`；
`movk w<rd>,#imm16,lsl #16` = `0x72A00000|(imm<<5)|rd`。

**打补丁前先校验现场编码**，对不上就别写。改完用 `1.py` 反向解出来回环对比一次。

**可以直接用 `IdentityV\injection-research\embed_payload.py` 生成头文件**，它会先校验现场编码、
`compile()` 载荷、写出后再回读 inspect 一次，比手写下面这段安全：

```powershell
python embed_payload.py build --so 1_patched.so --payload probe2.py --out EmbeddedHookSo.h `
       [--set Ensure=0x... --set RunStringFlags=0x... --set SimpleString=0x... --set ErrPrint=0x...]
```

手写的话，重新生成头文件（12 字节一行，跟原格式一致）：

```python
lines=["#ifndef EMBEDDED_HOOK_SO_H","#define EMBEDDED_HOOK_SO_H","",
       "alignas(16) static const unsigned char EMBEDDED_HOOK_SO[] = {"]
for i in range(0,len(out),12):
    lines.append("    "+" ".join("0x%02x,"%b for b in out[i:i+12]))
lines+=["};","","static const unsigned int EMBEDDED_HOOK_SO_SIZE = %d;"%len(out),"","#endif",""]
```

⚠️ `SoHookIntegration.cpp` 顶上有
`static_assert(EMBEDDED_HOOK_SO_SIZE == 976280)`，改大小要同步改它。

### ！！偏移写错会把游戏搞挂 ！！

2026-09-22 实测：旧偏移下 `0x3C77ED8` 在新版里是 `23 00 00 14` = `b #0x8c`，
**一条函数中段的分支指令**。Dobby 照样"挂成功"，但别的代码分支跳进那 12 字节跳板中间
→ 执行垃圾 → **游戏画面卡死、音乐还在放**（音频线程独立）。

所以：**挂之前先确认目标地址是函数入口**（`fd 7b bX a9` = `stp x29,x30,[sp,#-N]!`）。
`Hook success` 只说明字节写进去了，**不说明写对了**。

---

## 五、工程与操作

### 改哪个文件夹

**注入相关只有 `<007>`（`007开源骨骼进度绘制优化`）是活的**，
这跟 `idv-reverse-resume` 里"只改 IdentityV"的规则**相反**，注意别搞错：

| 文件 | 007 副本 | IdentityV |
|---|---|---|
| `SoHookIntegration.cpp` | **63,386 B（2026-07-29，带诊断）** | 57,395 B（原版，未改过）|
| `SoHookIntegration.h` | **508 B（带 RenderInjectDebugPanel）** | 477 B 原版 |
| `EmbeddedHookSo.h` | **已打补丁（2026-09-22），载荷是 probe2** | 原版（旧偏移 + 作者载荷）|
| `Android.mk` / `main.cpp` / `draw_Gui.cpp` | 注入已启用 | 全部注释掉 |

⚠️ **007 里 `jni\include\Android_draw\` 下几个 hook 文件的名字会误导人**（2026-09-22 用 `embed_payload.py inspect` 实测）：

| 文件 | 偏移 | 载荷 |
|---|---|---|
| `1.so` | 旧（`0x3C164DC`…）| 作者原版 561,747 B |
| **`1_patched.so`** | **新（`0x3C214DC`…）** | **作者原版 561,747 B** ← 要恢复作者功能用这个做基底 |
| `EmbeddedHookSo.h` | 新 | probe2 7,312 B |
| `EmbeddedHookSo.h.author_payload` | 新 | **probe2 7,312 B —— 名字是错的，里面不是作者载荷** |

`IdentityV` 的 `EmbeddedHookSo.h` 是旧偏移 + 作者载荷，没法直接用。

`IdentityV` 要启用注入的话：`Android.mk` 38/39 行、`main.cpp` 96/141 行、
`draw_Gui.cpp` 851/1226/1295/1367 行取消注释，并把 007 那份 `SoHookIntegration.cpp/.h` 覆盖过来。

原始未改动的 `.so`：`<存档>\2026.0611.0155\hook_so\1.so`（`<007>\jni\include\Android_draw\1.so` 是同一份）（md5 `e1207775...`），
以及本工程根目录下的 `007开源骨骼进度绘制优化.zip`（2026-07-17 快照，注入是**原装的**，不是用户加的）。

### 构建与推送

007 这份没有随机化，直接 ndk-build（别跑 `1.bat`，它 `move` 的文件名是过期的 `Identity5.sh`，
Android.mk 里 `LOCAL_MODULE := qq.sh`）：

```powershell
cd "<007>"
& "<NDK>\ndk-build.cmd" NDK_PROJECT_PATH=. `
    APP_BUILD_SCRIPT=jni\Android.mk NDK_APPLICATION_MK=jni\Application.mk -j8
```

**覆盖前要先 `pkill -f /data/qq.sh`**，否则 `cp` 报 `Text file busy`。

### UI 路径（默认全是关的，别以为没写）

1. `su -c /data/qq.sh`
2. 主面板勾选 **「骨骼与进度」** ← `show_sohook` 默认 `false`（`draw_Gui.cpp:200`）
3. 折叠头「骨骼与进度」里点 **「注入SO」** ← **注入不是自动的**，
   `SoHook::Update()` 只负责*检测*已加载的 SO
4. 独立窗口 **「注入调试信息」** 显示 `InjectLibrary()` 的结果，
   还有「导出到文件」→ `/sdcard/inject_debug.txt`

**同一个游戏进程只能注入一次**：`InjectLibrary` 开头 `FindModuleBase(".idv_hook_")`
命中就早退（"目标进程已加载该SO"），**不会重新调 `reload_hook`、不会有新日志**。
要重测必须**彻底重启游戏**。

### 「注入调试信息」怎么读

`InjectLibrary` 只在**失败**时给 `error` 赋值，最后无条件追加
` [重定向调试: ...]`。所以：

```
"...[重定向调试: open=0x.. dup2=0x.. openCallOk=1 fd=145 dup1=1/1 dup2=1/2]"
 ↑ 前面是空的  =  全链成功
```

前面有字 = 那一步失败：`远程mmap失败` / `目标进程dlopen失败` /
`目标进程dlsym(reload_hook)失败` / `远程调用reload_hook失败` / `目标进程已加载该SO`。

---

## 六、精确提取载荷脚本

**`full_extracted_script.py`（580,392 B）是坏的** —— 正则猜出来的，含 NUL，
`compile()` 直接报 `source code string cannot contain null bytes`。别用它。

正确做法：顺 `PY_SCRIPT_CONTENT` 的重定位项取。

```python
# .data 里 PY_SCRIPT_CONTENT @vaddr 0xF5AF0, 文件里是 0, 真值在 R_AARCH64_RELATIVE 的 addend
# RELA: 0xE198, 45192 字节, 每项 24 字节 (r_offset, r_info, r_addend)
start = addend_of(0xF5AF0)      # = 0x1B33C
end   = d.index(b'\x00', start)
payload = d[start:end]          # 561,747 字节, UTF-8, compile() 通过
```

---

## 七、为什么以前以为它不行（别再重复这条弯路）

按发现顺序，每一条都被实测推翻：

| 当时的判断 | 实际 |
|---|---|
| "logcat 没东西 = 注入没成功" | 缓冲区 128KB / 88 秒，日志被冲掉了 |
| "dup2 重定向能看到诊断" | `.so` 不导入 `printf`，诊断全走 logcat |
| "Android 15 namespace 会拒绝 /data/local/tmp 的 dlopen" | **没有**，实测 dlopen 成功，maps 里三段映射齐全 |
| "constructor 不在 `.init_array`，所以不会自动跑" | 确实不在，但由**静态初始化器**调用，照样跑 |
| "`Hook failed: -1` 是故障" | 是第二次重复挂，正常 |
| "偏移都废了要重找 5 个" | 4 个是齐齐 **+0xB000**，第 5 个原值本来就错 |

**方法论教训**：`error` 字符串为空、`fd=145`、`dup1=1/1` 这些**返回值本身就是证据**，
当时没往这个方向读。下次先把手上已有的输出榨干，再去怀疑机制。

---

## 八、把载荷换成自己的探针（调研用，2026-09-22 验证）

**这是注入最大的价值**：载荷只是 `.rodata` 里一段 NUL 结尾的字符串，可以整段替换成
任意 Python，在进程内以完整权限跑。一次就能问清楚跨进程猜半天的问题。

做法：

```python
so = bytearray(open("hook_patched.so","rb").read())   # 先打好 5 个偏移补丁的那份
START = 0x1B33C
end   = so.index(b"\x00", START)          # 原载荷 561,747 字节
assert len(src)+1 <= end-START            # 自己的脚本必须放得下
so[START:START+len(src)] = src
so[START+len(src)] = 0                    # 补 NUL，后面的残留字节不用管
```

然后重新生成 `EmbeddedHookSo.h` → ndk-build → 推送。
上面这段替换 + 生成头文件，`embed_payload.py build` 一条命令就做完了（见第四节）。
**改完务必回读校验那 4 组 `movz/movk` 还在**，别把偏移补丁覆盖掉。

探针本体的注意事项（`<存档>\2026.0917_b6f53566\hook_so\probe.py` 是跑通的那份，后继是同目录的 `probe2.py`）：

- **开 daemon 线程循环**，别在 `PyRun_SimpleString` 里同步干活 —— 那是持 GIL 的，会卡游戏
- 输出写 `/sdcard/Android/data/com.netease.dwrg/files/`（游戏以自己的 app uid `u0_aXXX` 身份写得进去）；
  拉回来要 `su -c cp` 到 `/sdcard/` 再 `adb pull`，目录是 `drwxrws---`，shell 读不到
- **读属性用 `object.__getattribute__(o,"__dict__")`**，不要无脑 `getattr` 所有名字 ——
  有的属性是 property，`getattr` 会触发副作用
- 每一层都 try/except，出错写独立的 err 文件；实测干净跑通时 err 文件里只有启动两行
- 想找"随时间变化的量"，就做**数值属性全量差分 + 算每秒变化率**，让答案自己跳出来，
  不要预先猜字段名。`_cd_delta` 就是这么找到的

**这一版的注入不会有任何游戏功能**（绘制/UDP 全没了），面板上 SO 数据计数恒为 0，
**这是正常的**。换回作者载荷即可恢复。

### 实测性能

`Script execution success!` 在 72 ms（作者那份 561KB 载荷是 297 ms）。
探针每 2 秒一轮，跑 245 秒 123 个样本，游戏无可感卡顿。

### 已经用它结掉的问题

| 问题 | 答案 |
|---|---|
| `genius_id_lv_lst` 是不是临时缓冲 | **不是**。`getattr` 与实例 `__dict__` 一致、类上无描述符、123 个样本 0 次空 |
| 技能剩余冷却在哪 | **`skill._cd_delta`**，秒，0=可用。见 `idv-replay-genius` 第四节 |
| `units_by_type` 权威全表 | 全部键的类名都读出来了，见 `python-layer.md` 第五节 |
| 绝处逢生"已消耗"标志 | **`CivilianUnit.ability_used[102] == True`**，本机和非本机玩家都已实测（见 `idv-replay-genius` 待办 #2）|
| CPython 版本 | **3.11.6**（`sys.version` 直接打出来的，不再是推断）|
