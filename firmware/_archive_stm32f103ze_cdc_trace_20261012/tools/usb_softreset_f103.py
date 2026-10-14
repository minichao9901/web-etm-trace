#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
usb_softreset_f103.py —— 用 SWD 给 STM32F1 的 USB 外设做一次干净的重新枚举

背景（重要）：
    STM32F1 的经典 USB FS device 外设**没有 BCDR 寄存器**（RM0008 的寄存器表只到 BTABLE 0x50），
    D+ 内部上拉是**常态接通**的，没有 DPPU 位可开关。
    所以"断开-重连"只能靠 CNTR 的 FRES/PDWN 位模拟：
        CNTR = FRES|PDWN   → 外设掉电、D+ 释放（主机看到拔出）
        CNTR = 0           → 上电、D+ 接上（主机看到插入）
        CNTR = 中断掩码    → 正常枚举
    本脚本就是按这个序列做一次"软拔插"，用来判定板子/固件是否能枚举。

用法：
  python usb_softreset_f103.py --target stm32f103cb --gap-ms 800
"""
import argparse
import sys
import threading
import time

WATCHDOG_S = 90
USB = 0x40005C00
CNTR, ISTR, FNR, DADDR, BTABLE = (USB + 0x40, USB + 0x44, USB + 0x48, USB + 0x4C, USB + 0x50)
FRES, PDWN = 1 << 0, 1 << 1
MASKM = 0xBD00  # CTRM|WKUPM|SUSPM|ERRM|ESOFM|RESETM


def _wd():
    time.sleep(WATCHDOG_S)
    print("[watchdog] 超时", file=sys.stderr)
    sys.exit(9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="stm32f103cb")
    ap.add_argument("--gap-ms", type=int, default=800)
    ap.add_argument("--freq", type=int, default=1_000_000)
    args = ap.parse_args()
    threading.Thread(target=_wd, daemon=True).start()

    from pyocd.core.helpers import ConnectHelper
    s = ConnectHelper.session_with_chosen_probe(
        target_override=args.target, frequency=args.freq, options={"auto_unlock": False})
    if s is None:
        print("连不上探针", file=sys.stderr)
        return 2

    with s:
        t = s.target
        if t.get_state() != t.State.HALTED:
            t.halt()

        def r16(a):
            return t.read16(a)

        def w16(a, v):
            t.write16(a, v & 0xFFFF)

        print(f"初始: CNTR=0x{r16(CNTR):04x} ISTR=0x{r16(ISTR):04x} "
              f"DADDR=0x{r16(DADDR):04x} BTABLE=0x{r16(BTABLE):04x}")

        # 1) 掉电：D+ 释放，主机看到拔出
        w16(CNTR, FRES | PDWN)
        time.sleep(args.gap_ms / 1000.0)
        print(f"掉电保持 {args.gap_ms} ms")

        # 2) 上电 + 干净复位序列
        w16(CNTR, FRES)          # 强制复位
        w16(CNTR, 0)             # 复位释放 -> 上电 -> D+ 接上
        w16(ISTR, 0)             # 清挂起
        w16(DADDR, 0)
        w16(BTABLE, 0)
        w16(CNTR, MASKM)         # 打开需要的中断
        print(f"上电后: CNTR=0x{r16(CNTR):04x} ISTR=0x{r16(ISTR):04x} DADDR=0x{r16(DADDR):04x}")

        t.resume()
        print("已 resume —— 主机现在应该开始枚举")
    return 0


if __name__ == "__main__":
    sys.exit(main())
