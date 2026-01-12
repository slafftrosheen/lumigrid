import { json, error } from '@sveltejs/kit';
import { readFile } from 'fs/promises';
import { marked } from 'marked';

const ROOT_DOCS = new Set(['README', 'StyleSystem', 'CONTRIBUTING']);

/** @type {import('./$types').RequestHandler} */
export async function GET({ params }) {
	const { slug } = params;
	let filePath = '';

	if (ROOT_DOCS.has(slug)) {
		filePath = `${slug}.md`;
	} else {
		filePath = `docs/${slug}.md`;
	}

	try {
		const markdown = await readFile(filePath, 'utf-8');

		// Extract title from the first H1 tag
		const titleMatch = markdown.match(/^#\s+(.*)/m);
		const title = titleMatch ? titleMatch[1] : slug.replace(/_/g, ' ');

		const content = await marked.parse(markdown);

		return json({
			title,
			content
		});
	} catch (e) {
		throw error(404, `Documentation page not found: ${slug}`);
	}
}
