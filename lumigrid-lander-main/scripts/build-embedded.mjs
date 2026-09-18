/**
 * Single-source embedded dashboard asset generator.
 * Reads the embedded UI source, compresses it with gzip level 9,
 * and generates LumigridLedNode/generated/dashboard_assets.hpp with C++ byte array.
 */

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const workspaceRoot = path.resolve(__dirname, '../..');
const embeddedHtmlSource = path.resolve(__dirname, '../embedded/dashboard.html');
const fallbackHtmlSource = path.resolve(workspaceRoot, 'LumigridLedNode/main/dashboard.html');

let inputHtmlPath = embeddedHtmlSource;
if (!fs.existsSync(inputHtmlPath)) {
  if (fs.existsSync(fallbackHtmlSource)) {
    inputHtmlPath = fallbackHtmlSource;
  } else {
    console.error(`Error: Cannot find dashboard source at ${embeddedHtmlSource}`);
    process.exit(1);
  }
}

console.log(`Reading embedded dashboard source from: ${inputHtmlPath}`);
const htmlContent = fs.readFileSync(inputHtmlPath);
const compressed = zlib.gzipSync(htmlContent, { level: 9 });

const bytesPerLine = 16;
const hexLines = [];
for (let i = 0; i < compressed.length; i += bytesPerLine) {
  const chunk = compressed.subarray(i, i + bytesPerLine);
  const line = Array.from(chunk)
    .map((b) => '0x' + b.toString(16).padStart(2, '0'))
    .join(', ');
  hexLines.push('    ' + line + ',');
}

const hppContent = `// AUTO-GENERATED. DO NOT EDIT.
#pragma once
#include <cstddef>
#include <cstdint>

static const uint8_t dashboard_html_gz[] = {
${hexLines.join('\n')}
};

static const size_t dashboard_html_gz_len = ${compressed.length};
`;

const targets = [
  path.resolve(workspaceRoot, 'LumigridLedNode/generated/dashboard_assets.hpp'),
  path.resolve(workspaceRoot, 'LumigridLedNode/components/factory/include/dashboard_assets.hpp'),
];

for (const target of targets) {
  const targetDir = path.dirname(target);
  if (!fs.existsSync(targetDir)) {
    fs.mkdirSync(targetDir, { recursive: true });
  }
  fs.writeFileSync(target, hppContent, 'utf-8');
  console.log(`Generated: ${target} (${htmlContent.length} bytes -> ${compressed.length} bytes gzipped)`);
}

console.log('Embedded dashboard assets generated successfully.');
