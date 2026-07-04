
<script lang="ts">
	import { base } from '$app/paths';
</script>

<svelte:head>
	<title>Documentation - LumiGrid</title>
	<meta name="description" content="Centralized documentation for the LumiGrid project." />
</svelte:head>

<div class="container section">
	<h1 class="hero-title">LumiGrid Documentation</h1>
	<p class="lead">
		Welcome to the central hub for all LumiGrid documentation. Whether you're a new user, a seasoned
		developer, or a contributor, you'll find the resources you need here.
	</p>

	<div class="grid two">
		<a href="{base}/docs/README" class="card" data-halo="1">
			<h3>Project Overview</h3>
			<p>High-level introduction to the LumiGrid project, its goals, and its architecture.</p>
		</a>
		<a href="{base}/docs/API_REFERENCE" class="card" data-halo="1">
			<h3>API Reference</h3>
			<p>Complete REST, MQTT, and SSE API documentation for LumiGrid nodes.</p>
		</a>
		<a href="{base}/docs/NODE_FAMILY" class="card" data-halo="1">
			<h3>Node Family</h3>
			<p>Detailed specifications for all LumiGrid hardware node types.</p>
		</a>
		<a href="{base}/docs/EMBEDDED_INTEGRATION" class="card" data-halo="1">
			<h3>Embedded Integration</h3>
			<p>Guides for integrating LumiGrid into LED Node Board and Raspberry Pi nodes.</p>
		</a>
		<a href="{base}/docs/StyleSystem" class="card" data-halo="1">
			<h3>Style System</h3>
			<p>Design token reference, component library, and usage examples.</p>
		</a>
		<a href="{base}/docs/CONTRIBUTING" class="card" data-halo="1">
			<h3>Contributing Guide</h3>
			<p>How to contribute to the SvelteKit development and StyleSystem.</p>
		</a>
	</div>
</div>

<style>
	.hero-title {
		font-size: var(--fs-display-lg);
		margin-bottom: var(--spacing-mobile-md);
	}

	.lead {
		font-size: var(--fs-body-lg);
		color: var(--muted);
		margin-bottom: var(--spacing-mobile-xl);
		max-width: 65ch;
	}

	.card {
		display: block;
		text-decoration: none;
		color: inherit;
		transition: transform 0.3s var(--ease-default), box-shadow 0.3s var(--ease-default);
	}

	.card:hover {
		transform: translateY(-4px);
		box-shadow: var(--shadow);
	}

	.card h3 {
		font-size: var(--fs-title-1);
		margin-bottom: var(--spacing-mobile-sm);
		color: var(--ink);
	}

	.card p {
		color: var(--muted);
	}
</style>
