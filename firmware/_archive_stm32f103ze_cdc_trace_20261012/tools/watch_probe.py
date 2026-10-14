#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
watch_probe.py —— 用 DWT 数据观察点抓"谁在写这个地址"

用于查 `g_trace` 这类"永远是垃圾值"的问题：是没人初始化它，还是运行期被踩。
Cortex-M3 有 4 个 DWT 比较器，这里用第 0 个，配置成"写观察点"。

用法：
  python watch_probe.py --target stm32f103cb --addr 0x20004fc0 --seconds 5

输出：观察点命中次数，以及命中瞬间的 CPU 现场（PC/LR + 调用栈前几层粗解）。
"""
import argparse
import sys
import threading
import time

WATCHDOG_S = 90

# DWT / DEMCR（Cortex-M3）
DEMCR = 0xE000EDFC
DWT_CTRL = 0xE0001000
DWT_COMP0 = 0xE0001020
DWT_MASK0 = 0xE0001024
DWT_FUNCTION0 = 0xE0001028
DHCSR = 0xE000EDF0


def _watchdog():
    print(f"[watchdog] 超过 {WATCHDOG_S}s，强制退出", file=sys.stderr)
    sys.exit(9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--addr", required=True, help="要监视的地址，如 0x20004fc0")
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--freq", type=int, default=1_000_000)
    args = ap.parse_args()

    threading.Thread(target=_watchdog, daemon=True).start()

    from pyocd.core.helpers import ConnectHelper

    addr = int(args.addr, 0)
    session = ConnectHelper.session_with_chosen_probe(
        target_override=args.target, frequency=args.freq, options={"auto_unlock": False}
    )
    if session is None:
        print("连接探针失败", file=sys.stderr)
        return 2

    with session:
        t = session.target
        t.halt()
        # TRCENA = 1
        demcr = t.read32(DEMCR)
        t.write32(DEMCR, demcr | (1 << 24))
        # DWT 解锁（部分 M3 需要）
        try:
            t.write32(0xE0001FB0, 0xC5ACCE55)
        except Exception:
            pass

        t.write32(DWT_COMP0, addr & 0xFFFFFFFC)
        t.write32(DWT_MASK0, 0)          # 精确匹配 4 字节
        # FUNCTION0 = 0b01_01_01: 数据地址匹配 + 写 + 触发调试事件（进 halt）
        t.write32(DWT_FUNCTION0, (0b0101 << 0) | (0b01 << 4) | (0b01 << 8) | (1 << 24))
        print(f"# 已设置写观察点 @0x{addr:08x}，DWT_FUNCTION0=0x{t.read32(DWT_FUNCTION0):08x}")

        hits = 0
        deadline = time.time() + args.seconds
        t.resume()
        while time.time() < deadline:
            state = t.get_state()
            if state == t.State.HALTED:
                hits += 1
                pc = t.read_core_register("pc")
                lr = t.read_core_register("lr")
                sp = t.read_core_register("sp")
                print(f"命中#{hits}: PC=0x{pc:08x} LR=0x{lr:08x} SP=0x{sp:08x}")
                # 把 DWT 关掉再继续，避免反复命中同一处
                t.write32(DWT_FUNCTION0, 0)
                if hits >= 3:
                    break
                t.resume()
            time.sleep(0.02)

        t.write32(DWT_FUNCTION0, 0)
        state = t.get_state()
        print(f"# 命中 {hits} 次；结束时状态={state}")
        if state != t.State.HALTED:
            t.halt()
        print(f"#  0x{addr:08x} = 0x{t.read32(addr):08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
