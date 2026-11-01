#!/usr/bin/env python3
"""Publish the current game bundle to its existing exe.dev VM over SSH."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
WEB = HERE.parents[1]
ROOT = WEB.parents[2]
REMOTE = '/home/exedev/poketcg'


def public_file(path):
    parts = path.parts
    if any(part.startswith('.') for part in parts):
        return False
    if len(parts) == 1:
        return path.name in {'index.html', 'duel.html', 'tcg.wasm', 'build-info.json'}
    if parts[0] == 'css':
        return path.suffix == '.css'
    if parts[0] == 'js':
        return path.suffix == '.js'
    if parts[0] in {'gfx', 'img'}:
        return path.suffix == '.png'
    if parts[0] == 'intro':
        return path.suffix == '.png' or path.name == 'intro_assets.json'
    if parts[0] == 'audio':
        return path.suffix == '.ogg' or path.name == 'audio.json'
    if parts[0] == 'world':
        return (str(path) in {'world/world.json', 'world/actors.l64'} or
                (parts[1] == 'maps' and path.suffix == '.l64') or
                (parts[1] == 'textures' and path.suffix == '.png'))
    return str(path) == 'saves/diary2-after-tutorial.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='poketcg64.exe.xyz')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]*\.exe\.xyz', args.host):
        parser.error('Use the VM hostname under exe.xyz')
    dist = WEB/'dist'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    release = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+commit[:12]
    work = WEB/'build/exe-deploy'/release
    work.mkdir(parents=True)
    files = {}
    for source in sorted(dist.rglob('*')):
        relative = source.relative_to(dist)
        if not public_file(relative):
            continue
        if source.is_symlink():
            raise RuntimeError(f'Public bundle cannot contain a symlink: {relative}')
        if source.is_file():
            files['public/'+str(relative)] = source
    for required in ('index.html', 'duel.html', 'tcg.wasm', 'world/world.json',
                     'audio/audio.json', 'saves/diary2-after-tutorial.json'):
        assert 'public/'+required in files, required
    for name in ('server.js', 'package.json', 'package-lock.json'):
        files['server/'+name] = WEB/'server'/name
    files['poketcg.service'] = HERE/'poketcg.service'
    manifest = {'release': release, 'source_commit': commit, 'host': args.host,
                'public_url': 'https://'+args.host+'/',
                'files': {name: {'bytes': p.stat().st_size,
                                'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                          for name, p in files.items()}}
    manifest_file = work/'release.json'
    manifest_file.write_text(json.dumps(manifest, indent=2)+'\n')
    archive = work/'release.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        for name, source in files.items():
            tar.add(source, arcname=name, recursive=False)
        tar.add(manifest_file, arcname='release.json', recursive=False)
    destination = REMOTE+'/releases/'+release
    command = 'mkdir -p '+shlex.quote(destination)+' && tar -xzf - --no-same-owner -C '+shlex.quote(destination)
    with archive.open('rb') as data:
        subprocess.run(['ssh', '-o', 'BatchMode=yes', args.host, command], stdin=data, check=True)
    # Verify the transferred bundle, install locked runtime dependencies, and
    # atomically select it. A failed health check restores the prior release.
    worker = '''import hashlib,json,os,pathlib,subprocess,time,urllib.request
root=pathlib.Path(REMOTE)
release=root/'releases'/RELEASE
manifest=json.loads((release/'release.json').read_text())
for name,entry in manifest['files'].items():
    assert hashlib.sha256((release/name).read_bytes()).hexdigest()==entry['sha256'],name
env=dict(os.environ,PATH='/opt/poketcg-node/bin:/usr/bin:/bin')
subprocess.run(['/opt/poketcg-node/bin/npm','ci','--omit=dev','--ignore-scripts','--no-audit','--no-fund'],cwd=release/'server',env=env,check=True)
current=root/'current';prior=current.resolve() if current.exists() else None
pending=root/'current.next';pending.unlink(missing_ok=True);pending.symlink_to(release);os.replace(pending,current)
try:
    subprocess.run(['sudo','install','-m','0644',str(release/'poketcg.service'),'/etc/systemd/system/poketcg.service'],check=True)
    subprocess.run(['sudo','systemctl','daemon-reload'],check=True)
    subprocess.run(['sudo','systemctl','enable','poketcg.service'],check=True)
    subprocess.run(['sudo','systemctl','restart','poketcg.service'],check=True)
    for attempt in range(30):
        try:
            with urllib.request.urlopen('http://127.0.0.1:8000/health',timeout=2) as response:assert response.read()==b'ok'
            break
        except Exception:
            if attempt==29:raise
            time.sleep(.5)
except Exception:
    if prior:
        pending.unlink(missing_ok=True);pending.symlink_to(prior);os.replace(pending,current)
        subprocess.run(['sudo','systemctl','restart','poketcg.service'],check=True)
    raise
print(json.dumps({'release':RELEASE,'url':manifest['public_url'],'public_files':sum(p.startswith('public/') for p in manifest['files']),'service':'active'}))
'''.replace('REMOTE', repr(REMOTE)).replace('RELEASE', repr(release))
    result = subprocess.check_output(['ssh', '-o', 'BatchMode=yes', args.host, 'python3 -'], input=worker.encode())
    (work/'deployment.log').write_bytes(result)
    print(result.decode(), end='')
    print('Local manifest:', manifest_file)


if __name__ == '__main__':
    main()
