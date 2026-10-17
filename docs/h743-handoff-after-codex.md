# H743 CDC+ETM：codex 复核后的接管状态（2026-10-17 晚）

> 上位文档：`docs/h743-etm-handoff.md`（环境/脚本/记录/结论/P0-P4）
> 详细实验报告：`docs/h743-cdc-etm-bringup.md`（§1–§9，含全部实测）
> 本文覆盖：**codex 复核后代码状态、它对 DeepSeek 的 review 逐条与回应、本轮补做的连续长窗验证、遗留问题。**

---

## 0. 一句话状态

`master` = `241e616`（codex 的 3 个提交），本轮在其上补了 `--filter-ns` CLI、烧录脚本 Tcl 路径修复、本文与连续长窗验证。
**板子：CPU=60MHz / TRACECLK=30MHz / HCLK=60MHz / TRDT 按 HCLK；COM43 在线；固件与 `build/fw.elf` 机器码一致（verified 31628 B）。**
**未 push。H743 在产品页上仍标"实验适配"。**

---

## 1. 代码状态

```
241e616 (codex) fix(records): 隔离未完成解码并允许并发浏览
1ad1566 (codex) fix(decode): 便携流式解码跟随运行时解码器目录
f28299e (codex) feat(etm): 接入 H743 网页，严格校验帧并显式记录软件滤波
77123fb (deepseek) docs(h743): 补 5.3.1 带宽利用率/异常包/ETF 4KB
... 之前 7 个 deepseek 提交
```

工作树干净；`captures/`、`records/` 仍在 .gitignore 里（约 10GB，交接需另拷）。
codex 的证据根目录（**不在仓库里**）：
`E:/web-etm-trace-coordination/artifacts/h743-acceptance/20261017-h743-web-review-c7a4e9/`
（`review-report.md`、`deepseek-pulse-review.json`、`diagnostic-4ns.csv` 4.4GB、`strict-demux-review.json`、`fresh-capture-validation-v2.json`、`repeat-summary-*.json`、`final-source-commit.patch` 等）

---

## 2. codex 做了什么

**`f28299e`（最大一个，44 文件）**
- **撤销** DeepSeek 的"帧内 FSYNC 容忍"，恢复**严格 TPIU 帧校验**（依据 Arm IHI0029D D4.2.2/D4.2.3 + OpenCSD 1.8.3 `trc_frame_deformatter.cpp`）
- 新增**默认关闭的显式软件滤波**：`trace_port.filter_pulses()` / `extract(..., glitch_ns)` / `decode_capture(..., glitch_ns)`，0–100ns，**不改原始 CSV/KVDAT**
- 网页接入 H743（能力标志 + 真实 IDR 选项 + 时钟页改用当前 RCC 推导，不再写死 H7B0 VCO560）
- JSONL/SQLite 分页 + 真实字节核算（取代 DeepSeek 的写零字段）；`streamed.schema=2`（decoded.json 只存摘要，事件只落 JSONL）；常规内存解码**恢复 200 万条保护**
- 修：240MHz 档误用 VCO=8f 超限、`-AhbDiv 1` 被忽略、USB 模式切换超时/返回码、`ERR UNKNOWN` 去掉、烧录脚本查退出码、`span` 需 `--live-buffer`
- 修：窄屏 iframe 150px、缩放与定位光标竞态、流式重解码混合代次、旧桥混用（`captureProtocol=2`）

**`1ad1566`** 修 DeepSeek `decode_stream.py` 的便携包 bug（按值捕获 ROOT → 冻结后找不到原生解码器），改为运行时读目录 + 回归

**`241e616`** 每条记录读写门：并发浏览、解码独占、`done` 在事务释放后发布、中断/截断标记（避免把截断 JSONL 当成功）

**它做到的真机验证**：4 次采集（10ms + 3×2ms），**63,490 / 63,490 / 63,530 / 210,602 条路径**，严格帧通过，**包错误/overflow/未处理/未核算/重叠/未知源 = 全 0**，392,910 个可验证后继 0 矛盾，Flash==ELF，首条恢复指令 == `trace_hook_entry`。

---

## 3. codex 的 review 逐条 ↔ 回应

