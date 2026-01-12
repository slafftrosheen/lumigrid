#!/bin/bash
# Installation script for Lumigrid Display Node
# Supports Raspberry Pi OS Bookworm (64-bit) and later

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/venv"

echo "==================================="
echo "Lumigrid Display Node Installation"
echo "==================================="
echo ""

# Check if running on Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    echo "Warning: This doesn't appear to be a Raspberry Pi"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Update system
echo "Updating system packages..."
sudo apt-get update

# Install system dependencies
# Note: Cython3 and build-essential are required for compiling rpi-rgb-led-matrix Python bindings
# libgpiod2 is required for the CircuitPython DHT sensor library
echo "Installing system dependencies..."
sudo apt-get install -y \
    python3-pip \
    python3-dev \
    python3-venv \
    python3-pillow \
    build-essential \
    cython3 \
    libatlas-base-dev \
    libgraphicsmagick++-dev \
    libgpiod2 \
    libboost-python-dev \
    libboost-system-dev \
    git \
    i2c-tools

# Create Python virtual environment
echo "Creating Python virtual environment..."
if [ -d "${VENV_DIR}" ]; then
    echo "Virtual environment already exists at ${VENV_DIR}"
else
    python3 -m venv "${VENV_DIR}"
    echo "Created virtual environment at ${VENV_DIR}"
fi

# Activate virtual environment
source "${VENV_DIR}/bin/activate"
echo "Activated virtual environment"

# Upgrade pip in virtual environment
echo "Upgrading pip..."
pip install --upgrade pip setuptools wheel

# Install hzeller's RGB LED Matrix library
echo "Installing RGB LED Matrix library..."
if [ ! -d "${SCRIPT_DIR}/rpi-rgb-led-matrix" ]; then
    cd "${SCRIPT_DIR}"
    git clone --recursive https://github.com/hzeller/rpi-rgb-led-matrix.git
fi

cd "${SCRIPT_DIR}/rpi-rgb-led-matrix"

# Build the core library first
echo "Building RGB LED Matrix core library..."
make

# Build and install Python bindings into the virtual environment
echo "Building and installing Python bindings..."
cd bindings/python
make build-python PYTHON="${VENV_DIR}/bin/python3"
"${VENV_DIR}/bin/pip" install .

cd "${SCRIPT_DIR}"

# Enable I2C for RTC and sensors
echo "Enabling I2C..."
# Check for both legacy and new config locations
if [ -f /boot/config.txt ]; then
    BOOT_CONFIG="/boot/config.txt"
elif [ -f /boot/firmware/config.txt ]; then
    BOOT_CONFIG="/boot/firmware/config.txt"
else
    echo "Warning: Could not find boot config file"
    BOOT_CONFIG=""
fi

if [ -n "${BOOT_CONFIG}" ]; then
    if ! grep -q "^dtparam=i2c_arm=on" "${BOOT_CONFIG}"; then
        echo "dtparam=i2c_arm=on" | sudo tee -a "${BOOT_CONFIG}"
    fi

    # Enable 1-Wire for DS18B20 temperature sensor
    echo "Enabling 1-Wire interface..."
    if ! grep -q "^dtoverlay=w1-gpio" "${BOOT_CONFIG}"; then
        echo "dtoverlay=w1-gpio,gpiopin=4" | sudo tee -a "${BOOT_CONFIG}"
    fi
fi

# Install Python dependencies into virtual environment
echo "Installing Python dependencies..."
pip install -r "${SCRIPT_DIR}/requirements.txt"

# Create config file from example if it doesn't exist
if [ ! -f "${SCRIPT_DIR}/config.yaml" ]; then
    echo "Creating default configuration..."
    cp "${SCRIPT_DIR}/config.yaml.example" "${SCRIPT_DIR}/config.yaml"
    echo "Please edit config.yaml to match your hardware setup"
    echo "Note: Set 'disable_hardware_pulsing: true' in led_matrix section to run without root privileges"
fi

# Create logs directory
mkdir -p "${SCRIPT_DIR}/logs"

# Create data directory for fonts
mkdir -p "${SCRIPT_DIR}/data/fonts"

# Update systemd service file with correct paths
echo "Configuring systemd service..."
CURRENT_USER=$(whoami)
SERVICE_FILE="${SCRIPT_DIR}/lumigrid.service"

# Create service file with correct paths
cat > "${SERVICE_FILE}" << EOF
[Unit]
Description=Lumigrid Display Node
After=network.target

[Service]
Type=simple
User=${CURRENT_USER}
Group=${CURRENT_USER}
WorkingDirectory=${SCRIPT_DIR}
Environment="LUMIGRID_CONFIG=${SCRIPT_DIR}/config.yaml"
Environment="PATH=${VENV_DIR}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
ExecStart=${VENV_DIR}/bin/python3 -m lumigrid.main
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Install systemd service
echo "Installing systemd service..."
sudo cp "${SERVICE_FILE}" /etc/systemd/system/
sudo systemctl daemon-reload

# Deactivate virtual environment
deactivate

echo ""
echo "==================================="
echo "Installation complete!"
echo "==================================="
echo ""
echo "Virtual environment created at: ${VENV_DIR}"
echo ""
echo "Next steps:"
echo "1. Edit config.yaml to match your hardware configuration"
echo "2. Enable the service: sudo systemctl enable lumigrid"
echo "3. Start the service: sudo systemctl start lumigrid"
echo "4. Check status: sudo systemctl status lumigrid"
echo "5. View logs: sudo journalctl -u lumigrid -f"
echo ""
echo "To run manually:"
echo "  source ${VENV_DIR}/bin/activate"
echo "  python3 -m lumigrid.main"
echo ""
echo "The web interface will be available at http://your-pi-ip:5000"
echo ""
echo "Note: A reboot may be required for I2C and 1-Wire to work"
echo ""
