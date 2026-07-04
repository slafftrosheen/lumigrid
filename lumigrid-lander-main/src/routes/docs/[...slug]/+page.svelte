
<script lang="ts">
	import { base } from '$app/paths';
	import { onMount } from 'svelte';

	interface DocData {
		title: string;
		content: string;
	}

	export let data: DocData;

	let contentEl: HTMLElement;

	onMount(() => {
		if (contentEl) {
			const links = contentEl.querySelectorAll('a');
			links.forEach((link) => {
				const href = link.getAttribute('href');
				if (href && href.startsWith('./')) {
					const newHref = `${base}/docs/${href.slice(2).replace('.md', '')}`;
					link.setAttribute('href', newHref);
				}
			});
		}
	});
</script>

<svelte:head>
	<title>{data.title} - LumiGrid Docs</title>
	<meta name="description" content="Documentation for {data.title}" />
</svelte:head>

<div class="container section">
	<a href="{base}/docs" class="back-link">← Back to Documentation</a>
	<div class="doc-content" bind:this={contentEl}>
		{@html data.content}
	</div>
</div>

<style>
	.back-link {
		display: inline-block;
		margin-bottom: var(--spacing-mobile-lg);
		color: var(--muted);
		text-decoration: none;
		transition: color 0.2s;
	}

	.back-link:hover {
		color: var(--ink);
	}

	.doc-content {
		max-width: 80ch;
	}

	:global(.doc-content h1) {
		font-size: var(--fs-display-lg);
		margin-bottom: var(--spacing-mobile-md);
	}

	:global(.doc-content h2) {
		font-size: var(--fs-title-1);
		margin-top: var(--spacing-mobile-xl);
		margin-bottom: var(--spacing-mobile-md);
		border-bottom: 1px solid rgba(255, 255, 255, 0.1);
		padding-bottom: var(--spacing-mobile-xs);
	}

	:global(.doc-content a) {
		color: var(--accent-primary);
		text-decoration: none;
	}

	:global(.doc-content a:hover) {
		text-decoration: underline;
	}

	:global(.doc-content pre) {
		background-color: rgba(0, 0, 0, 0.2);
		padding: var(--spacing-mobile-md);
		border-radius: var(--radius-md);
		overflow-x: auto;
	}

	:global(.doc-content code) {
		font-family: 'SF Mono', 'Courier New', monospace;
	}

	:global(.doc-content table) {
		width: 100%;
		border-collapse: collapse;
		margin-top: var(--spacing-mobile-md);
		margin-bottom: var(--spacing-mobile-md);
	}

	:global(.doc-content th, .doc-content td) {
		border: 1px solid rgba(255, 255, 255, 0.1);
		padding: var(--spacing-mobile-sm);
		text-align: left;
	}

	:global(.doc-content th) {
		background-color: rgba(255, 255, 255, 0.05);
	}
</style>
