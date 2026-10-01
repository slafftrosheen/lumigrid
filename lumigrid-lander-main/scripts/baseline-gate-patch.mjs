/**
 * Baseline gate fix: @sveltejs/adapter-vercel requires Windows symlink privilege
 * (SeCreateSymbolicLinkPrivilege), which this machine does not have (EPERM on all
 * fs.symlinkSync 'file'/'dir' types). Junctions do work.
 *
 * This patch wraps the three adapter symlink sites with a fallback that retries
 * with a 'junction' symlink on win32 when the regular call fails with EPERM.
 * Junctions behave like directory symlinks for local Vercel output purposes;
 * they are never committed and this script is idempotent.
 *
 * Run: node scripts/baseline-gate-patch.mjs
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const adapterPath = path.resolve(__dirname, '../node_modules/@sveltejs/adapter-vercel/index.js');

const wrap = `
function symlinkWithJunctionFallback(target, linkPath, type) {
	try {
		fs.symlinkSync(target, linkPath, type);
	} catch (error) {
		if (process.platform === 'win32' && (error.code === 'EPERM' || error.code === 'EACCES')) {
			fs.symlinkSync(target, linkPath, 'junction');
		} else {
			throw error;
		}
	}
}
`;

const replacements = [
	{
		find: "fs.symlinkSync(relative, `${base}.func`);",
		replace: "symlinkWithJunctionFallback(relative, `${base}.func`, 'file');",
	},
	{
		find: "fs.symlinkSync(`../${relative}`, `${base}/__data.json.func`);",
		replace: "symlinkWithJunctionFallback(`../${relative}`, `${base}/__data.json.func`, 'file');",
	},
	{
		find: 'fs.symlinkSync(relative_for_main, main_symlink_path);',
		replace: "symlinkWithJunctionFallback(relative_for_main, main_symlink_path, 'file');",
	},
	{
		find: 'fs.symlinkSync(relative_for_data, data_symlink_path);',
		replace: "symlinkWithJunctionFallback(relative_for_data, data_symlink_path, 'file');",
	},
	{
		find: "fs.symlinkSync(path.relative(path.dirname(dest), realdest), dest, is_dir ? 'dir' : 'file');",
		replace: "symlinkWithJunctionFallback(path.relative(path.dirname(dest), realdest), dest, is_dir ? 'dir' : 'file');",
	},
];

if (!fs.existsSync(adapterPath)) {
	console.error(`adapter-vercel not found at ${adapterPath}`);
	process.exit(1);
}

let source = fs.readFileSync(adapterPath, 'utf8');
let changes = 0;

for (const { find, replace } of replacements) {
	if (source.includes(replace)) {
		changes += 1;
		continue;
	}
	if (!source.includes(find)) {
		console.warn(`warning: pattern not found (adapter may have changed): ${find}`);
		continue;
	}
	source = source.split(find).join(replace);
	changes += 1;
}

if (!source.includes('symlinkWithJunctionFallback')) {
	console.error('warning: no junction-fallback references were written');
	process.exit(1);
}

// Insert the helper right after the first import block.
if (!source.includes('function symlinkWithJunctionFallback')) {
	const anchor = source.indexOf('\n\nconst');
	if (anchor === -1) {
		console.error('warning: could not locate insertion anchor for helper');
		process.exit(1);
	}
	source = source.slice(0, anchor) + '\n' + wrap + source.slice(anchor);
}

fs.writeFileSync(adapterPath, source, 'utf8');
console.log(`baseline-gate-patch: applied ${changes}/${replacements.length} junction fallbacks to adapter-vercel`);
