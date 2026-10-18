#!/usr/bin/env node
// emit-artifact-meta.mjs — write <out>/meta.json for the just-linked WASM artifact.
// Run by the launcher's POST_BUILD after SDLLaunch.js is final, so the checksums are of the served files:
//   node emit-artifact-meta.mjs --out=<dir> --target=ut99 --abi=<n> [--gitSha=…] [--builtAt=…] [--emcc=…] [--dep-<label>=…]
// Node stdlib only. The frontend's build validates the descriptor before assembling the served tree.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = Object.fromEntries(
    process.argv.slice(2).map((a) => {
        const i = a.indexOf('=');
        return [a.slice(2, i), a.slice(i + 1)];
    }),
);
const dir = args.out;

// Provenance is resolved at build time: the values on the POST_BUILD command line are computed once per
// configure. They remain the fallback for a tree without git.
const _here = dirname(fileURLToPath(import.meta.url));
const gitSha = (() => {
    try {
        return execFileSync('git', ['-C', _here, 'rev-parse', '--short', 'HEAD'], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore'],
        }).trim();
    } catch {
        return args.gitSha || 'unknown';
    }
})();
const builtAt = new Date().toISOString().replace(/\.\d+Z$/, 'Z');

const sha = (f) =>
    existsSync(join(dir, f)) ? 'sha256-' + createHash('sha256').update(readFileSync(join(dir, f))).digest('hex') : null;

const checksums = { wasm: sha('SDLLaunch.wasm'), js: sha('SDLLaunch.js') };
const dataSha = sha('SDLLaunch.data'); // optional (only if --preload-file produced it)
if (dataSha) checksums.data = dataSha;

// Every fetched library as `<repo>@<branch|tag>` (a branch tip is what a rebuild takes), handed over as
// --dep-<label>=… from the cache variables FetchContent read (cmake/ArtifactDeps.cmake). A GitHub URL is
// shortened to owner/name; a local checkout override arrives as `local:<path>` and is kept verbatim.
const depSpec = (v) => v.replace(/^https:\/\/github\.com\/([^/]+)\/([^/@]+?)(?:\.git)?(@.*)$/, '$1/$2$3');
const deps = Object.fromEntries(
    Object.entries(args)
        .filter(([k]) => k.startsWith('dep-'))
        .map(([k, v]) => [k.slice(4), depSpec(v)]),
);

const meta = {
    targetId: args.target || 'ut99',
    engineFamily: 'UE1',
    buildConfig: 'wasm-integrated',
    abiVersion: Number(args.abi), // from the vendored frozen ue_abi.h UE_ABI_VERSION
    loaderStyle: 'global-module', // classic <script> + bare globals
    provenance: { repo: 'engine-ut99', gitSha, builtAt },
    toolchain: { emscripten: args.emcc || 'unknown', base: 'maximqaxd/ut99dc (UnrealTournament v400)', ...deps },
    buildFlags: [
    // Hand-maintained: keep it in step with the launcher's link options. The threading set makes this a
    // threaded-only artifact; the host checks for SharedArrayBuffer and OffscreenCanvas.
    '-sASYNCIFY', '-sFULL_ES2', '-sUSE_SDL=2', '-sALLOW_MEMORY_GROWTH', '-sFORCE_FILESYSTEM', '-sWASMFS',
    '-pthread', '-sPROXY_TO_PTHREAD', '-sOFFSCREENCANVAS_SUPPORT', '-sOFFSCREENCANVASES_TO_PTHREAD=#canvas',
  ],
    exports: { symbolContract: 1 },
    checksums,
};

writeFileSync(join(dir, 'meta.json'), JSON.stringify(meta, null, 2) + '\n');
console.log(`[meta] wrote ${join(dir, 'meta.json')} (abi ${meta.abiVersion}, ${meta.buildConfig}, ${meta.loaderStyle})`);
