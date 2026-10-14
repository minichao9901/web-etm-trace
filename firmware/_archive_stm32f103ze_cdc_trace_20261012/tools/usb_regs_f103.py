#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
usb_regs_f103.py —— dump STM32F1 USB FS device 外设寄存器（FSDEV）

用于排查 "设备描述符请求失败"（枚举卡在 EP0）。
F1 的 USB 寄存器基址 = 0x40005C00：
  0x00 EP0R   0x04 EP1R ...   EPnR 布局：EA[3:0] STAT_TX[5:4] STAT_RX[7:6] DTOG_TX[8] ...
  0x40 CNTR   0x44 ISTR   0x48 FNR   0x4C DADDR  0x50 BTABLE
  0x400 起是 PMA（包缓冲，PMA_ACCESS=2 → 每个 16bit 占 2 字节地址）
"""
import argparse
import sys
import threading

WATCHDOG_S = 60

STAT_TX = {0: "DIS", 1: "STALL", 2: "NAK", 3: "VALID"}
STAT_RX = {0: "DIS", 1: "STALL", 2: "NAK", 3: "VALID"}


def _watchdog():
    print(f"[watchdog] 超过 {WATCHDOG_S}s，强制退出", file=sys.stderr)
    sys.exit(9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="stm32f103cb")
    ap.add_argument("--freq", type=int, default=1_000_000)
    ap.add_argument("--pma", type=int, default=64, help="dump 前 N 个 32bit PMA 字（0=不 dump）")
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

        def r32(a):
            return t.read32(a)

        base = 0x40005C00
        print("== EPnR（端点状态）==")
        for ep in range(8):
            v = r32(base + ep * 4)
            print(f"  EP{ep}R = 0x{v:08x}  EA={v & 0xF}  STAT_TX={STAT_TX[(v >> 4) & 3]}"
                  f"  STAT_RX={STAT_RX[(v >> 6) & 3]}  DTOG_TX={(v >> 8) & 1}"
                  f"  DTOG_RX={(v >> 9) & 1}  SETUP={(v >> 11) & 1}")

        cntr = r32(base + 0x40)
        istr = r32(base + 0x44)
        fnr = r32(base + 0x48)
        daddr = r32(base + 0x4C)
        btable = r32(base + 0x50)
        print("\n== 控制/状态 ==")
        print(f"  CNTR   = 0x{cntr:08x}  (低 16 位有效)")
        print(f"           FRES={(cntr >> 0) & 1} PDWN={(cntr >> 1) & 1} LP_MODE={(cntr >> 2) & 1}"
              f" FSUSP={(cntr >> 3) & 1} RESUME={(cntr >> 4) & 1}")
        print(f"           CTRM={(cntr >> 15) & 1} ERRMA={(cntr >> 14) & 1} WKUPM={(cntr >> 13) & 1}"
              f" SUSPM={(cntr >> 11) & 1} RESETM={(cntr >> 10) & 1} SOFM={(cntr >> 9) & 1}"
              f" ESOFM={(cntr >> 8) & 1}")
        print(f"  ISTR   = 0x{istr:08x}   （读 EPnR 会清对应 CTR 位）")
        print(f"           DIR={(istr >> 4) & 1} EP_ID={(istr >> 0) & 0xF}"
              f"  CTR={(istr >> 15) & 1} ERR={(istr >> 13) & 1} WKUP={(istr >> 12) & 1}"
              f" SUSP={(istr >> 11) & 1} RESET={(istr >> 10) & 1} SOF={(istr >> 9) & 1}"
              f" ESOF={(istr >> 8) & 1}")
        print(f"  FNR    = 0x{fnr:08x}  (帧号 {fnr & 0x7FF})")
        print(f"  DADDR  = 0x{daddr:08x}  (地址 {daddr & 0x7F}, EF={(daddr >> 7) & 1})")
        print(f"  BTABLE = 0x{btable:08x}")

        if args.pma:
            print(f"\n== PMA 前 {args.pma} 个 32bit 字（地址 = 基址 + 0x400 + i*4）==")
            for i in range(args.pma):
                v = r32(base + 0x400 + i * 4)
                b0, b1, b2, b3 = v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF
                asc = "".join(chr(b) if 32 <= b < 127 else "." for b in (b0, b1, b2, b3))
                print(f"  [{i:3d}] 0x{base + 0x400 + i * 4:08x}: {v:08x}  {b0:02x} {b1:02x} {b2:02x} {b3:02x}  {asc}")

        # 顺便看一下 g_trace（F103CB 固定地址）
        print("\n== g_trace @0x20004fc0 ==")
        for i in range(9):
            print(f"  [{i}] 0x{r32(0x20004FC0 + i * 4):08x}")

        if not args.pma:
            t.resume()
    return 0


if __name__ == "__main__":
    sys.exit(main())
