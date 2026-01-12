<script>
  import '../app.css';
  import { page } from '$app/stores';

  const navItems = [
    { path: '/', label: 'Dashboard', icon: '🎛️' },
    { path: '/effects', label: 'Effects', icon: '✨' },
    { path: '/sequencer', label: 'Sequencer', icon: '🎬' },
    { path: '/config', label: 'Config', icon: '⚙️' },
    { path: '/docs', label: 'Docs', icon: '📚' }
  ];
</script>

<div class="min-h-screen flex flex-col">
  <!-- Navigation -->
  <nav class="navbar bg-base-200 shadow-lg">
    <div class="flex-1">
      <a href="/" class="btn btn-ghost normal-case text-xl">
        <span class="text-primary">Lumi</span>Grid
      </a>
    </div>
    <div class="flex-none">
      <ul class="menu menu-horizontal px-1">
        {#each navItems as item}
          <li>
            <a 
              href={item.path}
              class:active={$page.url.pathname === item.path}
            >
              <span class="text-lg">{item.icon}</span>
              <span class="hidden sm:inline">{item.label}</span>
            </a>
          </li>
        {/each}
      </ul>
      
      <!-- Theme Switcher -->
      <div class="dropdown dropdown-end">
        <label tabindex="0" class="btn btn-ghost btn-circle">
          🎨
        </label>
        <ul tabindex="0" class="dropdown-content z-[1] menu p-2 shadow bg-base-100 rounded-box w-52">
          <li><button data-set-theme="light">☀️ Light</button></li>
          <li><button data-set-theme="dark">🌙 Dark</button></li>
          <li><button data-set-theme="cupcake">🧁 Cupcake</button></li>
          <li><button data-set-theme="night">🌃 Night</button></li>
        </ul>
      </div>
    </div>
  </nav>

  <!-- Main Content -->
  <main class="flex-1 container mx-auto p-4">
    <slot />
  </main>

  <!-- Footer -->
  <footer class="footer footer-center p-4 bg-base-200 text-base-content">
    <div>
      <p>LumiGrid v1.0.0 - Professional LED Controller</p>
    </div>
  </footer>
</div>

<svelte:head>
  <script>
    // Theme switcher
    document.addEventListener('click', (e) => {
      if (e.target.hasAttribute('data-set-theme')) {
        const theme = e.target.getAttribute('data-set-theme');
        document.documentElement.setAttribute('data-theme', theme);
        localStorage.setItem('theme', theme);
      }
    });

    // Load saved theme
    const savedTheme = localStorage.getItem('theme') || 'night';
    document.documentElement.setAttribute('data-theme', savedTheme);
  </script>
</svelte:head>
