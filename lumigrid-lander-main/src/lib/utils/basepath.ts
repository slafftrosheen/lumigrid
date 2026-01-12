/**
 * Calculates the base path of the application at runtime.
 *
 * This function is designed to work in different deployment scenarios,
 * such as running in a subdirectory or at the root.
 *
 * @returns {string} The calculated base path for the application.
 */
export function runtimeBase(): string {
  // Check for a base URL provided by the build environment (e.g., Vite).
  const provided = (import.meta as any).env?.BASE_URL || '';
  if (provided) return provided;

  // If not in a browser environment, return an empty string.
  if (typeof location === 'undefined') return '';

  // For other environments, infer the base path from the current URL's pathname.
  const path = location.pathname;
  const match = path.match(/^\/[^/]+\/[^/]+\//);
  return match ? match[0].slice(0, -1) : '';
}
