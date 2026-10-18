#!/usr/bin/env node
// web-smoke.mjs — bare-loader smoke test for the web artifact.
// Serves the built SDLLaunch.{js,wasm,data} over a minimal host page and drives it in headless Chromium
// (Playwright, resolved from the frontend repo). Confirms the module downloads, compiles, instantiates and
// main() runs. The host page injects no FetchFS, so the engine stops at package loading, before any WebGL:
// no GPU needed. Usage: node tools/web-smoke.mjs [artifactDir] [seconds]
import { createServer } from 'node:http';
import { readFileSync, existsSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { createRequire } from 'node:module';

const DIR = resolve(process.argv[2] || 'build-wasm/SDLLaunch');
const SECS = Number(process.argv[3] || 20);
const PORT = 8792;
// Playwright lives in the frontend repo, not this engine repo.
const require = createRequire('/home/ion/ufrontend/packages/e2e/');
const { chromium } = require('playwright');

if (!existsSync(join(DIR, 'SDLLaunch.js'))) {
    console.error(`[smoke] no SDLLaunch.js in ${DIR} — build first (cmake --build --preset wasm)`);
    process.exit(2);
}

// Minimal Emscripten host page (bare, non-MODULARIZE global Module, as the frontend's host page uses).
writeFileSync(
    join(DIR, '__smoke.html'),
    `<!doctype html><meta charset=utf-8><canvas id=canvas width=320 height=240></canvas><script>
window.__log=[];function cap(k,t){window.__log.push('['+k+'] '+t);}
var Module={canvas:document.getElementById('canvas'),arguments:[],
 print:function(t){cap('OUT',t);},printErr:function(t){cap('ERR',t);},
 onAbort:function(w){cap('ABORT',String(w));window.__abort=true;},
 locateFile:function(p){return p;},
 onRuntimeInitialized:function(){cap('SMOKE','runtimeInitialized');window.__rt=true;}};
window.Module=Module;
</script><script src="SDLLaunch.js"></script>`,
);

const MIME = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.wasm': 'application/wasm',
    '.data': 'application/octet-stream',
};
const server = createServer((req, res) => {
    let p = req.url.split('?')[0];
    if (p === '/') p = '/__smoke.html';
    const f = join(DIR, p);
    if (!existsSync(f)) {
        res.writeHead(404);
        res.end('nf');
        return;
    }
    res.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream' });
    res.end(readFileSync(f));
});
await new Promise((r) => server.listen(PORT, r));

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage();
const console_lines = [];
page.on('console', (m) => console_lines.push(m.text()));
page.on('pageerror', (e) => console_lines.push('[PAGEERROR] ' + e.message));
await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
await page.waitForTimeout(SECS * 1000);

const state = await page.evaluate(() => {
    const out = { rt: !!window.__rt, abort: !!window.__abort, log: window.__log || [], memfs: [], engineLog: null };
    try {
        const FS = window.FS || (window.Module && window.Module.FS);
        if (FS) {
            const walk = (d) => {
                for (const n of FS.readdir(d)) {
                    if (n === '.' || n === '..') continue;
                    const p = d === '/' ? '/' + n : d + '/' + n;
                    let mode = 0;
                    try {
                        mode = FS.stat(p).mode;
                    } catch {}
                    out.memfs.push(p);
                    if (FS.isDir(mode) && out.memfs.length < 200) walk(p);
                }
            };
            walk('/');
            const logPath = out.memfs.find((p) => /\.log$/i.test(p));
            if (logPath) out.engineLog = new TextDecoder().decode(FS.readFile(logPath));
        }
    } catch (e) {
        out.fsErr = String(e);
    }
    return out;
});
await browser.close();
server.close();

const engineLog = state.log.filter((l) =>
    /(Log|Init|Warning|Exit|DevNet|Bound|Unreal|Version|Package|Core|appThrowf|MissingIni|Detected)/i.test(l),
);
console.log(`\n=== web-smoke: ${DIR} ===`);
console.log(`runtimeInitialized: ${state.rt}`);
console.log(`abort: ${state.abort}`);
console.log(`Module.print/err lines: ${state.log.length}`);
console.log(`MEMFS entries: ${state.memfs.length}${state.fsErr ? ' (FS err: ' + state.fsErr + ')' : ''}`);
if (state.engineLog) {
    const lines = state.engineLog.split('\n');
    console.log(`--- engine log file (${lines.length} lines; first 45) ---`);
    console.log(lines.slice(0, 45).join('\n'));
} else {
    console.log('--- no engine .log file in MEMFS; stdout lines: ---');
    console.log(state.log.slice(0, 30).join('\n'));
}
if (console_lines.some((l) => l.includes('PAGEERROR'))) {
    console.log('--- page errors ---');
    console.log(console_lines.filter((l) => l.includes('PAGEERROR')).join('\n'));
}
// Pass: the module instantiated and the engine executed. With only the boot config preloaded and no
// FetchFS, the expected boundary is package resolution failing (Engine.u/Core.u are not in the FS), which
// the [appThrowf] line names.
const reachedPackageLoad = state.log.some((l) => /appThrowf|PackageNotFound|MissingIni|Package/i.test(l));
const engineRan = !!state.engineLog || engineLog.length > 0 || reachedPackageLoad;
const pass = state.rt && engineRan;
console.log(
    `\n[smoke] ${pass ? 'PASS' : 'FAIL'} — runtimeInitialized=${state.rt} engineExecuted=${engineRan}` +
        (reachedPackageLoad ? ' (reached package loading = the expected pre-FetchFS boundary)' : ''),
);
process.exit(pass ? 0 : 1);
