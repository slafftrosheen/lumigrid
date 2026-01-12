# LumiGrid Modern Web UI

Modern, responsive web interface for LumiGrid LED Controller built with SvelteKit, TailwindCSS, and DaisyUI.

## Features

- **Dashboard** - Real-time system status, LED preview, and quick controls
- **Effects Browser** - Visual effect selection with live preview (21 effects)
- **Multitrack Sequencer** - Timeline-based sequencer with 16 tracks
- **Configuration** - Device settings and network configuration
- **Responsive Design** - Works on desktop, tablet, and mobile
- **Dark/Light Themes** - Multiple theme options via DaisyUI

## Tech Stack

- **SvelteKit** - Modern web framework with SSR/SSG support
- **TailwindCSS** - Utility-first CSS framework
- **DaisyUI** - Beautiful component library for Tailwind
- **Vite** - Fast build tool optimized for ESP32 deployment

## Development

### Prerequisites

- Node.js 18+ and npm
- ESP32 device running LumiGrid firmware

### Setup

```bash
# Install dependencies
npm install

# Start development server
npm run dev

# The UI will be available at http://localhost:5173
# API calls are proxied to http://lumigrid.local
```

### Build for Production

```bash
# Build optimized static files for ESP32
npm run build

# Output will be in ./build directory
# Files are minified and compressed for ESP32 deployment
```

### Deploy to ESP32

The build output needs to be converted to a SPIFFS/LittleFS image and flashed to the ESP32:

```bash
# After building, create filesystem image
cd ../firmware
idf.py build

# The build system will package webapp/build into the filesystem partition
```

## Project Structure

```
webapp/
├── src/
│   ├── routes/           # Pages (SvelteKit file-based routing)
│   │   ├── +page.svelte          # Dashboard
│   │   ├── effects/              # Effects browser
│   │   ├── sequencer/            # Multitrack sequencer
│   │   └── config/               # Configuration
│   ├── lib/              # Reusable components
│   │   ├── LEDPreview.svelte     # LED strip visualization
│   │   ├── Timeline.svelte       # Sequencer timeline
│   │   └── api.js                # API wrapper
│   ├── stores/           # Svelte stores for state
│   ├── app.html          # HTML template
│   └── app.css           # Global styles
├── static/               # Static assets
├── package.json          # Dependencies
├── vite.config.js        # Build configuration
└── tailwind.config.js    # Tailwind/DaisyUI config
```

## API Integration

The UI communicates with the ESP32 via REST API:

- `GET /api/status` - System status and telemetry
- `GET /api/effects` - Available effects list
- `POST /api/effect/apply` - Apply effect with parameters
- `GET /api/sequences` - List sequences
- `POST /api/sequences` - Create sequence
- `POST /api/sequences/{id}/play` - Start playback
- `GET /api/leds/preview` - LED data for preview

## Customization

### Themes

Edit `tailwind.config.js` to add/modify themes:

```javascript
daisyui: {
  themes: ['light', 'dark', 'cupcake', 'night', 'your-custom-theme'],
}
```

### Colors

Customize colors in `app.css` or via DaisyUI theme variables.

### Components

Add new Svelte components in `src/lib/` and import them in pages.

## Performance

- **Bundle Size**: ~100-150KB compressed (optimized for ESP32)
- **Load Time**: <2s on local network
- **FPS**: 60fps for LED preview and timeline rendering
- **Memory**: Minimal - static files served from ESP32 flash

## Browser Support

- Chrome/Edge 90+
- Firefox 88+
- Safari 14+
- Mobile browsers (iOS Safari, Chrome Mobile)

## Development Tips

1. **Hot Reload**: Changes are reflected immediately in dev mode
2. **API Mocking**: Modify `vite.config.js` proxy for local API testing
3. **Theme Testing**: Use DaisyUI theme switcher to test all themes
4. **Responsive**: Test on different screen sizes with browser dev tools

## Troubleshooting

### API Connection Issues

If API calls fail, check:
1. ESP32 is on the same network
2. Hostname resolves (try IP address instead)
3. CORS is enabled on ESP32 (should be by default)

### Build Errors

```bash
# Clear cache and reinstall
rm -rf node_modules package-lock.json
npm install
```

### Theme Not Changing

Clear browser localStorage:
```javascript
localStorage.clear()
```

## License

Same as LumiGrid project

## Contributing

See main repository CONTRIBUTING.md
