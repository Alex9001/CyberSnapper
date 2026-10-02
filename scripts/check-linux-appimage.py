#!/usr/bin/env python3
"""Audit packaged ELF compatibility and capture CyberSnapper's visible X11 startup.

Run under an isolated xvfb-run display, without a Qt SDK installed:
  xvfb-run -a python3 scripts/check-linux-appimage.py IMAGE --evidence DIRECTORY
Needs binutils, libc-bin, xdotool, ImageMagick, Xvfb, and the runtime libraries.
No domain lookup is requested. Visibility is evidence of startup, not a complete
interactive GUI test or an event-loop responsiveness assertion.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time


LIMITS = {'GLIBC': (2, 35), 'GLIBCXX': (3, 4, 30), 'CXXABI': (1, 3, 13)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def output(*args, **kwargs):
    return subprocess.check_output(args, text=True, timeout=30, stderr=subprocess.STDOUT, **kwargs)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def is_main_executable(file, dynamic):
    # Some shared libraries (for example libc) can also carry an interpreter.
    if '(SONAME)' in dynamic:
        return False
    headers = output('readelf', '--file-header', '--program-headers', str(file))
    return bool(re.search(r'^\s*Type:\s+EXEC\b', headers, re.MULTILINE) or
                re.search(r'^\s*INTERP\s', headers, re.MULTILINE) or
                re.search(r'\(FLAGS_1\).*\bPIE\b', dynamic))


def inspect_elf(root, report):
    inspected = []
    environment = {**os.environ, 'LD_LIBRARY_PATH': str(root / 'usr/lib')}
    for file in sorted(root.rglob('*')):
        if not file.is_file():
            continue
        with file.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        resolved_file = file.resolve()
        require(resolved_file.is_relative_to(root.resolve()), f'ELF symlink escapes AppDir: {file}')
        dynamic = output('readelf', '-d', str(resolved_file))
        executable = is_main_executable(resolved_file, dynamic)
        # The kernel resolves executable symlinks (including AppRun.wrapped)
        # before the loader expands $ORIGIN. Shared libraries instead retain
        # their load pathname, so a DSO alias must be audited from its own
        # directory. ldd must use that same origin to avoid false resolutions.
        load_file = resolved_file if executable else file
        record = {'path': str(file.relative_to(root)),
                  'target': str(resolved_file.relative_to(root.resolve())),
                  'main_executable': executable, 'dynamic': '(NEEDED)' in dynamic}
        # Static Go executables have no dynamic symbol table; their absence is
        # valid, while every dynamically linked executable/library is audited.
        if '(NEEDED)' in dynamic or 'Dynamic section at offset' in dynamic:
            symbols = output('objdump', '-T', str(resolved_file))
            for line in symbols.splitlines():
                if '*UND*' not in line:
                    continue
                for family, version in re.findall(r'\b(GLIBCXX|GLIBC|CXXABI)_([0-9.]+)', line):
                    require(tuple(map(int, version.split('.'))) <= LIMITS[family],
                            f'Ubuntu 22.04 symbol limit exceeded in {file}: {line}')
        for line in dynamic.splitlines():
            if '(RPATH)' in line or '(RUNPATH)' in line:
                match = re.search(r'\[(.*)\]', line)
                require(match is not None, f'Unrecognized runtime path: {file}: {line}')
                for entry in match.group(1).split(':'):
                    require(entry == '$ORIGIN' or entry.startswith('$ORIGIN/') or
                            entry == '${ORIGIN}' or entry.startswith('${ORIGIN}/'),
                            f'Non-relocatable or empty runtime path in {file}: {line}')
                    relative = entry.replace('${ORIGIN}', '.', 1).replace('$ORIGIN', '.', 1)
                    require((load_file.parent / relative).resolve().is_relative_to(root.resolve()),
                            f'Runtime path escapes AppDir in {file}: {line}')
        if record['dynamic']:
            libraries = output('ldd', str(load_file), env=environment)
            require('not found' not in libraries, f'Unresolved dependency in {file}:\n{libraries}')
            record['dependencies'] = libraries
            for path in re.findall(r'=> (/\S+)', libraries):
                resolved = Path(path).resolve()
                require(resolved.is_relative_to(root.resolve()) or
                        str(resolved).startswith(('/usr/lib/', '/lib/')),
                        f'Dependency outside package/system library roots: {file}: {resolved}')
        inspected.append(record)
        # Retain partial evidence even if a later ELF fails.
        report.write_text(json.dumps(inspected, indent=2) + '\n')
    require(len(inspected) > 10, f'Unexpectedly few packaged ELF files: {len(inspected)}')
    return len(inspected)


def visible_window(process, environment):
    found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', '^CyberSnapper$'],
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=environment, timeout=5)
    if found.returncode != 0:
        return None
    for window in found.stdout.split():
        try:
            pid = int(output('xdotool', 'getwindowpid', window, env=environment).strip())
            if os.getpgid(pid) != process.pid:
                continue
            geometry = output('xdotool', 'getwindowgeometry', '--shell', window, env=environment)
            fields = dict(line.split('=', 1) for line in geometry.splitlines() if '=' in line)
            if int(fields.get('WIDTH', '0')) >= 100 and int(fields.get('HEIGHT', '0')) >= 100:
                return {'window_id': window, 'pid': pid, 'geometry': fields}
        except (ValueError, ProcessLookupError, subprocess.CalledProcessError):
            continue
    return None


class OwnedProcess:
    """Pin one process identity so a recycled PID can never be signalled."""
    def __init__(self, pid):
        self.pid = pid
        self.fd = os.pidfd_open(pid)
        try:
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
            self.start_ticks = int(fields[19])
        except Exception:
            os.close(self.fd)
            raise

    def wait(self, timeout):
        poller = select.poll()
        poller.register(self.fd, select.POLLIN)
        return bool(poller.poll(max(0, int(timeout * 1000))))

    def send(self, sig):
        if not self.wait(0):
            try:
                signal.pidfd_send_signal(self.fd, sig)
            except ProcessLookupError:
                pass

    def close(self):
        os.close(self.fd)


def descendants(process):
    """Pin children while their known parent is still alive."""
    result = []
    if process.wait(0):
        return result
    try:
        children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
    except FileNotFoundError:
        return result
    for pid in children:
        child = None
        try:
            child = OwnedProcess(int(pid))
            status = Path(f'/proc/{pid}/status').read_text()
            if not re.search(rf'^PPid:\s+{process.pid}$', status, re.MULTILINE):
                child.close()
                continue
            result.append(child)
            result.extend(descendants(child))
        except ProcessLookupError:
            continue
        except FileNotFoundError:
            if child is not None:
                child.close()
    return result


class AgentRpc:
    def __init__(self, server, owned):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.socket.settimeout(2)
            self.socket.connect(str(server))
            pid, uid, _ = struct.unpack('3i', self.socket.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            require(uid == os.getuid(), 'Isolated socket belongs to another user')
            require(Path(os.readlink(f'/proc/{pid}/exe')).name == 'cybersnapper-agent',
                    'Isolated socket is not served by the packaged agent')
            # Pin the peer before waiting for initialization or an RPC reply.
            if pid not in owned:
                owned[pid] = OwnedProcess(pid)
            self.process = owned[pid]
            self.sequence = 0
        except Exception:
            self.socket.close()
            raise

    def read_exact(self, length):
        data = bytearray()
        while len(data) < length:
            chunk = self.socket.recv(length - len(data))
            require(bool(chunk), 'Agent closed its RPC connection')
            data.extend(chunk)
        return data

    def call(self, method, params=None, timeout=30):
        self.socket.settimeout(timeout)
        self.sequence += 1
        request_id = f'runtime-check-{self.sequence}'
        body = json.dumps({'v': 1, 'id': request_id, 'method': method, 'params': params or {}}).encode()
        self.socket.sendall(struct.pack('>I', len(body)) + body)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.socket.settimeout(max(0.01, deadline - time.monotonic()))
            length = struct.unpack('>I', self.read_exact(4))[0]
            require(0 < length <= 16 * 1024 * 1024, 'Invalid agent RPC frame length')
            response = json.loads(self.read_exact(length))
            if response.get('id') != request_id:
                continue
            require(response.get('v') == 1 and 'error' not in response, f'Agent RPC failed: {response}')
            require(isinstance(response.get('result'), dict), 'Agent RPC result is missing')
            return response['result']
        raise TimeoutError(f'Agent RPC timed out: {method}')

    def close(self):
        self.socket.close()


def idle_status(status):
    return bool(status.get('activeProjectId')) and not any(status.get(key) for key in
        ('activeJobs', 'queuedJobs', 'browserOperations', 'queuedBrowserOperations'))


def stop_owned(process, graceful_timeout=10):
    """Wait for graceful exit, then clean up strictly and report escalation."""
    if process.wait(graceful_timeout):
        return False
    process.send(signal.SIGTERM)
    if not process.wait(5):
        process.send(signal.SIGKILL)
        require(process.wait(5), f'Owned process did not exit: {process.pid}')
    return True


def teardown(process, gui, rpc, agents, evidence):
    children = []
    report = {'passed': False, 'agents': [], 'escalated': [], 'errors': []}

    def failed(stage, error):
        report['errors'].append(f'{stage}: {error}')

    try:
        for agent in agents.values():
            report['agents'].append({'pid': agent.pid, 'start_ticks': agent.start_ticks})
            try:
                children.extend(descendants(agent))
            except Exception as error:
                failed(f'Inspect descendants of {agent.pid}', error)
        if rpc is not None:
            try:
                if not rpc.process.wait(0):
                    rpc.call('agent.stop', {'force': True}, timeout=15)
            except (OSError, ValueError) as error:
                # Exit may precede response flush; the exact pidfd must still
                # confirm exit below, otherwise cleanup fails the gate.
                report['stop_response_error'] = str(error)
        for handle in [*agents.values(), *children]:
            try:
                if stop_owned(handle, 15 if handle in agents.values() else 2):
                    report['escalated'].append(handle.pid)
            except Exception as error:
                # One broken process must not skip the remaining identities.
                failed(f'Stop owned process {handle.pid}', error)
    finally:
        # Keep the extraction tree alive until agent cleanup has been tried,
        # then always attempt GUI cleanup, evidence writing and every fd close.
        try:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
                report['escalated'].append(process.pid)
        except Exception as error:
            failed('Stop AppImage process group', error)
        if gui is not None:
            try:
                if stop_owned(gui, 5):
                    report['escalated'].append(gui.pid)
            except Exception as error:
                failed(f'Stop GUI process {gui.pid}', error)
        handles = [*children, *agents.values(), *([gui] if gui is not None else [])]
        for handle in handles:
            try:
                if not handle.wait(0):
                    failed(f'Owned process {handle.pid}', 'still alive after teardown')
            except Exception as error:
                failed(f'Check owned process {handle.pid}', error)
        if rpc is not None:
            try:
                rpc.close()
            except Exception as error:
                failed('Close agent RPC', error)
        for handle in handles:
            try:
                handle.close()
            except Exception as error:
                failed(f'Close process handle {handle.pid}', error)
        report['passed'] = not report['escalated'] and not report['errors']
        (evidence / 'teardown.json').write_text(json.dumps(report, indent=2) + '\n')
    require(report['passed'], f'Runtime teardown did not exit cleanly: {report}')


def launch(appimage, state, evidence, appdir):
    require(bool(os.environ.get('DISPLAY')), 'DISPLAY is required; run with xvfb-run -a')
    environment = {**os.environ, 'QT_QPA_PLATFORM': 'xcb', 'APPIMAGE_EXTRACT_AND_RUN': '1',
                   'HOME': str(state), 'XDG_CONFIG_HOME': str(state / 'config'),
                   'XDG_DATA_HOME': str(state / 'data'), 'XDG_CACHE_HOME': str(state / 'cache'),
                   'XDG_RUNTIME_DIR': str(state / 'runtime'),
                   'CYBERSNAPPER_DEFAULT_PROJECT': str(state / 'project'),
                   'CYBERSNAPPER_AGENT_SERVER': str(state / 'agent.sock')}
    (state / 'runtime').mkdir(mode=0o700)
    settings = state / 'config/CyberBrand/CyberSnapper.conf'
    settings.parent.mkdir(parents=True)
    settings.write_text('[onboarding]\ncompleted=true\n')
    (evidence / 'fixture.json').write_text(json.dumps({
        'onboarding_completed': True, 'scope': 'isolated test home only',
        'reason': 'Keep the main window unobscured by the separate first-run wizard',
        'cold_appimage_launch': True, 'project_created_by_packaged_agent': True}, indent=2) + '\n')
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD',
                'CYBERSNAPPER_AGENT', 'CYBERSNAPPER_WORKER_ENTRY', 'CYBERSNAPPER_NODE',
                'CYBERSNAPPER_BROWSER_CACHE', 'CYBERSNAPPER_UI_SCREENSHOT',
                'CYBERSNAPPER_UI_SCENE', 'CYBERSNAPPER_UI_SCREENSHOT_DELAY'):
        environment.pop(key, None)
    agents = {}
    rpc = None
    gui = None
    with (evidence / 'startup.log').open('w') as log:
        process = subprocess.Popen([str(appimage)], env=environment, stdout=log, stderr=log,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + 120
            stable = 0
            last_error = ''
            while time.monotonic() < deadline:
                require(process.poll() is None, f'AppImage exited with {process.returncode}; see startup.log')
                window = visible_window(process, environment)
                if window and gui is None:
                    gui = OwnedProcess(window['pid'])
                try:
                    if rpc is None:
                        rpc = AgentRpc(state / 'agent.sock', agents)
                    # This also waits for initial bundled-browser copying and
                    # GUI-requested verification, without ever autostarting.
                    browsers = rpc.call('browser.status', timeout=30)
                    status = rpc.call('agent.status', timeout=30)
                    stable = stable + 1 if window and idle_status(status) else 0
                    if stable >= 2:
                        time.sleep(2)  # Allow completed RPC updates to paint.
                        require(process.poll() is None and not rpc.process.wait(0), 'Application exited after readiness')
                        window = visible_window(process, environment)
                        require(window is not None, 'CyberSnapper main window disappeared')
                        (evidence / 'readiness.json').write_text(json.dumps({
                            'agent_pid': rpc.process.pid, 'agent_start_ticks': rpc.process.start_ticks,
                            'status': status, 'browsers': browsers, 'idle_observations': stable}, indent=2) + '\n')
                        for target, filename in ((window['window_id'], 'main-window.png'), ('root', 'desktop.png')):
                            subprocess.run(['import', '-window', target, str(evidence / filename)],
                                           check=True, env=environment, timeout=20)
                        (evidence / 'window.json').write_text(json.dumps(window, indent=2) + '\n')
                        return
                except (OSError, ValueError) as error:
                    last_error = str(error)
                    stable = 0
                    if rpc is not None:
                        rpc.close()
                        rpc = None
                time.sleep(1)
            raise ValueError(f'Packaged GUI and isolated agent did not become ready: {last_error}')
        finally:
            teardown(process, gui, rpc, agents, evidence)


def verify(appimage, evidence):
    appimage = appimage.resolve()
    evidence = evidence.resolve()
    require(appimage.is_file(), f'AppImage does not exist: {appimage}')
    evidence.mkdir(parents=True, exist_ok=True)
    report = {'appimage': appimage.name, 'sha256': sha256(appimage), 'passed': False}
    try:
        with tempfile.TemporaryDirectory(prefix='cybersnapper-runtime-') as temporary:
            stage = Path(temporary)
            with (evidence / 'extraction.log').open('w') as log:
                subprocess.run([str(appimage), '--appimage-extract'], cwd=stage,
                               check=True, stdout=log, stderr=log, timeout=120,
                               env={k: v for k, v in os.environ.items() if k != 'APPIMAGE_EXTRACT_AND_RUN'})
            report['elf_count'] = inspect_elf(stage / 'squashfs-root', evidence / 'elf-audit.json')
            state = stage / 'isolated-home'
            state.mkdir()
            launch(appimage, state, evidence, stage / 'squashfs-root')
        report['passed'] = True
    except Exception as error:
        report['passed'] = False
        report['error'] = str(error)
        raise
    finally:
        (evidence / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Verified {report["elf_count"]} packaged ELF files and visible CyberSnapper startup')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appimage', type=Path)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    try:
        verify(args.appimage, args.evidence)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Linux AppImage verification failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
