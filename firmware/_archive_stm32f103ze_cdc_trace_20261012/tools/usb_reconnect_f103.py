#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
usb_reconnect_f103.py —— 用 SWD 手动"拔插" STM32F1 的 USB，观察主机枚举

为什么需要它：排查"设备描述符请求失败"时，要把"固件侧 USB 状态机"和"主机侧枚举窗口"
分开看。这个脚本只碰 3 个寄存器（CNTR/ISTR/BCDR），按正确顺序把 D+ 上拉打开，
然后可以选择保持运行、让主机重新枚举。

用法：
  python usb_reconnect_f103.py --target stm32f103cb            # 断 800ms 再连，然后 resume
  python usb_reconnect_f103.py --target stm32f103cb --watch 10 # 连上后盯 10 秒寄存器变化
"""
import argparse
import sys
import threading
import time

WATCHDOG_S = 90

USB_BASE = 0x40005C00
CNTR = USB_BASE + 0x40
ISTR = USB_BASE + 0x44
FNR = USB_BASE + 0x48
DADDR = USB_BASE + 0x4C
BTABLE = USB_BASE + 0x50
BCDR = USB_BASE + 0x58

# CNTR 位
FRES, PDWN, LP_MODE, FSUSP, RESUME = 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4
CTRM, ERRMA, WKUPM, SUSPM, RESETM, SOFM, ESOFM = (
    1 << 15, 1 << 14, 1 << 13, 1 << 11, 1 << 10, 1 << 9, 1 << 8)
DPPU = 1 << 15  # BCDR 的 D+ 上拉


def _watchdog():
    print(f"[watchdog] 超过 {WATCHDOG_S}s，强制退出", file=sys.stderr)
    sys.exit(9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="stm32f103cb")
    ap.add_argument("--freq", type=int, default=1_000_000)
    ap.add_argument("--hold-ms", type=int, default=800)
    ap.add_argument("--watch", type=float, default=0, help="连上后盯多少秒")
    ap.add_argument("--no-resume", action="store_true")
    args = ap.parse_args()

    threading.Thread(target=_watchdog, daemon=True).start()

    from pyocd.core.helpers import ConnectHelper

    session = ConnectHelper.session_with_chosen_probe(
        target_override=args.target, frequency=args.freq, options={"auto_unlock": False}
    )
    if session is None:
        print("连接探针失败", file=sys.stderr)
        return 2

    with session:
        t = session.target
        if t.get_state() != t.State.HALTED:
            t.halt()

        def r16(a):
            return t.read16(a)

        def w16(a, v):
            t.write16(a, v & 0xFFFF)

        print(f"断开前: CNTR=0x{r16(CNTR):04x} BCDR=0x{r16(BCDR):04x} "
              f"ISTR=0x{r16(ISTR):04x} DADDR=0x{r16(DADDR):04x} BTABLE=0x{r16(BTABLE):04x}")

        # ---- 1) 断开 ----
        w16(BCDR, r16(BCDR) & ~DPPU)
        w16(CNTR, FRES)
        time.sleep(args.hold_ms / 1000.0)
        print(f"断开保持 {args.hold_ms} ms")

        # ---- 2) 重新连接（注意顺序：先复位外设、清挂起、开中断掩码，最后才上拉）----
        w16(CNTR, FRES)
        w16(ISTR, 0)
        w16(DADDR, 0)
        w16(BTABLE, 0)
        w16(CNTR, CTRM | WKUPM | SUSPM | ERRMA | ESOFM | RESETM)
        w16(BCDR, r16(BCDR) | DPPU)

        print(f"重连后: CNTR=0x{r16(CNTR):04x} BCDR=0x{r16(BCDR):04x} "
              f"ISTR=0x{r16(ISTR):04x} DADDR=0x{r16(DADDR):04x}")

        if not args.no_resume:
            t.resume()
            print("已 resume（目标在跑）")

        if args.watch > 0:
            print(f"\n盯 {args.watch}s（每秒采样一次；注意 halt 会把 CPU 停住）:")
            deadline = time.time() + args.watch
            while time.time() < deadline:
                time.sleep(1.0)
                print(f"  t={time.time() - (deadline - args.watch):4.1f}s  "
                      f"CNTR=0x{r16(CNTR):04x} ISTR=0x{r16(ISTR):04x} "
                      f"FNR=0x{r16(FNR):04x} DADDR=0x{r16(DADDR):04x} EP0R=0x{t.read32(USB_BASE):08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
