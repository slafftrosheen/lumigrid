# Asset Guide

This document provides guidelines for managing and updating the visual assets used in the LumiGrid lander.

## Hero Images

Hero images are displayed in the auto-cycling showcase on the homepage.

- **Location**: `/static/assets/hero/`
- **Formats**: `.jpg`, `.jpeg`, `.png`, `.webp`, `.avif`
- **Behavior**: The `HeroShowcase.svelte` component automatically discovers and cycles through all images in this directory.

### How to Update

1.  **Add or Replace Images**: Place your desired hero images in the `/static/assets/hero/` directory.
2.  **No Code Changes Needed**: The component will automatically pick up the new images.

## OG/Twitter Share Images

These images are used for social media sharing previews.

-   **Location**: `/static/assets/press/`
-   **Size**: 1200x630px (for optimal display)
-   **Naming Convention**:
    -   `og-led-node.jpg`: Home page
    -   `og-led-node-cases.jpg`: Cases page
    -   `og-led-node-contact.jpg`: Contact page

### How to Update

1.  **Create Images**: Design new images with the specified dimensions.
2.  **Replace Files**: Overwrite the existing files in the `/static/assets/press/` directory with your new images, maintaining the same file names.
3.  **Update Page Metadata**: In the relevant `+page.ts` file for each route, ensure the `ogImage` property points to the correct image path.

    ```typescript
    // src/routes/+page.ts
    export const load = () => ({
      theme: { hue: 300 },
      ogImage: '/assets/press/og-led-node.jpg'
    });
    ```
