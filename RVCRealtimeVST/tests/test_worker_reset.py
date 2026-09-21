"""Test the production reset/IPC dispatcher without importing Torch or NumPy."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

path = Path(__file__).resolve().parents[1] / "worker" / "rvc_worker.py"
spec = importlib.util.spec_from_file_location("worker_under_test", path)
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class Buffer:
    def __init__(self):
        self.value = 7

    def zero_(self):
        self.value = 0

    def fill(self, value):
        self.value = value


class ResetTests(unittest.TestCase):
    def make_engine(self):
        engine = worker.RVCStreamEngine.__new__(worker.RVCStreamEngine)
        for name in ("input_wav", "input_wav_res", "rms_buffer", "sola_buffer"):
            setattr(engine, name, Buffer())
        engine.rvc = SimpleNamespace(cache_pitch=Buffer(), cache_pitchf=Buffer())
        engine.last_pitch = 12
        engine.last_formant = 0
        engine.last_index_rate = 0.5
        return engine

    def test_reset_all_history_in_place(self):
        engine = self.make_engine()
        buffers = [engine.input_wav, engine.input_wav_res, engine.rms_buffer,
                   engine.sola_buffer, engine.rvc.cache_pitch, engine.rvc.cache_pitchf]
        engine.reset_stream()
        self.assertTrue(all(b.value == 0 for b in buffers))
        self.assertIs(engine.input_wav, buffers[0])
        self.assertIs(engine.rvc.cache_pitch, buffers[4])
        self.assertIsNone(engine.last_pitch)
        self.assertIsNone(engine.last_formant)
        self.assertIsNone(engine.last_index_rate)

    def test_reset_precedes_process_and_flag_can_clear(self):
        engine = self.make_engine()
        shared = bytearray(worker.HEADER_BYTES)
        worker.write_value(shared, worker.RESET_OFFSET, "I", 1)
        worker.write_value(shared, 32, "f", 12)
        audio = object()

        def process(samples, pitch, *args):
            self.assertIs(samples, audio)
            self.assertEqual(pitch, 12)
            self.assertEqual(engine.sola_buffer.value, 0)
            engine.sola_buffer.value = 9
            return samples

        engine.process = process
        self.assertIs(engine.process_request(shared, audio), audio)
        worker.write_value(shared, worker.RESET_OFFSET, "I", 0)
        engine.process = lambda *args: engine.sola_buffer.value
        self.assertEqual(engine.process_request(shared, audio), 9)

    def test_missing_optional_pitch_caches(self):
        engine = self.make_engine()
        engine.rvc = SimpleNamespace()
        engine.reset_stream()


if __name__ == "__main__":
    unittest.main()
