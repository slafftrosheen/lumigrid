<script>
  import '$lib/styles.css';
  import LumenField from '$lib/components/LumenField.svelte';
  import BackgroundFlare from '$lib/components/BackgroundFlare.svelte';
  import TopProgress from '$lib/components/TopProgress.svelte';
  import LightSettings from '$lib/components/LightSettings.svelte';
  import SkipLink from '$lib/components/SkipLink.svelte';
  import HaloCanvas from '$lib/components/HaloCanvas.svelte';
  import { base } from '$app/paths';
</script>

<svelte:head>
  <meta property="og:type" content="website" />
  <meta property="og:site_name" content="LumiGrid" />
  <meta name="twitter:card" content="summary" />
  <link rel="stylesheet" href="{base}/print.css" media="print">
</svelte:head>

<SkipLink />
<BackgroundFlare />
<HaloCanvas />
<LumenField intensity={1.0} parallax={0.06}/>
<TopProgress />
<LightSettings />
<main id="main" tabindex="-1">
  <slot />
</main>
