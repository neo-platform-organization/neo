import assert from 'node:assert/strict';
import {mkdtempSync, writeFileSync, mkdirSync, rmSync, readFileSync} from 'node:fs';
import {join, dirname, resolve} from 'node:path';
import {spawn, spawnSync} from 'node:child_process';
import {catalog, plan, emit} from './plugins.mjs';

const testRoot = resolve('.release/plugin-tests'); mkdirSync(testRoot, {recursive: true});
const directory = mkdtempSync(join(testRoot, 'neo-plugin-test-'));
const target = 'x86_64/Windows';
const configuration = (depth, requested) => ({format: 'neo.vm-build/1', depth, requested});
try {
    const root = join(directory, 'bundle'); mkdirSync(root);
    writeFileSync(join(root, 'code.c'), 'This is inert test input, not executable JavaScript or valid C.');
    const base = {format: 'neo.plugin/1', version: 1, collection: 'depth', kind: 'native', sources: ['code.c'], targets: {[target]: {}}};
    const definitions = [
        {...base, id: 'platform', abi: 2, role: 'platform', entry: 'platform_entry', requires: []},
        {...base, id: 'language', abi: 1, role: 'language', entry: 'language_entry', requires: ['platform']},
        {...base, id: 'preos', abi: 1, role: 'entry', entry: 'preos_entry', requires: ['language']},
    ];
    definitions.forEach(p => writeFileSync(join(root, `${p.id}.json`), JSON.stringify(p)));
    const index = join(root, 'catalog.json');
    const saveIndex = bundles => writeFileSync(index, JSON.stringify({format: 'neo.plugin-catalog/1', bundles}));
    saveIndex(definitions.map(p => `${p.id}.json`));
    const plugins = catalog(index);
    assert.deepEqual(plan(plugins, configuration(false, []), target), []);
    assert.deepEqual(plan(plugins, configuration(true, ['preos']), target).map(p => p.id), ['platform', 'language', 'preos']);
    assert.deepEqual(plan(plugins, configuration(true, ['language']), target).map(p => p.id), ['platform', 'language']);
    assert.throws(() => plan(plugins, configuration(false, ['preos']), target), /depth was disabled/);
    assert.throws(() => plan(plugins, configuration(true, ['missing']), target), /Missing plugin/);
    assert.throws(() => plan(plugins, configuration(true, ['preos']), 'arm64/Linux'), /does not support/);
    assert.throws(() => plan(plugins, configuration(false, []), 'x86_64/x86_64'), /needs a boot/);
    const cycle = new Map(plugins); cycle.set('platform', {...plugins.get('platform'), requires: ['preos']});
    assert.throws(() => plan(cycle, configuration(true, ['preos']), target), /cycle/);
    const escape = new Map(plugins); escape.set('platform', {...plugins.get('platform'), sources: ['../outside.c']});
    writeFileSync(join(directory, 'outside.c'), 'outside');
    assert.throws(() => plan(escape, configuration(true, ['platform']), target), /escapes/);
    const badFlags = new Map(plugins); badFlags.set('platform', {...plugins.get('platform'), targets: {[target]: {libraries: ['-lm;echo unsafe']}}});
    assert.throws(() => plan(badFlags, configuration(true, ['platform']), target), /Invalid library/);
    const conflict = new Map(plugins);
    conflict.set('second', {...plugins.get('language'), id: 'second', requires: []});
    assert.throws(() => plan(conflict, configuration(true, ['language', 'second']), target), /Conflicting plugins/);
    const disguised = new Map(plugins);
    disguised.set('asset', {...plugins.get('platform'), id: 'asset', kind: 'data', role: 'asset', collection: 'assets', payload: 'code.c'});
    assert.throws(() => plan(disguised, configuration(false, ['asset']), target), /Data plugins cannot add native/);
    writeFileSync(join(root, 'platform.json'), JSON.stringify({...definitions[0], abi: 99}));
    assert.throws(() => catalog(index), /interface ABI/);
    writeFileSync(join(root, 'platform.json'), JSON.stringify(definitions[0]));
    saveIndex(['platform.json', 'platform.json']); assert.throws(() => catalog(index), /Duplicate plugin/);
    saveIndex(definitions.map(p => `${p.id}.json`));
    const out = join(directory, 'out');
    emit([], target, out);
    assert.ok(!readFileSync(join(out, 'selection.c'), 'utf8').includes('extern '));
    assert.match(readFileSync(join(out, 'plugins.mk'), 'utf8'), /PLUGIN_SOURCES := \n/);
    const emptyKey = readFileSync(join(out, 'plugins.mk'), 'utf8').match(/PLUGIN_KEY := (\w+)/)[1];
    emit(plan(plugins, configuration(true, ['platform']), target), target, out);
    assert.notEqual(readFileSync(join(out, 'plugins.mk'), 'utf8').match(/PLUGIN_KEY := (\w+)/)[1], emptyKey);
    // Drive prompts when they appear, rather than queueing answers before the
    // second question exists. An EOF must cancel instead of choosing defaults.
    const config = join(directory, 'interactive.json');
    const child = spawn(process.execPath, ['tools/plugins.mjs', 'configure', target, config, index, out],
        {windowsHide: true, stdio: ['pipe', 'pipe', 'pipe']});
    let output = '', error = '', answered = 0;
    child.stdout.on('data', bytes => {
        output += bytes;
        if (answered === 0 && output.includes('[y/N]')) { answered = 1; child.stdin.write('y\n'); }
        if (answered === 1 && output.includes('Which plugins?')) { answered = 2; child.stdin.write('3\n'); }
    });
    child.stderr.on('data', bytes => { error += bytes; });
    const timeout = setTimeout(() => child.kill(), 10000);
    const exit = await new Promise((accept, reject) => { child.on('error', reject); child.on('exit', accept); });
    clearTimeout(timeout); assert.equal(exit, 0, error); assert.equal(answered, 2);
    assert.deepEqual(JSON.parse(readFileSync(config, 'utf8')).requested, ['preos']);
    const independent = spawnSync(process.execPath, ['tools/plugins.mjs', 'configure', target,
        join(directory, 'independent.json'), join(directory, 'missing-catalog.json'), out, '--no-depth'],
        {encoding: 'utf8', windowsHide: true, timeout: 10000});
    assert.ifError(independent.error); assert.equal(independent.status, 0, independent.stderr);
    const cancelled = spawnSync(process.execPath, ['tools/plugins.mjs', 'configure', target, config, index, out],
        {input: '', encoding: 'utf8', windowsHide: true, timeout: 10000});
    assert.ifError(cancelled.error); assert.equal(cancelled.status, 1); assert.match(cancelled.stderr, /cancelled/);
    if (process.argv[2]) {
        const dryRun = spawnSync(process.argv[2], ['-n', `CONFIG=${join(directory, 'dry-run.json')}`],
            {encoding: 'utf8', windowsHide: true, timeout: 5000, maxBuffer: 64 * 1024});
        assert.ifError(dryRun.error); assert.equal(dryRun.status, 0, dryRun.stderr);
        assert.match(dryRun.stdout, /plugins\.mjs configure/);
        assert.equal((dryRun.stdout.match(/plugins\.mjs configure/g) ?? []).length, 1,
            'Dry run recursively reentered Make before configuration');
    }
    console.log('PASS plugin builder: inert catalog, selection/dependencies, no-depth, target checks, cycle/duplicate/path rejection, safe emission, interactive prompts, and cancellation');
} finally {
    assert.equal(dirname(resolve(directory)), testRoot);
    assert.ok(directory.startsWith(join(testRoot, 'neo-plugin-test-')));
    rmSync(directory, {recursive: true, force: true});
}
