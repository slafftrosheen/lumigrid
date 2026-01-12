#!/bin/bash
#
# Build webapp for ESP32 deployment
# This script builds the SvelteKit app and prepares it for SPIFFS/LittleFS
#

set -e

echo "Building LumiGrid Web UI for ESP32..."

# Check if node_modules exists
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

# Build the app
echo "Building optimized static files..."
npm run build

# Check build output
if [ ! -d "build" ]; then
    echo "Error: Build directory not found!"
    exit 1
fi

# Calculate total size
BUILD_SIZE=$(du -sh build | cut -f1)
echo "Build complete! Total size: $BUILD_SIZE"

# List files
echo ""
echo "Built files:"
find build -type f -exec ls -lh {} \; | awk '{print $9, "-", $5}'

echo ""
echo "Next steps:"
echo "1. Copy build output to firmware/assets/webapp/"
echo "2. Run 'idf.py build' in firmware directory"
echo "3. Flash firmware with filesystem image"
echo ""
echo "Quick deploy:"
echo "  cp -r build/* ../firmware/assets/webapp/"
echo "  cd ../firmware && idf.py build flash"
