#!/usr/bin/env bash

# Fail immediately on errors, unset variables, and hidden pipeline failures
set -euo pipefail

# --- Configuration ---
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/source/src/cplus.c"
BIN="$ROOT/bin"
TOOLS="$ROOT/tools"
EXECUTABLES=("cplus" "c+" "cc+")

# --- Helper Functions ---
log_info()    { printf "[INFO] %s\n" "$1"; }
log_success() { printf "\033[32m[SUCCESS]\033[0m %s\n" "$1"; }
log_error()   { printf "\033[31m[ERROR]\033[0m %s\n" "$1" >&2; }

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        log_error "Required command '$1' is not installed. Please install it and retry."
        exit 1
    fi
}

get_tcc_url() {
    local arch; arch="$(uname -m)"
    local os; os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    
    # Optional OS check if you plan to support macOS (darwin) binaries later
    if [[ "$os" == "darwin" ]]; then
        log_error "macOS pre-built binaries are not natively mapped. Please update the script URL."
        exit 1
    fi

    case "$arch" in
        x86_64|amd64)
            echo "https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-0.9.27-linux-x86_64-bin.zip" ;;
        i386|i686)
            echo "https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-0.9.27-linux-x86-bin.zip" ;;
        armv7l|armhf|arm)
            # UPDATE THIS URL for 32-bit ARM if needed
            echo "https://YOUR_MIRROR_HERE/tcc-linux-arm-bin.zip" ;;
        *)
            log_error "Unsupported download architecture: $arch"
            exit 1 ;;
    esac
}

add_path_entry() {
    local entry="$1"
    local profile="$HOME/.profile"

    [ -n "${ZSH_VERSION:-}" ] && profile="$HOME/.zshrc"
    [ -n "${BASH_VERSION:-}" ] && profile="$HOME/.bashrc"

    touch "$profile"

    # Ensure path isn't strictly duplicated in the rc file
    if ! grep -Fxq "export PATH=\"\$PATH:$entry\"" "$profile"; then
        echo "export PATH=\"\$PATH:$entry\"" >> "$profile"
        log_info "Appended $entry to $profile"
    else
        log_info "$entry is already configured in $profile"
    fi
}

# --- Main Execution ---
echo "== C+ Compiler Setup =="

# 1. Validate Source
if [ ! -f "$SRC" ]; then
    log_error "Source file not found: $SRC"
    exit 1
fi

# 2. Resolve, Extract Local, or Download TCC
if command -v tcc >/dev/null 2>&1; then
    TCC="$(command -v tcc)"
    TCCDIR="$(dirname "$TCC")"
    log_success "Found system TCC: $TCC"
else
    mkdir -p "$TOOLS"
    TCCDIR_BASE="$TOOLS/tcc"
    
    ARCH="$(uname -m)"

    if [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
        log_info "ARM64 detected. Locating pre-downloaded local package..."
        require_cmd "tar"

        # Safely resolve the glob into an array
        shopt -s nullglob
        PKG_FILES=("$ROOT/arm-arch/"*.pkg.tar.xz)
        shopt -u nullglob

        if [ ${#PKG_FILES[@]} -eq 0 ]; then
            log_error "Local ARM64 package not found matching $ROOT/arm-arch/*.pkg.tar.xz"
            exit 1
        fi

        LOCAL_ARCHIVE="${PKG_FILES[0]}"
        [ -d "$TCCDIR_BASE" ] && rm -rf "$TCCDIR_BASE"
        mkdir -p "$TCCDIR_BASE"

        log_info "Extracting ${LOCAL_ARCHIVE}..."
        tar -xf "$LOCAL_ARCHIVE" -C "$TCCDIR_BASE"

        # Locate the binary (Arch pkgs usually install into usr/bin/)
        if [ -f "$TCCDIR_BASE/usr/bin/tcc" ]; then
            TCC="$TCCDIR_BASE/usr/bin/tcc"
        else
            TCC="$(find "$TCCDIR_BASE" -type f -name "tcc" -executable | head -n 1)"
        fi

    else
        log_info "TCC not found locally. Preparing download for $ARCH..."
        require_cmd "curl"
        require_cmd "unzip"

        URL="$(get_tcc_url)"
        ARCHIVE="$(basename "$URL")"
        ZIP="$TOOLS/$ARCHIVE"
        
        log_info "Downloading TCC from: $URL"
        curl -fsSL "$URL" -o "$ZIP"

        [ -d "$TCCDIR_BASE" ] && rm -rf "$TCCDIR_BASE"

        log_info "Extracting..."
        unzip -q "$ZIP" -d "$TOOLS"

        TCC="$TCCDIR_BASE/tcc"
    fi

    if [ -z "${TCC:-}" ] || [ ! -f "$TCC" ]; then
        log_error "TCC extraction completed, but binary missing."
        exit 1
    fi

    chmod +x "$TCC"
    
    # Update TCCDIR to the actual folder containing the binary for PATH export
    TCCDIR="$(dirname "$TCC")"
    log_success "TCC installed locally at: $TCC"
fi

# 3. Create Output Directories
mkdir -p "$BIN"
mkdir -p "$TOOLS"

# 4. Compile C+
log_info "Compiling binaries..."
for NAME in "${EXECUTABLES[@]}"; do
    OUTPUT="$BIN/$NAME"
    if ! "$TCC" -o "$OUTPUT" "$SRC"; then
        log_error "Compilation failed for $NAME"
        exit 1
    fi
    log_success "Built $NAME -> $OUTPUT"
done

# 5. Safely add directories to User PATH
BINFULL="$(realpath "$BIN")"
TOOLSFULL="$(realpath "$TOOLS")"
TCCFULL="$(realpath "$TCCDIR")"

log_info "Updating profile PATH variables..."
add_path_entry "$BINFULL"
add_path_entry "$TOOLSFULL"
add_path_entry "$TCCFULL"

export PATH="$PATH:$BINFULL:$TOOLSFULL:$TCCFULL"

echo ""
log_success "C+ compiler successfully built."
echo "Available commands: cplus, c+, cc+, tcc"
echo "Restart your terminal or source your profile to apply PATH changes."