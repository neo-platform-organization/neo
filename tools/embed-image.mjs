import {readFileSync, writeFileSync} from 'node:fs';
const [source, output, object = 'surface-exercise', mode = 'surface'] = process.argv.slice(2);
if (!source || !output || !['surface', 'cli'].includes(mode)) { throw new Error('embed-image SOURCE OUTPUT OBJECT surface|cli'); }
const text = readFileSync(source, 'utf8');
writeFileSync(output, `/* Generated from the canonical source; do not edit. */\n` +
    `static const char neo_bundle_source[] = ${JSON.stringify(text).replace(/\\u0000/g, '\\0')};\n` +
    `static const char neo_bundle_object[] = ${JSON.stringify(object)};\n` +
    `static const char neo_bundle_mode[] = ${JSON.stringify(mode)};\n`);
