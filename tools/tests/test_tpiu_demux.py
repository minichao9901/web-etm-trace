import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tpiu_demux import demux, FSYNC, HSYNC


class TpiuDemuxTests(unittest.TestCase):
    def test_restore_auxiliary_bits_and_remove_sync(self):
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        raw = b'prefix' + FSYNC + frame[:4] + HSYNC + frame[4:] + FSYNC
        stats, streams = demux(raw)
        self.assertEqual(streams[2], bytes.fromhex('ab 11 22 24 44 37 66 48 88 5b aa 6c cc 7f'))
        self.assertEqual(stats['frames'], 1)
        self.assertEqual(stats['hsyncs'], 1)
        self.assertEqual(stats['fsyncs'], 2)

    def test_odd_byte_can_belong_to_previous_source_id(self):
        frame = bytes.fromhex('05 a0 07 a1 10 22 24 44 36 66 48 88 5a aa 6c 02')
        stats, streams = demux(FSYNC + frame)
        self.assertEqual(streams[2], b'\xa0\xa1')
        self.assertEqual(streams[3][0:2], b'\x10\x22')
        self.assertEqual(stats['id_changes'], [2, 3])

    def test_sync_inside_frame_is_skipped_without_losing_data(self):
        # 格式化器在半帧处转入 idle 会插 FSYNC。数据字节恒为偶数（LSB 区分控制字节），
        # 所以 FF FF FF 7F 不可能是数据 -> 跳过 4 字节继续收集本帧，一个字节都不丢。
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        expected = demux(FSYNC + frame)[1]
        stats, streams = demux(FSYNC + frame[:4] + FSYNC + frame[4:])
        self.assertEqual(streams, expected)
        self.assertEqual(stats['frames'], 1)
        self.assertGreaterEqual(stats['mid_frame_fsyncs'], 1)
        self.assertEqual(stats['idle_resyncs'], 0)

    def test_partial_final_sync_is_preserved_as_capture_boundary(self):
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        complete, expected = demux(FSYNC + frame)
        for length in (1, 2, 3):
            stats, streams = demux(FSYNC + frame + FSYNC[:length])
            self.assertEqual(streams, expected)
            self.assertEqual(stats['frames'], complete['frames'])
            self.assertEqual(stats['errors'], 0)
            self.assertEqual(stats['trailing_sync_bytes'], length)
            self.assertEqual(stats['trailing_raw_bytes'], length)

    def test_bare_ff_pair_triggers_resync(self):
        # 完整 FSYNC 会被跳过；只有"FF FF 后面不是 7F"（构不成 FSYNC）才走重对齐，
        # 丢掉半帧并跳到下一个真 FSYNC，后面的帧照常解出。
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        expected = demux(FSYNC + frame)[1]
        stats, streams = demux(FSYNC + b'\x05\x11' + b'\xff\xff\x00\x00' + FSYNC + frame)
        self.assertGreaterEqual(stats['idle_resyncs'], 1)
        self.assertEqual(streams[2], expected[2])


if __name__ == '__main__':
    unittest.main()
