#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
min_probe.py —— 通过 CMSIS-DAP 探针读目标 RAM/寄存器（带超时与看门狗）

为什么不用 `pyocd cmd`：它的命令行输出把"读长度"当字节数、且反复重连探针时会返回
上一次的陈旧数据，容易把人带偏。这里直接用 pyocd 的 Python API，一次连上、halt、
读、resume，结果最可信。

用法：
  python min_probe.py --target stm32f103cb --read 0x20004fc0:36 --read 0x20000010:16
  python min_probe.py --target stm32f103cb --no-halt --read 0x20004fc0:36
"""
import argparse
import sys
import threading

WATCHDOG_S = 60


def _watchdog():
    print(f"[watchdog] 超过 {WATCHDOG_S}s，强制退出", file=sys.stderr)
    sys.exit(9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--read", action="append", default=[],
                    help="addr:len，可多次；addr/len 支持 0x 前缀")
    ap.add_argument("--no-halt", action="store_true",
                    help="不 halt（读运行中的内存，结果可能不一致）")
    ap.add_argument("--resume", action="store_true",
                    help="读完后 resume（默认保持 halt 状态不变）")
    ap.add_argument("--freq", type=int, default=1_000_000)
    args = ap.parse_args()

    threading.Thread(target=_watchdog, daemon=True).start()

    import pyocd
    from pyocd.core.helpers import ConnectHelper

    session = ConnectHelper.session_with_chosen_probe(
        target_override=args.target,
        frequency=args.freq,
        options={"auto_unlock": False},
    )
    if session is None:
        print("连接探针失败", file=sys.stderr)
        return 2

    with session:
        target = session.target
        was_halted = target.get_state() == target.State.HALTED
        if not args.no_halt and not was_halted:
            target.halt()
        state = "HALTED" if target.get_state() == target.State.HALTED else "RUNNING"
        print(f"# target={args.target} state={state}")

        for spec in args.read:
            addr_s, _, len_s = spec.partition(":")
            addr = int(addr_s, 0)
            length = int(len_s or "4", 0)
            data = target.read_memory_block8(addr, length)
            words = []
            for i in range(0, len(data) - 3, 4):
                w = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16) | (data[i + 3] << 24)
                words.append(f"{w:08x}")
            print(f"{addr:08x}: " + " ".join(words))
            printable = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
            print(f"          ascii: {printable}")

        if args.resume and not was_halted:
            target.resume()
            print("# resumed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
