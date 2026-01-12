#!/bin/bash
# Build webapp and deploy to firmware for ESP32 flash
# This script builds the SvelteKit app, optimizes it, and prepares it for LittleFS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WEBAPP_DIR="$PROJECT_ROOT/webapp"
FIRMWARE_DIR="$PROJECT_ROOT/firmware"
ASSETS_DIR="$FIRMWARE_DIR/assets/webapp"

echo "=== LumiGrid WebApp Build & Deploy ==="
echo ""
echo "Webapp directory: $WEBAPP_DIR"
echo "Firmware assets:  $ASSETS_DIR"
echo ""

# Check if webapp directory exists
if [ ! -d "$WEBAPP_DIR" ]; then
    echo "❌ ERROR: Webapp directory not found at $WEBAPP_DIR"
    exit 1
fi

# Navigate to webapp directory
cd "$WEBAPP_DIR"

# Check if package.json exists
if [ ! -f "package.json" ]; then
    echo "❌ ERROR: package.json not found in webapp directory"
    exit 1
fi

# Install dependencies if node_modules doesn't exist
if [ ! -d "node_modules" ]; then
    echo "[1/4] Installing webapp dependencies..."
    npm install
else
    echo "[1/4] Dependencies already installed (node_modules exists)"
fi

# Build the webapp
echo ""
echo "[2/4] Building webapp with Vite..."
npm run build

# Check if build was successful
if [ ! -d "build" ]; then
    echo "❌ ERROR: Build directory not found after npm run build"
    exit 1
fi

# Calculate build size
BUILD_SIZE=$(du -sh build | cut -f1)
echo "✅ Build complete! Size: $BUILD_SIZE"

# List built files
echo ""
echo "Built files:"
find build -type f | while read file; do
    size=$(ls -lh "$file" | awk '{print $5}')
    echo "  $file - $size"
done

# Clean destination directory
echo ""
echo "[3/4] Preparing firmware assets directory..."
rm -rf "$ASSETS_DIR"
mkdir -p "$ASSETS_DIR"

# Copy build output to firmware assets
echo "[4/4] Deploying to firmware/assets/webapp/..."
cp -r build/* "$ASSETS_DIR/"

# List deployed files
echo ""
echo "Deployed files to $ASSETS_DIR:"
find "$ASSETS_DIR" -type f | while read file; do
    size=$(ls -lh "$file" | awk '{print $5}')
    echo "  $(basename $file) - $size"
done

echo ""
echo "==================================="
echo "✅ WebApp Build & Deploy Complete"
echo "==================================="
echo ""
echo "Next steps:"
echo "  1. Build firmware: cd firmware && idf.py build"
echo "  2. Flash firmware: cd firmware && idf.py -p /dev/ttyUSB0 flash"
echo "  3. Access web UI at http://192.168.4.1 (AP mode)"
echo ""
echo "Note: The webapp files will be included in the LittleFS partition"
echo "      when you run 'idf.py build' with littlefs_create_partition_image enabled"
echo ""
