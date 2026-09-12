#!/usr/bin/env bash

set -e

OS="$(uname -s)"

# -------------------------------
# Helpers
# -------------------------------

check_command() {
    command -v "$1" >/dev/null 2>&1
}

print_dep() {
    local name="$1"
    local command="$2"

    if check_command "$command"; then
        printf "%-14s ✓\n" "$name"
        return 0
    else
        printf "%-14s ✗\n" "$name"
        return 1
    fi
}

echo ""
echo "========================================="
echo "          Mousiki Setup"
echo "========================================="
echo ""

echo "OS detecting..."
echo "  $OS"
echo ""

# -------------------------------
# Detect platform
# -------------------------------

IS_TERMUX=false

if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux" ]; then
    IS_TERMUX=true
    echo "Platform: Termux"
elif [ "$OS" = "Darwin" ]; then
    echo "Platform: macOS"
elif [ "$OS" = "Linux" ]; then
    echo "Platform: Linux"
else
    echo "Error: Unsupported operating system: $OS"
    exit 1
fi

echo ""

# -------------------------------
# Dependency checking
# -------------------------------

echo "======== Checking deps =========="

MISSING=()

if ! print_dep "ffmpeg" "ffmpeg"; then
    MISSING+=("ffmpeg")
fi

if ! print_dep "curl" "curl"; then
    MISSING+=("curl")
fi

if ! print_dep "cmake" "cmake"; then
    MISSING+=("cmake")
fi

if [ "$IS_TERMUX" = true ]; then
    if ! print_dep "clang" "clang"; then
        MISSING+=("clang")
    fi

    if ! print_dep "make" "make"; then
        MISSING+=("make")
    fi
else
    if ! print_dep "make" "make"; then
        MISSING+=("make")
    fi
fi

# -------------------------------
# Install missing dependencies
# -------------------------------

if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    echo "Missing dependencies:"
    printf '  - %s\n' "${MISSING[@]}"
    echo ""

    if [ "$IS_TERMUX" = true ]; then
        echo "Termux dependencies are not installed automatically."
        echo "Please install the missing packages and run setup.sh again."
        exit 1
    fi

    if [ "$OS" = "Darwin" ]; then

        if ! check_command brew; then
            echo "Error: Homebrew is required."
            echo "Install Homebrew and run setup.sh again."
            exit 1
        fi

        echo "==> Installing missing macOS dependencies..."

        HOMEBREW_NO_AUTO_UPDATE=1 brew install "${MISSING[@]}"

    elif [ "$OS" = "Linux" ]; then

        if check_command apt-get; then

            echo "==> Installing missing dependencies via apt..."

            sudo apt-get update
            sudo apt-get install -y \
                cmake \
                build-essential \
                ffmpeg \
                curl

        elif check_command pacman; then

            echo "==> Installing missing dependencies via pacman..."

            sudo pacman -Sy --noconfirm \
                cmake \
                base-devel \
                ffmpeg \
                curl

        elif check_command dnf; then

            echo "==> Installing missing dependencies via dnf..."

            sudo dnf install -y \
                cmake \
                gcc-c++ \
                make \
                ffmpeg \
                curl

        else
            echo "Error: Unsupported Linux package manager."
            exit 1
        fi
    fi
fi

# -------------------------------
# Verify dependencies again
# -------------------------------

echo ""
echo "======== Verifying deps ========="

for cmd in cmake ffmpeg curl; do
    if ! check_command "$cmd"; then
        echo "Error: $cmd is still missing."
        exit 1
    fi
done

if [ "$IS_TERMUX" = true ] && ! check_command clang; then
    echo "Error: clang is still missing."
    exit 1
fi

echo "All dependencies are ready."
echo ""

# -------------------------------
# Configuration
# -------------------------------

echo "======== Configuring Mousiki ========"

CONFIG_DIR="$HOME/.config/mousiki"
mkdir -p "$CONFIG_DIR"

if [ ! -f "$CONFIG_DIR/config.txt" ]; then
    cp config.txt "$CONFIG_DIR/config.txt"
    echo "Created $CONFIG_DIR/config.txt"
    echo ""
    echo "Open $CONFIG_DIR/config.txt and fill in:"
    echo "  JellyfinServerUrl=http://your-server:8096"
    echo "  JellyfinApiKey=<key from Dashboard -> API Keys>"
    echo ""
else
    echo "Existing config found. Keeping current file."
    echo "If your config has no JellyfinServerUrl/JellyfinApiKey yet,"
    echo "add them to $CONFIG_DIR/config.txt"
fi

# -------------------------------
# Build
# -------------------------------

echo ""
echo "======== Building ( cmake ) ========"

cmake -B build

echo ""
echo "======== Building ( make ) ========="

CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

cmake --build build -j"$CORES"

# -------------------------------
# Verify build
# -------------------------------

BINARY="./build/mousiki"

if [ ! -f "$BINARY" ]; then
    echo ""
    echo "Error: Build completed but binary was not found."
    exit 1
fi

echo ""
echo "======== Build completed ==========="
echo ""
echo "Binary: $BINARY"
echo ""

# -------------------------------
# Optional binary installation
# -------------------------------

if [ "$IS_TERMUX" = true ]; then
    BIN_DIR="$PREFIX/bin"
else
    BIN_DIR="/usr/local/bin"
fi

printf "Want to copy binary to %s? (Y/N) " "$BIN_DIR"
read -r INSTALL_BINARY

if [[ "$INSTALL_BINARY" =~ ^[Yy]$ ]]; then

    mkdir -p "$BIN_DIR"

    if [ -w "$BIN_DIR" ]; then
        cp "$BINARY" "$BIN_DIR/mousiki"
    else
        sudo cp "$BINARY" "$BIN_DIR/mousiki"
    fi

    echo ""
    echo "copied!!"
    echo ""
    echo "Run Mousiki with:"
    echo "  mousiki"
else
    echo ""
    echo "Binary left at:"
    echo "  $BINARY"
fi

echo ""
echo "========================================="
echo "        Mousiki setup complete!"
echo "========================================="