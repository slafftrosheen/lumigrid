import { error } from '@sveltejs/kit';
import { base } from '$app/paths';
import { marked } from 'marked';
import bundle from '$lib/generated/documentation.json';

type Guide = { title: string; markdown: string; historical: boolean };
export const guides: Record<string, Guide> = bundle;

/** Resolve relative guide links before rendering, including history and anchors. */
export function documentHref(slug: string, href: string): string | null {
  if (/^(https?:|mailto:)/i.test(href) || href.startsWith('#')) return href;
  if (/^[a-z][a-z\d+.-]*:/i.test(href) || href.startsWith('//')) return null;
  const url = new URL(href, `https://docs.invalid/docs/${slug}.md`);
  const target = decodeURIComponent(url.pathname).replace(/^\/docs\//, '').replace(/\.md$/, '');
  if (Object.hasOwn(guides, target)) return `${base}/docs/${target}${url.hash}`;
  // Historical local-file links may no longer exist. Do not invent live URLs.
  return null;
}

export async function loadGuide(slug: string) {
  if (!Object.hasOwn(guides, slug)) error(404, 'Documentation page not found');
  const guide = guides[slug];
  const renderer = new marked.Renderer();
  renderer.heading = ({ tokens, depth, text }) => {
    const id = text.toLowerCase().replace(/[^\p{L}\p{N}\s-]/gu, '').trim().replace(/\s+/g, '-');
    return `<h${depth} id="${id}">${renderer.parser.parseInline(tokens)}</h${depth}>`;
  };
  renderer.link = ({ href, tokens }) => {
    const text = renderer.parser.parseInline(tokens);
    const resolved = documentHref(slug, href);
    return resolved ? `<a href="${resolved.replace(/&/g, '&amp;').replace(/"/g, '&quot;')}">${text}</a>` : text;
  };
  // Docs are repository-owned Markdown; don't render historical raw HTML/scripts.
  renderer.html = ({ text }) => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  return { title: guide.title, historical: guide.historical, content: await marked.parse(guide.markdown, { renderer }) };
}
