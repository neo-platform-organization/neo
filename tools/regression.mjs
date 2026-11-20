// Make owns compilation. This driver runs checks using direct process APIs.
import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync} from 'node:fs';
import {resolve, join, dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = resolve(process.argv[2] ?? 'build/x86_64/Windows/neo.exe');
const image = resolve(process.argv[3] ?? 'neo/neo-os/image.neo');
const data = JSON.parse(readFileSync(join(root, 'tools/regression-data.json'), 'utf8').replace(/^\uFEFF/, ''));
const arrays = data.arrays;
const temporaryRoot = resolve(tmpdir());
const directory = mkdtempSync(join(temporaryRoot, 'neo-check-'));
const paths = {image};
for (const name of ['changed', 'invalid', 'saved', 'weft', 'lowered', 'candidate', 'effect', 'event_text', 'event_large']) {
    paths[name] = join(directory, `${name} test source`);
}
let checks = 0;
function execute(args, input) {
    const child = spawnSync(exe, args, {cwd: root, input, encoding: 'utf8', windowsHide: true,
        timeout: 60000, maxBuffer: 4 * 1024 * 1024, env: {...process.env, NEO_TEST_RENDER_READBACK: '1', NEO_AUDIO_DEVICE: 'null', NEO_TEST_PRIVATE_SELECTION: '1'}});
    assert.ifError(child.error);
    assert.equal(child.status, 0, `${args.join(' ')}\n${child.stderr}\n${child.stdout}`);
    return child.stdout.replace(/\r\n/g, '\n').trimEnd().split('\n');
}
function expand(command) {
    return command.replace(/\$(\w+)/g, (_, name) => {
        assert.ok(paths[name], `Unknown test path ${name}`);
        return paths[name];
    });
}
function session(commands, source = paths.lowered) {
    return execute(['session', source], commands.map(expand).join('\n') + '\n');
}
function check(name, expected, source = paths.lowered) {
    const actual = session(arrays[name], source);
    assert.deepEqual(actual, typeof expected === 'string' ? arrays[expected] : expected, name);
    ++checks;
    return actual;
}
function prefix(name, expected, pattern, source = paths.lowered) {
    const actual = session(arrays[name], source);
    assert.deepEqual(actual.slice(0, -1), arrays[expected], name);
    assert.match(actual.at(-1), pattern, name);
    ++checks;
    return actual;
}
function save(name, text) { writeFileSync(paths[name], text); }

async function idleCheck() {
    const child = spawn(exe, ['session', image], {cwd: root, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe']});
    let stderr = '';
    child.stderr.on('data', bytes => { stderr += bytes; });
    const lines = createInterface({input: child.stdout})[Symbol.asyncIterator]();
    let timer;
    const deadline = new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`Idle timer failed: ${stderr}`)), 5000); });
    const closed = new Promise((resolveExit, reject) => {
        child.on('error', reject);
        child.on('exit', code => code === 0 ? resolveExit() : reject(new Error(`Idle session exited ${code}: ${stderr}`)));
    });
    // A failure before awaiting closed must still have a rejection handler.
    closed.catch(() => {});
    try {
        child.stdin.write('after idle 30 instrument increment 7\n');
        for (const expected of ['queued idle', 'event idle result 7']) {
            const line = await Promise.race([lines.next(), deadline]);
            assert.equal(line.value, expected);
        }
        child.stdin.end('quit\n');
        await Promise.race([closed, deadline]);
        ++checks;
    } finally {
        clearTimeout(timer);
        child.kill();
    }
}

