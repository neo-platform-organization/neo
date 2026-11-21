// VM build mechanism. Plugin repositories supply passive manifests and payloads.
import {readFileSync, writeFileSync, existsSync, mkdirSync, realpathSync, renameSync} from 'node:fs';
import {resolve, dirname, relative, isAbsolute, join} from 'node:path';
import {createInterface} from 'node:readline/promises';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';

function read(path) { return JSON.parse(readFileSync(path, 'utf8').replace(/^\uFEFF/, '')); }
function requireValue(value, message) { if (!value) throw new Error(message); }
function atom(value) { return typeof value === 'string' && /^[A-Za-z_][A-Za-z0-9_.-]*$/.test(value); }
function strings(value, label) {
    requireValue(Array.isArray(value) && value.every(v => typeof v === 'string'), `Invalid ${label}`);
    return value;
}
function localFile(root, value, directory = false) {
    requireValue(typeof value === 'string' && value && !isAbsolute(value) && !value.includes('\\'), 'Expected a relative bundle path');
    const path = realpathSync(resolve(root, value));
    const rel = relative(realpathSync(root), path);
    requireValue((directory || rel) && rel !== '..' && !rel.startsWith('../') && !rel.startsWith('..\\') && !isAbsolute(rel), 'Payload escapes its bundle');
    // These are build inputs, never Make fragments or command-line strings.
    requireValue(!/[\s$#;"'`]/.test(path), 'Build input paths currently cannot contain whitespace or shell/Make syntax');
    const result = relative(process.cwd(), path).replaceAll('\\', '/');
    requireValue(!result.includes(':') && !result.split('/').includes('__parent__'), 'Unsupported or reserved build path');
    return result;
}
export function catalog(path) {
    const document = read(path);
    requireValue(document.format === 'neo.plugin-catalog/1', 'Unsupported plugin catalog format');
    const result = new Map();
    for (const entry of strings(document.bundles, 'catalog bundles')) {
        const file = resolve(dirname(path), entry), plugin = read(file);
        requireValue(plugin.format === 'neo.plugin/1' && atom(plugin.id) && Number.isSafeInteger(plugin.version) && plugin.version > 0,
            `Invalid plugin identity in ${file}`);
        requireValue(!result.has(plugin.id), `Duplicate plugin ${plugin.id}`);
        requireValue(atom(plugin.collection) && ['native', 'data'].includes(plugin.kind), `Invalid plugin kind/collection: ${plugin.id}`);
        requireValue(plugin.targets && typeof plugin.targets === 'object' && !Array.isArray(plugin.targets), `Missing targets: ${plugin.id}`);
        strings(plugin.requires ?? [], 'plugin dependencies').forEach(id => requireValue(atom(id), 'Invalid dependency name'));
        if (plugin.kind === 'native') {
            requireValue(['platform', 'language', 'entry', 'library'].includes(plugin.role), 'Unsupported native plugin role');
            if (plugin.role !== 'library') requireValue(typeof plugin.entry === 'string' && /^[A-Za-z_]\w*$/.test(plugin.entry), 'Invalid native entry symbol');
            if (plugin.role !== 'library') requireValue(plugin.abi === {platform: 2, language: 1, entry: 1}[plugin.role], 'Unsupported plugin interface ABI');
        } else requireValue(atom(plugin.role) && !['platform', 'language', 'entry'].includes(plugin.role) && typeof plugin.payload === 'string', 'Invalid data plugin');
        result.set(plugin.id, {...plugin, file, root: dirname(file)});
    }
    return result;
}
export function plan(plugins, config, target) {
    requireValue(config.format === 'neo.vm-build/1' && typeof config.depth === 'boolean', 'Unsupported VM configuration');
    const requested = strings(config.requested, 'requested plugins');
    const result = [], active = new Set(), done = new Set(), roles = new Set();
    function visit(id) {
        requireValue(atom(id), 'Invalid selected plugin name');
        requireValue(!active.has(id), `Plugin dependency cycle at ${id}`);
        if (done.has(id)) return;
        const plugin = plugins.get(id);
        requireValue(plugin, `Missing plugin ${id}`);
        requireValue(config.depth || plugin.collection !== 'depth', `${id} requires depth, but depth was disabled`);
        requireValue(Object.hasOwn(plugin.targets, target), `${id} does not support ${target}`);
        const variant = plugin.targets[target];
        requireValue(variant && typeof variant === 'object' && !Array.isArray(variant), 'Invalid target variant');
        if (plugin.kind === 'data') requireValue(!(plugin.sources?.length || variant.sources?.length ||
            plugin.includes?.length || variant.includes?.length || variant.embeds_image?.length ||
            variant.assembly?.length || variant.libraries?.length), 'Data plugins cannot add native code or compiler inputs');
        active.add(id);
        for (const dep of [...(plugin.requires ?? []), ...strings(variant.requires ?? [], 'target dependencies')]) visit(dep);
        active.delete(id); done.add(id);
        if (['platform', 'language', 'entry', 'image'].includes(plugin.role)) {
            requireValue(!roles.has(plugin.role), `Conflicting plugins for role ${plugin.role}`);
            roles.add(plugin.role);
        }
        const sources = [...strings(plugin.sources ?? [], 'sources'), ...strings(variant.sources ?? [], 'target sources')]
            .map(path => localFile(plugin.root, path));
        const assembly = strings(variant.assembly ?? [], 'assembly').map(path => localFile(plugin.root, path));
        const libraries = strings(variant.libraries ?? [], 'libraries');
        const includes = [...strings(plugin.includes ?? [], 'includes'), ...strings(variant.includes ?? [], 'target includes')]
            .map(path => localFile(plugin.root, path, true));
        const embeds = strings(variant.embeds_image ?? [], 'embedded-image sources').map(path => localFile(plugin.root, path));
        requireValue(embeds.every(path => sources.includes(path)), 'Embedded-image source is not part of this plugin');
        requireValue(libraries.every(value => /^-l[A-Za-z0-9_]+$/.test(value) || value === '-pthread'), 'Invalid library option');
        requireValue(sources.every(path => path.endsWith('.c')) && assembly.every(path => path.endsWith('.S')), 'Invalid source kind');
        const payload = plugin.kind === 'data' ? localFile(plugin.root, plugin.payload) : null;
        result.push({...plugin, sources, assembly, libraries, includes, embeds, payload});
    }
    for (const id of requested) visit(id);
    requireValue(target !== 'x86_64/x86_64' || roles.has('platform'), 'Standalone x86_64 needs a boot/platform plugin; a no-depth host bootstrap can still be built');
    return result;
}
function save(path, text) {
    mkdirSync(dirname(path), {recursive: true});
    if (existsSync(path) && readFileSync(path, 'utf8') === text) return;
    writeFileSync(path + '.tmp', text); renameSync(path + '.tmp', path);
}
export function emit(selected, target, out) {
    const fingerprint = createHash('sha256').update(JSON.stringify({target, plugins: selected.map(p => ({
        id: p.id, version: p.version, abi: p.abi, role: p.role, entry: p.entry, sources: p.sources, assembly: p.assembly,
        libraries: p.libraries, includes: p.includes, embeds: p.embeds, payload: p.payload}))})).digest('hex');
    let code = '#include "selection.h"\n';
    const platform = selected.find(p => p.role === 'platform'), language = selected.find(p => p.role === 'language');
    const entry = selected.find(p => p.role === 'entry'), image = selected.find(p => p.role === 'image');
    if (platform) code += `extern const neo_depth_plugin *${platform.entry}(void);\n`;
    if (language) code += `extern const neo_language_plugin ${language.entry};\n`;
    if (entry) code += `extern int ${entry.entry}(int,char **);\n`;
    code += `const neo_vm_selection *neo_build_selection(void) {\n    static neo_vm_selection selection={.target=${JSON.stringify(target)},.ids=${JSON.stringify(selected.map(p => p.id).join(' ') || 'none')},.language=${language ? '&' + language.entry : 'NULL'},.entry=${entry ? entry.entry : 'NULL'}};\n`;
    if (platform) code += `    selection.platform=${platform.entry}();\n`;
    code += '    return &selection;\n}\n';
    save(join(out, 'selection.c'), code);
    save(join(out, 'plugins.mk'), `# Generated selection; do not edit.\nPLUGIN_KEY := ${fingerprint}\nPLUGIN_SOURCES := ${[...new Set(selected.flatMap(p => p.sources))].join(' ')}\nPLUGIN_ASSEMBLY := ${selected.flatMap(p => p.assembly).join(' ')}\nPLUGIN_LIBS := ${[...new Set(selected.flatMap(p => p.libraries))].join(' ')}\nPLUGIN_ROLES := ${selected.map(p => p.role).join(' ')}\nBUNDLE_IMAGE := ${image?.payload ?? ''}\nPLUGIN_INCLUDE_DIRS := ${[...new Set(selected.flatMap(p => p.includes))].join(' ')}\nPLUGIN_EMBED_SOURCES := ${selected.flatMap(p => p.embeds).join(' ')}\n`);
    save(join(out, 'selection.json'), JSON.stringify({format: 'neo.vm-selection/1', target,
        plugins: selected.map(p => ({id: p.id, version: p.version, manifest: p.file, sources: p.sources, payload: p.payload}))}, null, 2) + '\n');
}
async function main() {
    const [command, target = 'x86_64/Windows', configPath = '.neo-config.json', catalogPath = 'input/plugins/catalog.json', out = `build/${target}`,
        ...options] = process.argv.slice(2);
    if (command === 'emit') {
        const config = existsSync(configPath) ? read(configPath) : {format: 'neo.vm-build/1', depth: false, requested: []};
        const selected = plan(config.requested?.length ? catalog(catalogPath) : new Map(), config, target);
        emit(selected, target, out); console.log('ok'); return;
    }
    if (command === 'list') {
        const plugins = catalog(catalogPath);
        for (const p of plugins.values()) console.log(`${p.id} [${p.collection}/${p.kind}] ${Object.hasOwn(p.targets, target) ? 'available' : 'unsupported'}`);
        return;
    }
    requireValue(command === 'configure', 'Expected configure, list, or emit');
    let depth = false, requested = [], plugins;
    requireValue(!options.length || (options.length === 1 && options[0] === '--no-depth') ||
        (options.length === 2 && options[0] === '--select'), 'Expected --no-depth or --select ID,ID');
    if (options.includes('--no-depth')) {
        depth = false;
    } else if (options.includes('--select')) {
        depth = true; requested = (options[options.indexOf('--select') + 1] ?? '').split(',').filter(Boolean);
    } else {
        const terminal = createInterface({input: process.stdin, output: process.stdout});
        terminal.on('SIGINT', () => terminal.close());
        async function question(prompt) {
            let closed;
            const cancelled = new Promise((_, reject) => { closed = () => reject(new Error('VM configuration cancelled before a selection was saved')); });
            terminal.once('close', closed);
            try { return await Promise.race([terminal.question(prompt), cancelled]); }
            finally { terminal.removeListener('close', closed); }
        }
        try {
            depth = /^(y|yes)$/i.test((await question('Include depth components in this VM? [y/N] ')).trim());
            if (depth) {
                plugins = catalog(catalogPath);
                const available = [...plugins.values()].filter(p => Object.hasOwn(p.targets, target));
                available.forEach((p, i) => console.log(`${i + 1}. ${p.id} [${p.collection}/${p.kind}]`));
                const answer = await question('Which plugins? Enter numbers or IDs, separated by commas: ');
                requested = answer.split(',').map(s => s.trim()).filter(Boolean).map(s => /^\d+$/.test(s) ? available[Number(s) - 1]?.id : s);
            }
        } finally { terminal.close(); }
    }
    const config = {format: 'neo.vm-build/1', depth, requested};
    const selected = plan(plugins ?? (requested.length ? catalog(catalogPath) : new Map()), config, target);
    console.log('Resolved inclusion: ' + (selected.map(p => p.id).join(', ') || 'none (bootstrap only)'));
    console.log('Dependencies are included explicitly in the saved selection.');
    save(configPath, JSON.stringify({...config, resolved: selected.map(p => ({id: p.id, version: p.version}))}, null, 2) + '\n');
    console.log('Saved ' + configPath);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main().catch(error => { console.error('neo configure: ' + error.message); process.exitCode = 1; });
}
