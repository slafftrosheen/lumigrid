/**
 * Test-only harness for asynchronous Svelte page workflows.
 *
 * Execute the selected production function declarations with controlled state
 * and API dependencies. This tests their actual await/ownership behavior; it
 * does not emulate Svelte rendering or replace browser component checks.
 */
import { readFileSync } from 'node:fs';
import ts from 'typescript';

export function loadPageFunctions<T extends Record<string, (...args: any[]) => any>>(
  fileUrl: URL,
  names: string[],
  context: Record<string, unknown>,
): T {
  const source = readFileSync(fileUrl, 'utf8');
  const script = source.match(/<script\b[^>]*>([\s\S]*?)<\/script>/)?.[1];
  if (!script) throw new Error(`No page script in ${fileUrl.pathname}`);
  const ast = ts.createSourceFile(fileUrl.pathname, script, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
  const functions = ast.statements.filter(ts.isFunctionDeclaration);
  const selected = names.map((name) => {
    const declaration = functions.find((fn) => fn.name?.text === name);
    if (!declaration) throw new Error(`Page function ${name} is missing from ${fileUrl.pathname}`);
    return declaration.getText(ast);
  });
  const compiled = ts.transpileModule(selected.join('\n'), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.None },
  }).outputText;
  return new Function('context', `with (context) { ${compiled}\nreturn { ${names.join(', ')} }; }`)(context) as T;
}