try {
    for (const line of execute(['selftest'])) { console.log(line); }
    const store = join(directory, 'exercise-store');
    mkdirSync(store);
    const exerciseArgs = ['exercise-test', image, 'lifecycle-exercise', store];
    for (const [initial, saved] of [[0, 5], [5, 10]]) {
        const result = execute(exerciseArgs).join('\n');
        assert.match(result, new RegExp(`PASS exercise: restored ${initial} saved ${saved};`));
        console.log(result);
    }
    // A separate process restores the stored graph, with no serialized grants.
    const checkpoint = join(directory, 'checkpoint.neo');
    // Ask the broker for portable Warp instead of interpreting private headers.
    const graphSnapshot = execute(['exercise-snapshot', store]).join('\n');
    writeFileSync(checkpoint, graphSnapshot);
    assert.deepEqual(session(['send lifecycle-exercise reading', 'send lifecycle-exercise restore'], checkpoint),
        ['result 10', 'error document access denied']);
    const platform = execute(['platform']);
    assert.match(platform[0], /word-bits 64 processors [1-9][0-9]* page-bytes [1-9][0-9]* clock-resolution-ns [1-9]/);
    assert.match(platform[1], /input [01] surfaces [01] storage 1 network-loopback 1 entropy 1 native-isolation 1/);
    assert.deepEqual(execute(['session', image], 'send instrument reading'), ['result 0']);
    assert.deepEqual(execute(['session', image], 'x'.repeat(70000) + '\nrevision\nquit\n'),
        ['error command exceeds limit', 'revision 1']);
    assert.deepEqual(execute(['run', image]), ['42']);
    assert.deepEqual(execute(['roundtrip', image]), ['42']);
    const initial = execute(['snapshot', image]).join('\n');
    assert.match(initial, /\(slot level 42\)/);
    assert.match(initial, /\(slot level 7\)/);
    const cloned = prefix('clones', 'clone_expected', /\(object first-copy \(delegate @instrument\).*\(slot level 8\)\)/, image);
    save('saved', cloned.at(-1));
    check('restored_clone', ['result 8', 'result 5'], paths.saved);
    check('deletion', 'deletion_expected', paths.saved);
    save('weft', data.fixture);
    const lowered = execute(['weft', paths.weft]).join('\n');
    save('lowered', lowered);
    assert.deepEqual(execute(['run-weft', paths.weft]), ['42']);

    const events = session(arrays.events, image);
    assert.deepEqual(events.filter(line => line.startsWith('event ')), arrays.event_expected);
    assert.ok(events.includes('cancelled cancelled') && events.includes('result 10'));
    await idleCheck();
    save('event_text', 'hello world'); save('event_large', 'x'.repeat(80));
    const fileCommands = arrays.file_events.map((line, index) => line.replace('{0}', index === 0 ? paths.event_text : paths.event_large));
    const files = session(fileCommands);
    assert.deepEqual(files.filter(line => line.startsWith('event ')), arrays.file_expected);
    assert.equal(files.filter(line => line === 'result "hello world"').length, 2);
    check('graph_events', [...arrays.graph_expected, ...Array.from({length: 8}, (_, i) => `event image-${i+3} result 100`), 'result 100']);
    check('inherited_events', 'inherited_expected', image);
    const full = session([
        ...Array.from({length: 31}, (_, i) => `after held-${i+1} 60000 meter reading`),
        'trace workspace queue-many 2', 'send meter reading',
        ...Array.from({length: 31}, (_, i) => `cancel held-${i+1}`), 'await', 'send meter reading', 'quit']);
    assert.ok(full.includes('error event queue full'));
    assert.equal(full.filter(line => line === 'result 0').length, 2);
    assert.ok(!full.some(line => line.startsWith('event ') || line.includes('committed')));
    save('candidate', lowered.replace('(call self sum value 7)', '(call self sum value 9)'));
    const acceptance = prefix('acceptance', 'acceptance_expected', /\(slot total 37\)/);
    save('saved', acceptance.at(-1));
    check('reopened', 'reopened_expected', paths.saved);
    check('package_calls', 'package_expected');
    save('saved', lowered.replace('(import calculate @package-service add)', '(import calculate @package-service-other add)').trimEnd().slice(0, -1) +
        ' (object package-service-other (package arithmetic-service) (export add) (handler add value 99)))');
    check('retarget_calls', ['import allowed', 'revision 2', 'error package import access denied']);

    for (const [name, expected] of [
        ['lifecycle','lifecycle_expected'], ['fractions','fraction_expected'], ['float_vector','float_vector_expected'],
        ['floating_returns','floating_expected'], ['constructed','construction_expected'], ['ref_results','ref_returns'],
        ['vector_results','vector_returns'], ['byte_results','byte_expected'], ['text_input','input_expected'],
        ['text_copy','copy_expected'], ['text_result','result_expected']]) { check(name, expected); }
    check('locals', ['result 3.125', 'revision 1 receiver meter owner meter target slot caption', '    slot caption = "changed";']);
    check('dispatch', ['result 5', 'result 1', 'error runtime receiver does not implement the message']);
    for (const [name, expected, pattern] of [
        ['live_floats','live_float_expected', /\(slot fractional-coordinates ~\[4\.5, -6\.25\]\)/],
        ['float_slot','float_expected', /\(slot fraction ~3\.125\)/],
        ['refs','refs_expected', /\(slot selected @math\)/],
        ['vector_slot','vector_expected', /\(slot copied-coordinates \[7, -2, 15\]\)/],
        ['binary_slot','binary_expected', /\(slot copied-packet #00ff\)/],
        ['text_writes','text_expected', /\(slot caption ""\)/]]) { prefix(name, expected, pattern); }

    const projected = execute(['source', image]).join('\n');
    assert.match(projected, /if \(input < 0\) then return 0/);
    assert.match(projected, /left \* right/);
    save('weft', projected);
    assert.deepEqual(execute(['run-weft', paths.weft]), ['42']);
    save('weft', projected.replace('handler reading() = get self.level;', 'handler reading() = call @arithmetic.double(get self.level);'));
    const live = session(arrays.live_weft, image);
    assert.deepEqual(live.slice(0, 4), arrays.live_expected);
    assert.match(live.join('\n'), /handler reading\(\) = call @arithmetic.double/);
    const changed = readFileSync(image, 'utf8').replace('(handler reading (get self level))', data.changedHandlers)
        .replace('(slot level 0)', '(slot extra 5) (slot level 100)');
    save('changed', changed);
    save('invalid', changed.replace('(send @instrument sample 21)', '(send @instrument sample)'));
    const response = prefix('commands', 'expected', /\(slot extra 5\) \(slot level 42\)/, image);
    assert.match(response.at(-1), /\(slot level 7\)/);
    for (const [name, expected] of [['arguments','argument_results'], ['multi','multi_expected'], ['trace','trace_expected'], ['evaluation','evaluation_expected']]) {
        check(name, expected, image);
    }
    const loop = session(arrays.loop, image);
    assert.deepEqual(loop.slice(0, 9), arrays.loop_start);
    assert.deepEqual(loop.slice(-4), arrays.loop_end);
    const inspection = session(arrays.inspection, image).join('\n');
    for (const pattern of [/target knight double\n\(knight double value/, /target knight double\n    knight double\(value\)/,
        /receiver instrument owner instrument-prototype target handler sample\n    handler sample/,
        /result 0\nresult 42\nrevision 2/, /target slot level\n\(slot level 100\)/,
        /target slot level\n    slot level = 42;\nerror unknown inspection member/]) { assert.match(inspection, pattern); }
    const view = session(arrays.knight_view, image).join('\n');
    assert.match(view, /target knight double\n    knight double\(value\)/);
    assert.match(view, /transition increment\(state, amount\) -> \{ nextState, result \}/);
    assert.match(view, /target handler export\nunresolved: document I\/O\nresult 0$/);
    const transformed = session(arrays.transformed, image);
    assert.equal(transformed.length, 5);
    assert.match(transformed[0], /^\(let factor 3 /);
    assert.deepEqual(transformed.slice(1,3), ['error no matching atom in Knight body', 'error stale graph revision']);
    assert.match(transformed[4], /\(let factor 2 /);
    save('candidate', transformed[0]); save('effect', '(write-document value)');
    const probe = [...arrays.probe];
    probe[1] = `probe 1 arithmetic double as knight "${paths.candidate}" 21`;
    probe[2] = `probe 1 arithmetic double as knight "${paths.effect}" 21`;
    assert.deepEqual(session(probe, image), arrays.probe_expected);
    let publishIndex = 0;
    const publication = arrays.publication.map(line => line.includes('{0}') ?
        line.replace('{0}', publishIndex++ === 0 ? paths.effect : paths.candidate) : line);
    assert.deepEqual(session(publication, image), arrays.publication_expected);
    const lattice = session(arrays.lattice, image);
    assert.equal(lattice.length, 16);
    assert.deepEqual(lattice.slice(0,5), ['placed instrument','projection revision 1 x y','instrument 2 3','projection revision 1 z t','instrument 5 7']);
    for (const index of [5,7,11]) { assert.equal(lattice[index], 'revision 1 receiver instrument owner instrument target object instrument'); }
    assert.equal(lattice[6], lattice[8]); assert.equal(lattice[6], lattice[12]);
    assert.equal(lattice[10], 'error ambiguous projected point');
    assert.deepEqual(lattice.slice(13), ['revision 1','revision 2','error no node at projected point']);
    save('saved', response.at(-1));
    check('restored', ['result 84'], paths.saved);
    console.log(`PASS Make regression driver: ${checks} session checks plus native, event, source-view, and persistence gates`);
} finally {
    // Delete only the uniquely created test directory directly under the temp root.
    assert.equal(dirname(resolve(directory)), temporaryRoot);
    assert.ok(directory.startsWith(join(temporaryRoot, 'neo-check-')));
    rmSync(directory, {recursive: true, force: true});
}
