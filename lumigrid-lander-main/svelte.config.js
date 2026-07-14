import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
  preprocess: vitePreprocess(),
  kit: {
    adapter: adapter({ fallback: 'index.htm' }),
    paths: { base: '', relative: false },
    prerender: { 
      entries: ['/', '/style-demo', '/flasher'],
      handleMissingId: 'ignore',
      handleUnseenRoutes: 'ignore',
      handleHttpError: ({ path, referrer, message }) => {
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
