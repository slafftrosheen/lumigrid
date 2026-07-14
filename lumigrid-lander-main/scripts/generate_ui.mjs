import fs from 'fs';
import zlib from 'zlib';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const buildDir = path.resolve(__dirname, '../build');
const indexFile = path.join(buildDir, 'index.html');
const outFile = path.resolve(__dirname, '../../LumigridLedNode/wled00/html_ui.h');

if (!fs.existsSync(indexFile)) {
  console.error(`Error: ${indexFile} not found. Run npm run build first.`);
  process.exit(1);
}

const html = fs.readFileSync(indexFile);
const gzipped = zlib.gzipSync(html);

const length = gzipped.length;
const hexArray = [];
for (let i = 0; i < length; i++) {
  hexArray.push('0x' + gzipped[i].toString(16).padStart(2, '0'));
}

// Format the hex array nicely (16 bytes per line)
const formattedHex = [];
for (let i = 0; i < hexArray.length; i += 16) {
  formattedHex.push('  ' + hexArray.slice(i, i + 16).join(', '));
}

const buildTime = Math.floor(Date.now() / 1000);

const cppCode = `/*
 * Binary array for the Web UI.
 * gzip is used for smaller size and improved speeds.
 * Generated from LumiGrid Operator Panel
 */

// Automatically generated build time for cache busting (UNIX timestamp)
#define WEB_BUILD_TIME ${buildTime}
 
const size_t PAGE_index_length = ${length};
const uint8_t PAGE_index[] PROGMEM = {
${formattedHex.join(',\n')}
};
`;

fs.writeFileSync(outFile, cppCode);
console.log(`Generated ${outFile} successfully! (Size: ${length} bytes)`);
