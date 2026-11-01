"""Full OS catalogue/checkpoint/regrant integration; public pins from tls_live.ps1."""
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

pins = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
exe = str(Path('build/x86_64/Windows/neo.exe').resolve())
build = Path('build/x86_64/Windows')
processes = []
outputs = []


def send(process, commands):
    process.stdin.write('\n'.join(commands) + '\n')
    process.stdin.flush()


def waitfor(process, log, command, marker):
    offset = len(log.read_text(encoding='utf-8'))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        send(process, [command])
        time.sleep(.03)
        content = log.read_text(encoding='utf-8')
        if marker in content[offset:]:
            return
        if process.poll() is not None:
            break
    raise AssertionError(log.read_text(encoding='utf-8')[-12000:])


def launch(role, source, tag):
    log = build / ('network-os-' + role + '-' + tag + '.log')
    output = log.open('w', encoding='utf-8')
    outputs.append(output)
    process = subprocess.Popen([exe, 'live', str(source)], stdin=subprocess.PIPE,
                               stdout=output, stderr=subprocess.STDOUT, text=True)
    processes.append(process)
    return process, log


def bind(process, role, port, register):
    driver = '/workspace/' + ('incoming' if role == 'receive' else 'outgoing')
    mode = 'listen' if role == 'receive' else 'connect'
    local, peer = (pins['server'], pins['client']) if role == 'receive' else (pins['client'], pins['server'])
    scheduler = '/packages/system/scheduler'
    commands = [f'net-grant-tls {driver} {mode} 127.0.0.1 {port} {local} {peer}']
    if register:
        commands += [f'set {scheduler}/candidate-name "network"',
                     f'set {scheduler}/candidate-periodic true',
                     f'link {scheduler} candidate {driver} 99',
                     f'call {scheduler} register', f'unlink {scheduler} candidate']
    send(process, commands + [f'call {driver} start', f'drive {scheduler}', 'resume'])


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def checkpoint(process, log, role):
    driver = '/workspace/' + ('incoming' if role == 'receive' else 'outgoing')
    path = build / ('network-os-' + role + '-saved.neo')
    send(process, ['pause', f'call {driver} stop', f'unlink {driver} network',
                   f'delete {driver}/host-network', f'save {path.as_posix()}', 'quit'])
    process.wait(timeout=30)
    assert 'neo:' not in log.read_text(encoding='utf-8'), log.read_text(encoding='utf-8')[-12000:]
    result = subprocess.run([exe, 'check', str(path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return path


try:
    commands = []
    for package in ['exposure', 'session']:
        commands += [f'pkg-load neo/packages/{package}.neo', 'pkg install', 'pkg clear']
    for package, export, name in [('exposure', 'publisher', 'publisher'),
                                   ('exposure', 'subscriber', 'subscriber'),
                                   ('session', 'session', 'outgoing'), ('session', 'session', 'incoming')]:
        commands += [f'set /package-manager/id "neo.network.{package}"',
                     f'set /package-manager/export-name "{export}"',
                     f'set /package-manager/instance-name "{name}"', 'pkg instance']
    commands += ['add /workspace (scope (value 42))',
                 'link /workspace/publisher scope /workspace/scope 9',
                 'set /workspace/incoming/role "subscriber"']
    for service in ['publisher', 'subscriber']:
        commands += [f'set /workspace/{service}/origin "local"',
                     f'set /workspace/{service}/publication "restore"',
                     f'set /workspace/{service}/acknowledgements true',
                     f'set /workspace/{service}/heartbeats true']
    for driver, service in [('outgoing', 'publisher'), ('incoming', 'subscriber')]:
        commands += [f'link /workspace/{driver} runtime /workspace/{driver} 119',
                     f'link /workspace/{driver} service /workspace/{service} 127',
                     f'set /workspace/{driver}/require-authentication true']
    base = build / 'network-os-base.neo'
    result = subprocess.run([exe, 'live', 'neo/neo-os/image.neo'],
                            input='\n'.join(commands + [f'save {base.as_posix()}', 'quit']) + '\n',
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and 'neo:' not in result.stdout, result.stdout
    source = base.read_text(encoding='utf-8')
    nodes = len(re.findall(r'\(', re.sub(r'"(?:\\.|[^"\\])*"', '""', source)))
    assert 4096 < nodes <= 16384, nodes
    # Exercise the host file reader beyond its former 1 MiB ceiling as well.
    padded = build / 'network-os-padded.neo'
    data = base.read_bytes()
    padded.write_bytes(data + b'\n' * max(0, 1024 * 1024 + 32 - len(data)))
    checked = subprocess.run([exe, 'check', str(padded)], capture_output=True, text=True, timeout=30)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    port = free_port()
    receiver, rlog = launch('receive', base, 'first')
    sender, slog = launch('send', base, 'first')
    bind(receiver, 'receive', port, True)
    waitfor(receiver, rlog, 'show /workspace/incoming/phase', 'phase = "connecting"')
    bind(sender, 'send', port, True)
    waitfor(sender, slog, 'show /workspace/publisher/received-revision', 'received-revision = 1')
    send(receiver, ['pause', 'set /workspace/subscriber/accept-name "first"',
                    'call /workspace/subscriber accept', 'resume'])
    waitfor(sender, slog, 'show /workspace/publisher/accepted-revision', 'accepted-revision = 1')
    send(sender, ['pause', 'set /workspace/scope/value 99', 'call /workspace/outgoing resync', 'resume'])
    waitfor(sender, slog, 'show /workspace/publisher/received-revision', 'received-revision = 2')
    receiver_saved = checkpoint(receiver, rlog, 'receive')
    sender_saved = checkpoint(sender, slog, 'send')
    receiver, rlog = launch('receive', receiver_saved, 'restored')
    sender, slog = launch('send', sender_saved, 'restored')
    waitfor(receiver, rlog, 'show /workspace/incoming/phase', 'phase = "stopped"')
    waitfor(receiver, rlog, 'show /workspace/subscriber/inbox/candidate/revision', 'revision = 2')
    waitfor(receiver, rlog, 'show /workspace/subscriber/accepted/first/state/value', 'value = 42')
    waitfor(sender, slog, 'show /workspace/publisher/revision', 'revision = 2')
    # Source restore is inert: no endpoints in the checkpoint, no driver/resume restoration.
    for saved in [receiver_saved, sender_saved]:
        text = saved.read_text(encoding='utf-8')
        assert 'host-network' not in text and '"network" @' not in text
    port = free_port()
    bind(receiver, 'receive', port, False)
    waitfor(receiver, rlog, 'show /workspace/incoming/phase', 'phase = "connecting"')
    bind(sender, 'send', port, False)
    waitfor(receiver, rlog, 'show /workspace/subscriber', 'deferred = object')
    waitfor(receiver, rlog, 'show /workspace/subscriber/deferred/revision', 'revision = 3')
    waitfor(receiver, rlog, 'show /workspace/incoming/peer-identity', 'peer-identity = "' + pins['client'] + '"')
    waitfor(sender, slog, 'show /workspace/outgoing/peer-identity', 'peer-identity = "' + pins['server'] + '"')
    send(receiver, ['pause', 'set /workspace/subscriber/accept-name "old"',
                    'call /workspace/subscriber accept', 'resume'])
    waitfor(sender, slog, 'show /workspace/publisher/received-revision', 'received-revision = 3')
    waitfor(sender, slog, 'show /workspace/publisher/accepted-revision', 'accepted-revision = 1')
    send(receiver, ['pause', 'set /workspace/subscriber/accept-name "third"',
                    'call /workspace/subscriber accept', 'resume'])
    waitfor(sender, slog, 'show /workspace/publisher/accepted-revision', 'accepted-revision = 3')
    waitfor(receiver, rlog, 'show /workspace/subscriber/accepted/first/state/value', 'value = 42')
    waitfor(receiver, rlog, 'show /workspace/subscriber/accepted/third/state/value', 'value = 99')
    checkpoint(receiver, rlog, 'receive')
    checkpoint(sender, slog, 'send')
    print(f'PASS full OS networking: {nodes} base objects, {base.stat().st_size} checkpoint bytes; '
          'both package catalogues and four instances; TLS transfer, explicit decisions, '
          'save/restore, fresh grants and preserved revisions/candidates/accepted state')
finally:
    for process in processes:
        if process.poll() is None:
            process.kill()
            process.wait()
    for output in outputs:
        output.close()
