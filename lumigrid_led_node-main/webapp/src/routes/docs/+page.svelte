<script>
  import { onMount } from 'svelte';
  import Overview from './sections/Overview.svelte';
  import SetupGuide from './sections/SetupGuide.svelte';
  import ApiReference from './sections/ApiReference.svelte';
  import HardwareGuide from './sections/HardwareGuide.svelte';
  import NetworkingGuide from './sections/NetworkingGuide.svelte';
  import Troubleshooting from './sections/Troubleshooting.svelte';
  import DeveloperGuide from './sections/DeveloperGuide.svelte';
  
  let activeSection = 'overview';
  
  const sections = [
    { id: 'overview', title: 'Overview', icon: '📚', component: Overview },
    { id: 'setup', title: 'Setup Guide', icon: '🚀', component: SetupGuide },
    { id: 'api', title: 'API Reference', icon: '📡', component: ApiReference },
    { id: 'hardware', title: 'Hardware', icon: '🔌', component: HardwareGuide },
    { id: 'networking', title: 'Networking', icon: '🌐', component: NetworkingGuide },
    { id: 'troubleshooting', title: 'Troubleshooting', icon: '🔧', component: Troubleshooting },
    { id: 'developer', title: 'Developer Guide', icon: '💻', component: DeveloperGuide }
  ];
  
  onMount(() => {
    const hash = window.location.hash.slice(1);
    if (hash && sections.find(s => s.id === hash)) {
      activeSection = hash;
    }
  });
  
  function setSection(id) {
    activeSection = id;
    window.location.hash = id;
    window.scrollTo(0, 0);
  }
  
  $: currentSection = sections.find(s => s.id === activeSection);
</script>

<svelte:head>
  <title>Documentation - LumiGrid</title>
</svelte:head>

<div class="flex flex-col lg:flex-row gap-4 h-full">
  <!-- Sidebar Navigation -->
  <div class="lg:w-64 flex-shrink-0">
    <div class="card bg-base-200 shadow-xl sticky top-4">
      <div class="card-body p-4">
        <h2 class="card-title text-lg mb-2">Documentation</h2>
        <ul class="menu menu-compact p-0">
          {#each sections as section}
            <li>
              <button
                class:active={activeSection === section.id}
                on:click={() => setSection(section.id)}
              >
                <span class="text-lg">{section.icon}</span>
                {section.title}
              </button>
            </li>
          {/each}
        </ul>
      </div>
    </div>
  </div>

  <!-- Main Content -->
  <div class="flex-1">
    <div class="card bg-base-100 shadow-xl">
      <div class="card-body prose max-w-none">
        {#if currentSection}
          <svelte:component this={currentSection.component} />
        {/if}
      </div>
    </div>
  </div>
</div>

<style>
  :global(.prose h1) {
    @apply text-3xl font-bold mt-0 mb-4;
  }
  
  :global(.prose h2) {
    @apply text-2xl font-semibold mt-6 mb-3;
  }
  
  :global(.prose h3) {
    @apply text-xl font-semibold mt-4 mb-2;
  }
  
  :global(.prose code) {
    @apply bg-base-200 px-1 py-0.5 rounded text-sm;
  }
  
  :global(.prose pre) {
    @apply bg-base-200 p-4 rounded-lg overflow-x-auto;
  }
  
  :global(.prose pre code) {
    @apply bg-transparent p-0;
  }
  
  :global(.prose table) {
    @apply w-full;
  }
  
  :global(.prose a) {
    @apply link link-primary;
  }
  
  :global(.prose ul) {
    @apply list-disc list-inside;
  }
  
  :global(.prose ol) {
    @apply list-decimal list-inside;
  }
  
  :global(.prose .lead) {
    @apply text-lg opacity-80 mb-4;
  }
</style>