| # | codex 的指控 | 我的回应 |
|---|---|---|
| 1 | **帧内完整 FSYNC 不可能合法**（IHI0029D D4.2.2 帧间同步、D4.2.3 帧内只允许半字同步；OpenCSD 也拒绝）。放宽规则会掩盖采样错误 | ✅ **我错了，认**。我当时只证明"数据字节恒为偶数 ⇒ `FF FF FF 7F` 不是数据"，**没有**证明"它是合法同步"——实际含义相反：**它证明字节流已被错位**（毛刺 → nibble 位移）。这是本轮最实质的方法错误 |
| 2 | 流式质量字段不能写零（`overlapBytes` 硬置 0、`unaccounted = input - processed`） | ✅ 接受（我上一轮自己也已承认措辞错误） |
| 3 | "流式 O(1)" 不成立（末尾把整份 decoded.json 读回字符串，且 events 双份） | ✅ 接受。我那个实现确实不是 O(1) |
| 4 | **不要**按 mtime/size 缓存输入 SHA | ✅ **接受它的反对**。我提的"哈希缓存"会削弱证据链；保留每次真 SHA（分块读）更正确 |
| 5 | H743 网页原来不允许配置 | ✅ 接受，已由它修 |
| 6 | 固件/脚本细节：240 档 VCO 超限、`-AhbDiv 1` 被忽略、`ERR UNKNOWN`、USB 超时/返回码、烧录退出码、`span --live-buffer` | ✅ 全部接受——**前两条和 `ERR UNKNOWN` 都是我留下的 bug/待办** |
| 7 | 窄屏 iframe 退回 150px | ✅ 接受，已修 |
| 8 | 缩放完成后覆盖新定位光标 | ✅ 接受，已修 |
| 9 | 流式重解码期间暴露混合代次 | ✅ 接受，已修 |
| 10 | 新页面混用旧桥会静默忽略滤波参数 | ✅ 接受，已修（`captureProtocol=2`） |

**对我上轮三处过度声明的处理**：

| 我的原话 | 状态 |
|---|---|
| "ETM 字节一个没丢" | ❌ **撤回**（只证明无未消费字节，异常点由 gaps/RESERVED 标出） |
| "CPU 越高越好（33,848 / 68,403）" | ⚠️ **降级为待复验**（那两份记录严格帧校验失败；各记录毛刺数不同，跨记录比较不公平） |
| "TRACECLK 40MHz 是物理上限" | ❌ **作废**（dense40 原始 CSV：CH0 最小宽度 2ns、**11,744 个 2ns 脉冲**、21.7% 异常半周期；它证明该配置失败，不足以定成 LA 通用上限） |
| "带宽利用率线性外推到 1.76GHz" | ⚠️ **降级**：平均带宽不可线性外推，也不能保证突发不 overflow（ETF 4KB 只吸收 ms/10µs 级突发） |

---

## 4. 本轮补做：连续长窗（初始化 → 枚举 → hello 回显）

codex 明确列为**未通过**的一项（"本轮未重做'初始化→Windows 枚举→单次 hello'的完整连续长窗硬件验收"）。

**采集条件**（一次连续采集，非拼接）：
`captures/h743/longflow` · CPU/HCLK=60MHz / TRACECLK=30MHz · 500MSa/s × **1.0s** · 触发 `--reset --neg-edge 1` ·
`trace_hook_entry`(0x08000B50) 入口断点 → 配 ETM/TPIU → 武装 LA → 放行 → **300 次连续 `hello world!\n`（每 4ms，首次 588ms）** →
回读 `hello world!\n` · 完整波形留在 `capture.kvdat`。

**解码条件**：严格帧校验（codex 恢复的规则）+ **解码前软件滤波 4ns**（原始 CSV/KVDAT 不改写）。

| 窗口 | 指令 | 执行 | 范围内存 | **包问题** | gaps | TPIU 帧 | 4ns 移除的时钟跳变 | 关键函数 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 初始化 `[0, 20ms]` | 379,277 | 322,416 | 75,892 | **0** | 1 | 1,796 | CH0=2（CH4=14,248） | `OTG_FS_IRQHandler`(7) `USBD_IRQHandler`(38) `dwc2_get_glb_intstatus`(7) `usbd_event_suspend_handler`(7) + main/SysTick |
| 回显 `[600, 620ms]` | 428,867 | 375,436 | 80,908 | **0** | 1 | 1,984 | CH0=6 | `bulk_out` → `cdc_write`(100) → `usbd_ep_start_write`(456) → `dwc2_ep_write`(196) → `handle_command`(228) → `strcmp`(120) → `trace_log_printf`(92) → `SEGGER_RTT_Write`(68) |

