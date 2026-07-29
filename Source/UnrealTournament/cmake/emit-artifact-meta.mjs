#!/usr/bin/env node
// emit-artifact-meta.mjs — write <out>/meta.json for the just-linked WASM artifact (docs/05 §4).
// Run by the UnrealTournament launcher's POST_BUILD, AFTER SDLLaunch.js is finalized, so the js checksum is
// of the served file. Self-contained (node stdlib only — no monorepo dep; the artifact + its descriptor
// travel together across the public/private seam). The monorepo's packages/build VALIDATES this against
// the glue ABI_VERSION + the host loader contract before assembling serve-web.
// (ufront step 2.7b, engine-ut99 — mirrors engine-unreal1's script, adapted for the "Launch.*" basenames.)
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const args = Object.fromEntries(
    process.argv.slice(2).map((a) => {
        const i = a.indexOf('=');
        return [a.slice(2, i), a.slice(i + 1)];
    }),
);
const dir = args.out;
const sha = (f) =>
    existsSync(join(dir, f)) ? 'sha256-' + createHash('sha256').update(readFileSync(join(dir, f))).digest('hex') : null;

const checksums = { wasm: sha('SDLLaunch.wasm'), js: sha('SDLLaunch.js') };
const dataSha = sha('SDLLaunch.data'); // optional (only if --preload-file produced it)
if (dataSha) checksums.data = dataSha;

const meta = {
    targetId: args.target || 'ut99',
    engineFamily: 'UE1',
    buildConfig: 'wasm-integrated',
    abiVersion: Number(args.abi), // from the vendored frozen ue_abi.h UE_ABI_VERSION
    loaderStyle: 'global-module', // classic <script> + bare globals (like engine-ut2004 / engine-unreal1)
    provenance: { repo: 'engine-ut99', gitSha: args.gitSha || 'unknown', builtAt: args.builtAt || 'unknown' },
    toolchain: { emscripten: args.emcc || 'unknown', base: 'maximqaxd/ut99dc (UnrealTournament v400)' },
    buildFlags: [
    // Descriptive, hand-maintained: keep it in step with the linker block or the descriptor
    // quietly misstates what shipped. -sWASMFS landed in 2.13.1 and the threading set in 2.13.2,
    // which makes this a THREADED-ONLY artifact (decision D2) — the host capability-gates on
    // SharedArrayBuffer + OffscreenCanvas, so that is exactly what the descriptor must say.
    '-sASYNCIFY', '-sFULL_ES2', '-sUSE_SDL=2', '-sALLOW_MEMORY_GROWTH', '-sFORCE_FILESYSTEM', '-sWASMFS',
    '-pthread', '-sPROXY_TO_PTHREAD', '-sOFFSCREENCANVAS_SUPPORT', '-sOFFSCREENCANVASES_TO_PTHREAD=#canvas',
  ],
    exports: { symbolContract: 1 },
    checksums,
};

writeFileSync(join(dir, 'meta.json'), JSON.stringify(meta, null, 2) + '\n');
console.log(`[meta] wrote ${join(dir, 'meta.json')} (abi ${meta.abiVersion}, ${meta.buildConfig}, ${meta.loaderStyle})`);
