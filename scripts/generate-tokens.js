const fs = require('fs');
const path = require('path');

const DESIGN_DIR = path.join(__dirname, '../design');
const LANDER_OUT = path.join(__dirname, '../lumigrid-lander-main/src/lib/tokens.ts');
const NODE_OUT = path.join(__dirname, '../LumigridLedNode/main/html/css/tokens.css');

function loadJson(filename) {
    return JSON.parse(fs.readFileSync(path.join(DESIGN_DIR, filename), 'utf8'));
}

const tokens = loadJson('tokens.json');
const typography = loadJson('typography.json');
const motion = loadJson('motion.json');

const mergedTokens = {
    ...tokens,
    ...typography,
    ...motion
};

// 1. Generate Svelte tokens.ts
const tsContent = `/**
 * Design Tokens System
 * Generated from /design tokens. Do not edit directly.
 */

export const tokens = ${JSON.stringify(mergedTokens, null, 2)};

export type Tokens = typeof tokens;
`;

fs.writeFileSync(LANDER_OUT, tsContent, 'utf8');
console.log(`Generated ${LANDER_OUT}`);

// 2. Generate Embedded CSS Variables
function flattenObj(obj, prefix = '') {
    let result = '';
    for (const [key, value] of Object.entries(obj)) {
        if (typeof value === 'object' && value !== null) {
            result += flattenObj(value, prefix ? `${prefix}-${key}` : key);
        } else {
            // Convert camelCase to kebab-case
            const kebabKey = key.replace(/([a-z0-9]|(?=[A-Z]))([A-Z])/g, '$1-$2').toLowerCase();
            const fullKey = prefix ? `${prefix}-${kebabKey}` : kebabKey;
            result += `  --${fullKey}: ${value};\n`;
        }
    }
    return result;
}

const cssContent = `/* 
 * Design Tokens System
 * Generated from /design tokens. Do not edit directly.
 */
:root {
${flattenObj(mergedTokens)}
}
`;

// Ensure css directory exists in LumigridLedNode
const cssDir = path.dirname(NODE_OUT);
if (!fs.existsSync(cssDir)) {
    fs.mkdirSync(cssDir, { recursive: true });
}

fs.writeFileSync(NODE_OUT, cssContent, 'utf8');
console.log(`Generated ${NODE_OUT}`);
