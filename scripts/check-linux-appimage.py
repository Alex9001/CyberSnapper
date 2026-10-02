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


def launch(appimage, state, evidence, appdir):
    require(bool(os.environ.get('DISPLAY')), 'DISPLAY is required; run with xvfb-run -a')
    environment = {**os.environ, 'QT_QPA_PLATFORM': 'xcb', 'APPIMAGE_EXTRACT_AND_RUN': '1',
                   'HOME': str(state), 'XDG_CONFIG_HOME': str(state / 'config'),
                   'XDG_DATA_HOME': str(state / 'data'), 'XDG_CACHE_HOME': str(state / 'cache'),
                   'XDG_RUNTIME_DIR': str(state / 'runtime'),
                   'CYBERSNAPPER_DEFAULT_PROJECT': str(state / 'project'),
                   'CYBERSNAPPER_AGENT_SERVER': str(state / 'agent.sock')}
    (state / 'runtime').mkdir(mode=0o700)
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD'):
        environment.pop(key, None)
    with (evidence / 'startup.log').open('w') as log:
        process = subprocess.Popen([str(appimage)], env=environment, stdout=log, stderr=log,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                require(process.poll() is None, f'AppImage exited with {process.returncode}; see startup.log')
                window = visible_window(process, environment)
                if window:
                    # Avoid accepting a transient splash or a window that
                    # immediately crashes: require continued visibility.
                    time.sleep(2)
                    require(process.poll() is None, 'AppImage exited after showing its window')
                    window = visible_window(process, environment)
                    require(window is not None, 'CyberSnapper main window disappeared')
                    subprocess.run(['import', '-window', window['window_id'],
                                    str(evidence / 'main-window.png')],
                                   check=True, env=environment, timeout=20)
                    subprocess.run(['import', '-window', 'root', str(evidence / 'desktop.png')],
                                   check=True, env=environment, timeout=20)
                    (evidence / 'window.json').write_text(json.dumps(window, indent=2) + '\n')
                    return
                time.sleep(0.1)
            raise ValueError('No visible CyberSnapper main window appeared within 60 seconds')
        finally:
            # The agent detaches from the GUI process group. Stop only this
            # isolated test agent before removing its private state directory.
            try:
                subprocess.run([str(appdir / 'usr/bin/cybersnapper-cli'), '--force', 'agent', 'stop'],
                               env=environment, stdout=log, stderr=log, timeout=30, check=False)
            except (OSError, subprocess.SubprocessError) as error:
                log.write(f'Agent cleanup warning: {error}\n')
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)


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
