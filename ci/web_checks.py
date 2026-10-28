#!/usr/bin/env python3
"""Run web checks against fixtures/emulators, never sibling .env.local files."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'web'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--suite', choices=['dashboard', 'firebase'], required=True)
args = parser.parse_args()
output = ROOT / 'test-reports/web' / args.suite
output.mkdir(parents=True, exist_ok=True)
configs = WEB / '.firebase/ci' / args.suite
configs.mkdir(parents=True, exist_ok=True)
empty = configs / 'empty-env'
empty.mkdir(exist_ok=True)
q = lambda value: json.dumps(str(value))
vite = configs / 'vite.config.ts'
vite.write_text('import base from "../../../vite.config";\nexport default { ...base, root: ' + q(WEB) + ', envDir: ' + q(empty) + ', build: {...base.build, outDir: ' + q(ROOT / 'build-ci/web/dist') + ', emptyOutDir: true }};\n')
# Clear any shell-provided production Firebase configuration too.
env = {key: value for key, value in os.environ.items() if not key.startswith('VITE_')}
env['CI'] = 'true'
if args.suite == 'firebase':
    env.update(VITE_FIREBASE_API_KEY='demo-key', VITE_FIREBASE_AUTH_DOMAIN='demo-vault.firebaseapp.com',
               VITE_FIREBASE_DATABASE_URL='https://demo-vault-default-rtdb.firebaseio.com',
               VITE_FIREBASE_PROJECT_ID='demo-vault', VITE_USE_EMULATORS='true')

bin_dir = WEB / 'node_modules/.bin'
commands = []
def add(name, command):
    commands.append((name, list(map(str, command))))
def playwright(name, port, test_dir, preview=False):
    config = configs / (name + '.config.ts')
    command = [str(bin_dir / 'vite'), 'preview' if preview else '--host', '127.0.0.1']
    if preview:
        command += ['--host', '127.0.0.1']
    command += ['--port', str(port), '--strictPort', '--config', str(vite)]
    config.write_text('import { defineConfig } from "@playwright/test";\nexport default defineConfig(' + json.dumps({
        'testDir': str(WEB / 'tests' / test_dir), 'workers': 1 if test_dir == 'live' else 2,
        'fullyParallel': test_dir != 'live', 'forbidOnly': True, 'retries': 0,
        'reporter': [['list'], ['junit', {'outputFile': str(output / (name + '.xml'))}], ['html', {'outputFolder': str(output / (name + '-html')), 'open': 'never'}]],
        'outputDir': str(output / (name + '-results')),
        'use': {'baseURL': f'http://127.0.0.1:{port}', 'trace': 'retain-on-failure', 'screenshot': 'only-on-failure', 'video': 'retain-on-failure'},
        'webServer': {'command': shlex.join(command), 'url': f'http://127.0.0.1:{port}', 'reuseExistingServer': False},
    }) + ');\n')
    return [bin_dir / 'playwright', 'test', '--config', config]

if args.suite == 'dashboard':
    add('typecheck', [bin_dir / 'tsc', '--noEmit'])
    add('build', [bin_dir / 'vite', 'build', '--config', vite])
    add('unit', [bin_dir / 'vitest', 'run', '--config', vite, '--reporter=default', '--reporter=junit', '--outputFile', output / 'unit.xml'])
    add('browser', playwright('browser', 4173, 'browser'))
    add('preview', playwright('preview', 4175, 'preview', True))
else:
    add('rules', [bin_dir / 'firebase', 'emulators:exec', '--project', 'demo-vault', '--only', 'database',
                  shlex.join(list(map(str, [bin_dir / 'vitest', 'run', '--config', 'vitest.rules.config.ts', '--reporter=default', '--reporter=junit', '--outputFile', output / 'rules.xml'])))])
    add('live', [bin_dir / 'firebase', 'emulators:exec', '--project', 'demo-vault', '--only', 'auth,database',
                 shlex.join(list(map(str, playwright('live', 4174, 'live'))))])
failed = []
for name, command in commands:
    print(f'Running {name}', flush=True)
    with (output / (name + '.log')).open('w') as log:
        process = subprocess.Popen(command, cwd=WEB, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in process.stdout:
            print(line, end='', flush=True)
            log.write(line)
        if process.wait():
            failed.append(name)
print('Failed checks: ' + ', '.join(failed) if failed else 'All checks passed')
sys.exit(bool(failed))
