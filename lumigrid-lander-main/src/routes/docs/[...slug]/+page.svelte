<script lang="ts">
  import { base } from '$app/paths';
  export let data: { title: string; content: string; historical: boolean };
</script>

<svelte:head>
  <title>{data.title} · LumiGrid docs</title>
  <meta name="description" content={`LumiGrid guide: ${data.title}`} />
  {#if data.historical}<meta name="robots" content="noindex" />{/if}
</svelte:head>

<div class="container section" lang="en">
  <a href={`${base}/docs`}>← Documentation</a>
  {#if data.historical}<aside class="history">Historical record. Results and instructions below apply to an earlier revision. <a href={`${base}/docs`}>Use the current guides.</a></aside>{/if}
  <article class="doc-content">{@html data.content}</article>
</div>

<style>
  .doc-content { max-width:90ch; line-height:1.75; overflow-wrap:anywhere; }
  .history { padding:1rem; margin-block:1.5rem; border:1px solid currentColor; }
  :global(.doc-content h1) { font-size:clamp(2rem,4vw,3rem); line-height:1.2; margin-block:1.5rem; }
  :global(.doc-content h2) { margin-top:2.5rem; }
  :global(.doc-content h3) { margin-top:1.8rem; }
  :global(.doc-content pre) { overflow:auto; padding:1rem; background:#ffffff0a; border:1px solid #ffffff26; border-radius:.5rem; }
  :global(.doc-content table) { display:block; max-width:100%; overflow:auto; border-collapse:collapse; margin-block:1.5rem; }
  :global(.doc-content th), :global(.doc-content td) { border:1px solid #ffffff33; padding:.6rem .8rem; text-align:left; vertical-align:top; }
  :global(.doc-content li) { margin-block:.4rem; }
  :global(.doc-content a) { text-decoration:underline; text-underline-offset:.2em; }
  :global(.doc-content blockquote) { border-left:3px solid var(--muted); margin-inline:0; padding-left:1rem; }
</style>
