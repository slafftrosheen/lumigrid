import adapter from '@sveltejs/adapter-vercel';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
  preprocess: process.env.VITEST ? undefined : vitePreprocess(),
  kit: {
    adapter: adapter(),
    paths: { base: '', relative: false },
    prerender: { 
      entries: ['/', '/style-demo', '/flasher'],
      handleHttpError: ({ path, referrer, message }) => {
        // Ignore 404 errors for removed routes or client-only dashboard
        if (path.endsWith('/cases') || path.endsWith('/contact') || path.startsWith('/dashboard')) {
          return;
        }
        throw new Error(message);
      }
    },
    alias: { $lib: 'src/lib' }
  }
};

export default config;
