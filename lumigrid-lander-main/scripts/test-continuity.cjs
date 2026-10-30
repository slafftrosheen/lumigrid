#!/usr/bin/env node
// Dependency-light host tests: transpile the actual pure TypeScript modules.
// This is NOT a Svelte build or browser/physical-device test.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const cp = require('node:child_process');
let ts;
try { ts = require('typescript'); }
catch {
  const globalRoot = cp.execFileSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['root', '-g'], { encoding:'utf8' }).trim();
  ts = require(path.join(globalRoot, 'typescript'));
}
const root = path.resolve(__dirname, '..');
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'lg-continuity-'));
try {
  for (const name of ['effectCatalog', 'syncPolicy', 'sync']) {
    const filename = path.join(root, 'src/lib/api', name + '.ts');
    if (!fs.existsSync(filename)) continue;
    const result = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
      compilerOptions: { module:ts.ModuleKind.CommonJS, target:ts.ScriptTarget.ES2022, strict:true },
      fileName: filename, reportDiagnostics:true,
    });
    const errors = (result.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error);
    if (errors.length) throw new Error(ts.formatDiagnosticsWithColorAndContext(errors, {
      getCanonicalFileName:f=>f, getCurrentDirectory:()=>root, getNewLine:()=> '\n'
    }));
    fs.writeFileSync(path.join(out, name + '.cjs'), result.outputText);
    fs.writeFileSync(path.join(out, name + '.js'), result.outputText);
  }
  // Explicit SDK boundary: network calls are forbidden in these host-only tests.
  fs.writeFileSync(path.join(out, 'transport.js'), 'exports.apiJson = () => { throw new Error("Network API not available in host regression"); };');
  const selected = process.argv.slice(2);
  const tests = selected.length ? selected.map(n=>path.join(root, 'tests/continuity', n)) :
    fs.readdirSync(path.join(root, 'tests/continuity')).filter(n=>n.endsWith('.test.cjs')).map(n=>path.join(root,'tests/continuity',n));
  const result = cp.spawnSync(process.execPath, ['--test', ...tests], {
    stdio:'inherit', env:{...process.env, LG_COMPILED:out}
  });
  process.exitCode = result.status ?? 1;
} finally { fs.rmSync(out, {recursive:true, force:true}); }
