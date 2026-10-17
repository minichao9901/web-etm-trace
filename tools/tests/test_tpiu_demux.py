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

    def test_full_sync_in_middle_of_frame_drops_partial_frame(self):
        # 帧中间的 FSYNC = 格式化器在半帧处转入 idle（CPU==TRACECLK 的稀疏 trace 常态）。
        # 新语义：丢掉这个半帧并重新对齐，绝不让它混进可信流，但也不再让整条流失败。
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        stats, streams = demux(FSYNC + b'\x05\x11' + FSYNC + frame)
        self.assertEqual(stats['idle_resyncs'], 1)
        self.assertEqual(stats['dropped_partial_frame_bytes'], 2)
        self.assertEqual(stats['frames'], 1)
        self.assertEqual(streams[2], bytes.fromhex('ab 11 22 24 44 37 66 48 88 5b aa 6c cc 7f'))

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

    def test_broken_prefix_is_resynced_and_later_frame_still_decoded(self):
        frame = bytes.fromhex('05 ab 10 22 24 44 36 66 48 88 5a aa 6c cc 7e aa')
        expected = bytes.fromhex('ab 11 22 24 44 37 66 48 88 5b aa 6c cc 7f')
        for broken in (b'\x05\x11', b'\x00\x00'):
            stats, streams = demux(FSYNC + broken + FSYNC + frame)
            self.assertGreaterEqual(stats['idle_resyncs'], 1)
            self.assertEqual(stats['frames'], 1)
            self.assertEqual(streams[2], expected)


if __name__ == '__main__':
    unittest.main()
