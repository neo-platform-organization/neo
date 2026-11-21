import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
const [exe, image, depth, ...roles] = process.argv.slice(2);
const selection = JSON.parse(readFileSync(join(dirname(exe), 'selection.json'), 'utf8'));
const information = spawnSync(resolve(exe), ['--build-info'], {encoding: 'utf8', windowsHide: true});
assert.ifError(information.error); assert.equal(information.status, 0);
assert.ok(information.stdout.replaceAll('\r', '').includes('included ' + (selection.plugins.map(p => p.id).join(' ') || 'none') + '\n'),
    'Executable does not match the selected plugins');
if (roles.includes('entry')) {
    const result = spawnSync(process.execPath, [resolve(depth, 'tools/regression.mjs'), resolve(exe), resolve(image)],
        {stdio: 'inherit', windowsHide: true});
    if (result.error) throw result.error;
    process.exitCode = result.status ?? 1;
} else {
    const result = spawnSync(resolve(exe), ['--build-info'], {encoding: 'utf8', windowsHide: true});
    assert.ifError(result.error); assert.equal(result.status, 0); assert.match(result.stdout, /^target /);
    const start = spawnSync(resolve(exe), [], {encoding: 'utf8', windowsHide: true});
    assert.ifError(start.error); assert.equal(start.status, 0, start.stderr); assert.match(start.stdout, /no execution frontend selected/);
    console.log('PASS VM build without an execution frontend: ' + result.stdout.trim().replaceAll('\n', '; '));
}
