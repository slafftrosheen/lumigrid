# LumiGrid Display Node UI

This directory contains the source code for the SvelteKit web interface used to control and manage the LumiGrid Display Node.

## Development

The SvelteKit UI is served by the main Python/Flask application. To develop the UI, you can run a local development server that will proxy API requests to the running display node.

### Prerequisites

- Node.js 18+
- npm

### Setup and Running

1.  **Install dependencies:**
    ```bash
    npm install
    ```

2.  **Run the development server:**
    ```bash
    npm run dev
    ```

This will start a development server on `http://localhost:5173`. The UI will make API calls to the Python backend, which should be running separately.

## Building for Production

To build the static UI assets for production, run the following command:

```bash
npm run build
```

The build output will be located in the `build` directory. The main Flask application is configured to serve the static files from this directory.
