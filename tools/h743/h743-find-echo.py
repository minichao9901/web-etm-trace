"""在同一次 LA 采集里扫时间窗，找出含 CDC 回显路径的那一段。"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(r'E:\web-etm-trace')
sys.path.insert(0, str(ROOT / 'tools'))
from decode_trace import decode_capture          # noqa: E402

rec = Path(sys.argv[1])
span = float(sys.argv[2]) if len(sys.argv) > 2 else 0.004
starts = [float(x) for x in (sys.argv[3].split(',') if len(sys.argv) > 3
                             else '0.48,0.50,0.52,0.54,0.56,0.58'.split(','))]
meta = json.loads((rec / 'meta.json').read_text(encoding='utf-8'))
config = {k: int(v) for k, v in meta['etm'].items()}
markers = ('usbd_cdc_acm_bulk_out', 'usbd_cdc_acm_bulk_in', 'cdc_write', 'handle_command',
           'trace_log_printf', 'SEGGER_RTT_Write')
for t0 in starts:
    tag = f'{t0:g}-{t0 + span:g}'.replace('.', 'p')
    csv = rec / f'span-{tag}.csv'
    if not csv.exists():
        subprocess.run([sys.executable, '-u', '-X', 'utf8', str(ROOT / 'tools/la/kingst_la.py'),
                        'export', '--out', str(csv), '--channels', '0', '1', '2', '3', '4',
                        '--time-span', str(t0), str(t0 + span), '--no-kvdat'],
                       cwd=str(ROOT), check=True, timeout=900)
    try:
        r = decode_capture(csv, rec / 'firmware.elf', rec / f'span-{tag}', config=config)
    except Exception as exc:                       # noqa: BLE001
        print(f'{t0:.3f}-{t0+span:.3f}  解码失败: {str(exc)[:90]}')
        continue
    hits = {k: v for k, v in r['summary']['functions'].items() if k in markers}
    print(f"{t0:.3f}-{t0+span:.3f}  instr={r['summary']['instructions']:>7}  "
          f"issues={r['summary']['packetIssues']:>3}  命中={hits}")
    if any(k.startswith('usbd_cdc_acm_bulk') or k == 'cdc_write' for k in hits):
        print('  ^^ 这一段就是回显路径')
        break