物理质量（codex 的只读工具 `tools/h743/review-captures.py`）：初始化窗 CH0 共 1,200,033 个跳变，**只有 1 个 2ns 脉冲**（早期 `full60` 是 66 个），最小宽度 2.0ns → 这一份的时钟质量明显优于此前记录。

**边界（必须如实标注）**：这是**同一次连续采集**（一份 KVDAT、一个触发点），但**解码是分窗做的**。
把 587ms 一次解完要 ~11M 条指令、GB 级文件、十几分钟，属于"能但代价大"；按 codex 的口径，**分窗解码不能冒充"单窗完整通过"**。

产物：`captures/h743/longflow/`（`decoded.json`、`etm-simplified.c`、`span-0p6-0p62/`、`capture.kvdat`）。

---

## 5. 遗留问题（按优先级）

1. **40MHz / 频率上限**：结论作废，需在**时钟无毛刺**的条件下重测（当前手段：4ns 软件滤波 + 严格帧；先要弄清 LA 的滤波是否作用于 Socket 导出——codex 说这项 A/B 仍未做）。
2. **长窗单次解码**：分窗可做，单窗代价大；若要"一气呵成"的单窗，需要更大内存 / 更长的解码预算（网页侧首建索引也是线性）。
3. **LA 滤波 ↔ CSV 导出的关系**：`cpu60-trace30` 的 KVDAT 存了 2 样本/4ns 滤波配置，但当时导出的 CSV 仍含 2ns 脉冲 → **配置快照不能证明导出已滤波**，需同一次采集做 0/4ns 两次导出的 A/B。
4. **H743 仍标实验适配、未 push**；同 DEV_ID 的 H742/H750/H753 未宣称验收。
5. **原生 Chrome WebUSB 授权、物理拔插重枚举、其他芯片实板回归**未做（codex 用只在证据目录存在的 USB 转发适配层完成了网页验收，已在报告里声明）。
6. **240MHz 档仅构建通过**，未烧录。
7. 我提出的"哈希缓存"优化**不做**（会削弱证据链）。

---

## 6. 工具与命令（本轮新增/变化）

```powershell
# 解码时开软件滤波（本轮给 CLI 补的，之前只有网页能传）
python -X utf8 tools/h743_etm.py decode --record <record> --filter-ns 4
python -X utf8 tools/h743_etm.py span   --record <record> --start 0.60 --end 0.62 --live-buffer --filter-ns 4
python -X utf8 tools/h743_etm.py capture --record <record> --at-entry trace_hook_entry `
    --trigger "--reset --neg-edge 1" --time 1.0 --rate 500000000 `
    --echo --echo-repeat 300 --echo-interval 0.004 --export-span 0 0.020 --overwrite

# 物理质量复核（codex 的只读工具：每通道跳变数/最小宽度/2ns/4ns 计数 + 已存 TPIU 的严格帧校验）
python -X utf8 tools/h743/review-captures.py <record> [<record2>...] --out <新的报告.json>

# 烧录（已修 Tcl 反斜杠问题）
pwsh -NoProfile -File tools/h743/h743-flash.ps1 [-Elf <abs path>]
```

固件构建（codex 版 `build.ps1` 参数有变化：`-AhbDiv` 默认 **0** 表示沿用档位自带 HPRE，另有 `-OutputDir` / `-NoPublish`）：

```powershell
pwsh -NoProfile -File firmware\stm32h743_cdc_trace\build.ps1 -CoreMHz 60 -TraceR 8 `
     -Gcc 'E:/Share/env-windows/tools/gnu_gcc/arm_gcc/mingw/bin/arm-none-eabi-gcc.exe'
```

---

## 7. 新踩的坑（本轮）

1. **`h743-flash.ps1` 的路径不能带反斜杠进 OpenOCD 的 `-c "..."`**：那是 **Tcl** 字符串，`\f`/`\b`/`\t` 会被当转义，表现为
   `couldn't open E:web-etm-traceirmwarestm32h743_cdc_traceuildw.elf`。已修（`$Elf -replace '\\','/'`）。
   **凡把路径塞进 OpenOCD `-c` 的地方都要注意。**
2. **入口断点地址随固件变**：codex 重建固件后 `trace_hook_entry` 从 `0x08000B48` 变成 `0x08000B50`。
   用旧 `build/fw.elf` 会表现为"复位后没有命中入口（超时）"。**换固件后必须重建再拿地址**。
3. 板子被复位/重新烧录后，`g_trace` 在 `0x20003000`：`34524354`(TRC4) + sysclk + ready，可做一键盘点。
