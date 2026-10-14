#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
usb_reg_scan_f103.py —— 扫描 STM32F1 USB 寄存器区，确认 BCDR 到底在哪个偏移、能不能写

背景：stm32f103xb.h 里 ST 自己的 USB_TypeDef **不含 BCDR**（最高只到 BTABLE @0x50），
而 CherryUSB 的 usb_fsdev_reg.h 里那份 USB_TypeDef 有 `BCDR @0x58`。
本脚本直接对硬件做写-回读实验来定论。
"""
import sys
import threading
import time

WATCHDOG_S = 90
BASE = 0x40005C00


def _wd():
    time.sleep(WATCHDOG_S)
    print("[watchdog] 超时", file=sys.stderr)
    sys.exit(9)


def main():
    threading.Thread(target=_wd, daemon=True).start()
    from pyocd.core.helpers import ConnectHelper

    s = ConnectHelper.session_with_chosen_probe(
        target_override="stm32f103cb", frequency=1_000_000, options={"auto_unlock": False})
    if s is None:
        print("连不上探针", file=sys.stderr)
        return 2
    with s:
        t = s.target
        if t.get_state() != t.State.HALTED:
            t.halt()

        print("=== 0x40005C00 + 0x40..0x60 当前值 ===")
        for off in range(0x40, 0x62, 2):
            print(f"  +0x{off:02x} = 0x{t.read16(BASE + off):04x}")

        print("\n=== 写-回读实验（写 0x8000，看能不能存住）===")
        for off in (0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C):
            old = t.read16(BASE + off)
            t.write16(BASE + off, 0x8000)
            new = t.read16(BASE + off)
            mark = "  <== 可写!" if new != old else ""
            print(f"  +0x{off:02x}: old=0x{old:04x} -> 写 0x8000 -> 读 0x{new:04x}{mark}")

        apb1 = t.read32(0x4002101C)
        print(f"\nAPB1ENR = 0x{apb1:08x}   USBEN(bit23) = {(apb1 >> 23) & 1}")

        print("\n=== 对照：CNTR(+0x40) 与 ISTR(+0x44) 是否可写 ===")
        for off in (0x40, 0x44):
            old = t.read16(BASE + off)
            t.write16(BASE + off, old)
            print(f"  +0x{off:02x} 回写同值后 = 0x{t.read16(BASE + off):04x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
