"""Readiness, precise process teardown, and failure-report regressions."""
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('runtime', Path(__file__).with_name('check-linux-appimage.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class RuntimeLifecycleTest(unittest.TestCase):
    def test_ready_requires_project_and_no_background_operations(self):
        ready = {'activeProjectId': 'project', 'activeJobs': 0, 'queuedJobs': 0,
                 'browserOperations': False, 'queuedBrowserOperations': 0}
        self.assertTrue(checker.idle_status(ready))
        for key in ready:
            value = '' if key == 'activeProjectId' else 1
            self.assertFalse(checker.idle_status({**ready, key: value}), key)

    def test_peer_is_pinned_even_when_initial_rpc_times_out(self):
        sock = Mock()
        sock.getsockopt.return_value = struct.pack('3i', 456, os.getuid(), 0)
        sock.recv.side_effect = TimeoutError('initialization still busy')
        owned = {}
        handle = Mock(pid=456)
        with patch.object(checker.socket, 'socket', return_value=sock), \
             patch.object(checker.os, 'readlink', return_value='/app/usr/bin/cybersnapper-agent'), \
             patch.object(checker, 'OwnedProcess', return_value=handle) as pin:
            rpc = checker.AgentRpc(Path('/isolated/agent.sock'), owned)
            pin.assert_called_once_with(456)
            self.assertIs(owned[456], handle)
            with self.assertRaises(TimeoutError):
                rpc.call('agent.status')
            rpc.close()
            handle.close.assert_not_called()  # Teardown still owns this identity.

    def test_rpc_uses_v1_framing_and_ignores_events(self):
        def frame(value):
            body = json.dumps(value).encode()
            return struct.pack('>I', len(body)) + body
        stream = io.BytesIO(frame({'v': 1, 'event': 'queue.changed'}) +
                            frame({'v': 1, 'id': 'runtime-check-1', 'result': {'ok': True}}))
        rpc = checker.AgentRpc.__new__(checker.AgentRpc)
        rpc.socket = Mock()
        rpc.socket.recv.side_effect = lambda length: stream.read(min(length, 3))
        rpc.sequence = 0
        self.assertEqual(rpc.call('agent.status'), {'ok': True})
        request = rpc.socket.sendall.call_args.args[0]
        self.assertEqual(struct.unpack('>I', request[:4])[0], len(request) - 4)
        self.assertEqual(json.loads(request[4:])['v'], 1)

    def test_rpc_rejects_invalid_frame_and_closed_connection(self):
        for data in (b'', struct.pack('>I', 17 * 1024 * 1024)):
            rpc = checker.AgentRpc.__new__(checker.AgentRpc)
            rpc.socket = Mock()
            rpc.socket.recv.side_effect = io.BytesIO(data).read
            rpc.sequence = 0
            with self.assertRaises(ValueError):
                rpc.call('agent.status')

    def test_graceful_exit_sends_no_signal(self):
        handle = Mock()
        handle.wait.return_value = True
        self.assertFalse(checker.stop_owned(handle))
        handle.send.assert_not_called()

    def test_delayed_exit_is_escalation(self):
        handle = Mock()
        handle.wait.side_effect = [False, True]
        self.assertTrue(checker.stop_owned(handle))
        handle.send.assert_called_once_with(signal.SIGTERM)

    def test_stuck_process_fails_after_bounded_kill(self):
        handle = Mock(pid=123)
        handle.wait.return_value = False
        with self.assertRaisesRegex(ValueError, 'did not exit'):
            checker.stop_owned(handle)
        self.assertEqual([call.args[0] for call in handle.send.call_args_list],
                         [signal.SIGTERM, signal.SIGKILL])

    def test_teardown_stops_agent_before_gui_and_rejects_escalation(self):
        for escalated in (False, True):
            with self.subTest(escalated=escalated), tempfile.TemporaryDirectory() as directory:
                evidence = Path(directory)
                process = Mock(pid=234)
                process.poll.return_value = None
                agent = Mock(pid=345, start_ticks=678)
                agent.wait.return_value = True
                rpc = Mock(process=agent)
                order = []
                rpc.call.side_effect = lambda *a, **k: order.append('agent')
                agent.wait.return_value = False
                with patch.object(checker, 'descendants', return_value=[]), \
                     patch.object(checker, 'stop_owned', return_value=escalated), \
                     patch.object(checker.os, 'killpg', side_effect=lambda *a: order.append('gui')):
                    # Exact process is known to have exited by final verification.
                    agent.wait.side_effect = [False, True]
                    if escalated:
                        with self.assertRaisesRegex(ValueError, 'did not exit cleanly'):
                            checker.teardown(process, None, rpc, {345: agent}, evidence)
                    else:
                        checker.teardown(process, None, rpc, {345: agent}, evidence)
                self.assertEqual(order, ['agent', 'gui'])
                self.assertEqual(json.loads((evidence / 'teardown.json').read_text())['passed'], not escalated)

    def test_teardown_failure_still_cleans_every_identity_and_writes_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            process = Mock(pid=222)
            process.poll.return_value = None
            process.wait.side_effect = RuntimeError('group wait failed')
            agent = Mock(pid=333, start_ticks=123)
            child = Mock(pid=444, start_ticks=124)
            gui = Mock(pid=555, start_ticks=125)
            for handle in (agent, child, gui):
                handle.wait.return_value = True
            with patch.object(checker, 'descendants', return_value=[child]), \
                 patch.object(checker.os, 'killpg'), \
                 patch.object(checker, 'stop_owned', side_effect=[RuntimeError('agent stop failed'), False,
                                                                RuntimeError('GUI stop failed')]) as stop:
                with self.assertRaisesRegex(ValueError, 'did not exit cleanly'):
                    checker.teardown(process, gui, None, {333: agent}, Path(directory))
            self.assertEqual([call.args[0].pid for call in stop.call_args_list], [333, 444, 555])
            for handle in (agent, child, gui):
                handle.close.assert_called_once()
            result = json.loads((Path(directory) / 'teardown.json').read_text())
            self.assertFalse(result['passed'])
            self.assertEqual(len(result['errors']), 3)

    def test_temp_cleanup_failure_cannot_keep_success_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / 'test.AppImage'
            image.write_bytes(b'fixture')
            stage = root / 'stage'
            stage.mkdir()
            evidence = root / 'evidence'
            temporary = Mock()
            temporary.__enter__ = Mock(return_value=str(stage))
            temporary.__exit__ = Mock(side_effect=OSError('cleanup failure'))
            with patch.object(checker.tempfile, 'TemporaryDirectory', return_value=temporary), \
                 patch.object(checker.subprocess, 'run'), \
                 patch.object(checker, 'inspect_elf', return_value=108), \
                 patch.object(checker, 'launch'):
                with self.assertRaisesRegex(OSError, 'cleanup failure'):
                    checker.verify(image, evidence)
            result = json.loads((evidence / 'result.json').read_text())
            self.assertFalse(result['passed'])
            self.assertEqual(result['error'], 'cleanup failure')

    @unittest.skipUnless(hasattr(os, 'pidfd_open') and hasattr(signal, 'pidfd_send_signal'), 'Linux pidfds required')
    def test_real_owned_process_identity_and_wait(self):
        process = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
        handle = checker.OwnedProcess(process.pid)
        try:
            self.assertGreater(handle.start_ticks, 0)
            self.assertFalse(handle.wait(0))
            handle.send(signal.SIGTERM)
            self.assertTrue(handle.wait(5))
            process.wait(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            handle.close()


if __name__ == '__main__':
    unittest.main()
