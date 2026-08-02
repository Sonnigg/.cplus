#!/bin/sh

# Fail on errors and unset variables
set -eu

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

MAIN_SRC="$ROOT/bootstrap/main-cplus.c"
SRC_DIR="$ROOT/bootstrap/src"
INCLUDE_DIR="$ROOT/bootstrap/include"
BIN="$ROOT/bin"
TOOLS="$ROOT/tools"

UPDATE_STATE="$ROOT/.cplus-update.json"
COMPILER_MANIFEST="$ROOT/manifest.json"
LIBC_MANIFEST="$ROOT/libc+/manifest.json"

REMOTE_COMPILER_MANIFEST="https://raw.githubusercontent.com/Sonnigg/cplus/main/manifest.json"
REMOTE_LIBC_MANIFEST="https://raw.githubusercontent.com/Sonnigg/cplus/main/libc%2B/manifest.json"

# ------------------------------------------------------------
# Logging
# ------------------------------------------------------------

log_info() {
    printf "\033[94m[INFO]\033[0m %s\n" "$1"
}

log_success() {
    printf "\033[32m[SUCCESS]\033[0m %s\n" "$1"
}

log_error() {
    printf "\033[31m[ERROR]\033[0m %s\n" "$1" >&2
}

# ------------------------------------------------------------
# JSON Version Helpers (POSIX Compatible)
# ------------------------------------------------------------

extract_json_version() {
    file="$1"
    if [ ! -f "$file" ]; then
        echo ""
        return
    fi
    grep -o '"version"[[:space:]]*:[[:space:]]*"[^"]*"' "$file" | head -n 1 | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

fetch_remote_version() {
    url="$1"
    if command -v curl >/dev/null 2>&1; then
        content=$(curl -s "$url")
    elif command -v wget >/dev/null 2>&1; then
        content=$(wget -qO- "$url")
    else
        echo ""
        return
    fi
    echo "$content" | grep -o '"version"[[:space:]]*:[[:space:]]*"[^"]*"' | head -n 1 | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

# ------------------------------------------------------------
# Update Checking & Git Pull
# ------------------------------------------------------------

check_for_updates() {
    if [ ! -f "$UPDATE_STATE" ] || [ ! -f "$COMPILER_MANIFEST" ] || [ ! -f "$LIBC_MANIFEST" ]; then
        return 1
    fi

    local_compiler=$(extract_json_version "$COMPILER_MANIFEST")
    local_libc=$(extract_json_version "$LIBC_MANIFEST")

    log_info "Checking for C+ updates..."

    latest_compiler=$(fetch_remote_version "$REMOTE_COMPILER_MANIFEST")
    latest_libc=$(fetch_remote_version "$REMOTE_LIBC_MANIFEST")

    if [ -z "$latest_compiler" ] || [ -z "$latest_libc" ]; then
        return 1
    fi

    changed=0

    if [ "$latest_compiler" != "$local_compiler" ]; then
        printf "\n\033[33mCompiler update available!\033[0m\n"
        printf "Installed: %s\n" "$local_compiler"
        printf "Latest:    %s\n" "$latest_compiler"
        changed=1
    fi

    if [ "$latest_libc" != "$local_libc" ]; then
        printf "\n\033[33mlibc+ update available!\033[0m\n"
        printf "Installed: %s\n" "$local_libc"
        printf "Latest:    %s\n" "$latest_libc"
        changed=1
    fi

    should_rebuild=1 # Return code convention inverted for shell check function (0 = true/success)

    if [ "$changed" -eq 1 ]; then
        printf "\n"
        printf "Update now? [Y/n]: "
        read -r answer
        case "$answer" in
            [Yy]* | "")
                log_info "Pulling latest updates via git..."
                if ! command -v git >/dev/null 2>&1; then
                    log_error "Git is not installed or not found in PATH. Cannot perform automatic update."
                else
                    cd "$ROOT"
                    if git pull; then
                        log_success "Repository updated successfully."
                        return 0
                    else
                        log_error "Git pull failed."
                    fi
                fi
                ;;
            *)
                ;;
        esac
    fi

    return 1
}

# ------------------------------------------------------------
# PATH Handling
# ------------------------------------------------------------

add_path_entry() {
    ENTRY="$1"

    if [ -n "${ZSH_VERSION:-}" ]; then
        PROFILE="$HOME/.zshrc"
    elif [ -f "$HOME/.bashrc" ]; then
        PROFILE="$HOME/.bashrc"
    else
        PROFILE="$HOME/.profile"
    fi

    touch "$PROFILE"

    LINE="export PATH=\"\$PATH:$ENTRY\""

    if grep -Fxq "$LINE" "$PROFILE"; then
        log_info "$ENTRY already exists in $PROFILE"
    else
        printf "\n%s\n" "$LINE" >> "$PROFILE"
        log_info "Added $ENTRY to $PROFILE"
    fi
}

absolute_path() {
    CDPATH= cd -- "$1" && pwd
}

# ------------------------------------------------------------
# Build Logic
# ------------------------------------------------------------

build_cplus() {
    if [ ! -f "$MAIN_SRC" ]; then
        log_error "Main source file not found: $MAIN_SRC"
        exit 1
    fi

    SRC_FILES="$MAIN_SRC"
    if [ -d "$SRC_DIR" ]; then
        for file in "$SRC_DIR"/*.c; do
            if [ -f "$file" ]; then
                SRC_FILES="$SRC_FILES $file"
            fi
        done
    fi

    if command -v tcc >/dev/null 2>&1; then
        TCC="$(command -v tcc)"
        TCCDIR="$(dirname "$TCC")"
        log_success "Found system TCC: $TCC"
    else
        mkdir -p "$TOOLS"
        ARCH="$(uname -m)"

        case "$ARCH" in
            x86_64|amd64)
                PKG_DIR="tar-bz"
                PKG_EXT="*.tar.bz"
                ;;
            i386|i686)
                PKG_DIR="tar-bz"
                PKG_EXT="*.tar.bz"
                ;;
            aarch64|arm64)
                PKG_DIR="arm-arch"
                PKG_EXT="*.pkg.tar.xz"
                ;;
            *)
                log_error "Unsupported architecture: $ARCH"
                exit 1
                ;;
        esac

        log_info "Searching local TCC archive..."
        PACKAGE_DIR="$ROOT/$PKG_DIR"

        if [ ! -d "$PACKAGE_DIR" ]; then
            log_error "Missing package directory: $PACKAGE_DIR"
            exit 1
        fi

        LOCAL_ARCHIVE=""
        for FILE in "$PACKAGE_DIR"/*; do
            case "$FILE" in
                $PKG_EXT)
                    LOCAL_ARCHIVE="$FILE"
                    break
                    ;;
            esac
        done

        if [ -z "$LOCAL_ARCHIVE" ]; then
            log_error "No matching TCC archive found."
            exit 1
        fi

        TCCBASE="$TOOLS/tcc"
        if [ -d "$TCCBASE" ]; then
            rm -rf "$TCCBASE"
        fi
        mkdir -p "$TCCBASE"

        log_info "Extracting $LOCAL_ARCHIVE..."
        case "$LOCAL_ARCHIVE" in
            *.tar.bz|*.tar.bz2)
                tar -xf "$LOCAL_ARCHIVE" -C "$TCCBASE"
                ;;
            *.tar.xz)
                tar -xf "$LOCAL_ARCHIVE" -C "$TCCBASE"
                ;;
            *)
                log_error "Unknown archive format."
                exit 1
                ;;
        esac

        TCC="$(find "$TCCBASE" -type f -name tcc | head -n 1)"
        if [ -z "$TCC" ] || [ ! -f "$TCC" ]; then
            log_error "TCC binary not found after extraction."
            exit 1
        fi

        chmod +x "$TCC"
        TCCDIR="$(dirname "$TCC")"
        log_success "Installed local TCC: $TCC"
    fi

    mkdir -p "$BIN"
    mkdir -p "$TOOLS"

    log_info "Compiling C+ compiler..."
    for NAME in cplus c+ cc+; do
        OUTPUT="$BIN/$NAME"
        # shellcheck disable=SC2086
        if "$TCC" -I"$INCLUDE_DIR" -o "$OUTPUT" $SRC_FILES; then
            log_success "Built $OUTPUT"
        else
            log_error "Compilation failed for $NAME"
            exit 1
        fi
    done

    if [ -f "$COMPILER_MANIFEST" ] && [ -f "$LIBC_MANIFEST" ]; then
        c_ver=$(extract_json_version "$COMPILER_MANIFEST")
        l_ver=$(extract_json_version "$LIBC_MANIFEST")
        printf '{\n  "compiler": "%s",\n  "libcp": "%s",\n  "lastCheck": "%s"\n}\n' "$c_ver" "$l_ver" "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" > "$UPDATE_STATE"
    fi

    BINFULL="$(absolute_path "$BIN")"
    TOOLSFULL="$(absolute_path "$TOOLS")"
    TCCFULL="$(absolute_path "$TCCDIR")"

    log_info "Updating PATH..."
    add_path_entry "$BINFULL"
    add_path_entry "$TOOLSFULL"
    add_path_entry "$TCCFULL"
}

# ------------------------------------------------------------
# Main Execution Flow
# ------------------------------------------------------------

printf "== C+ Compiler Setup ==\n"

perform_rebuild=0
if check_for_updates; then
    perform_rebuild=1
fi

if [ "$perform_rebuild" -eq 1 ] || [ ! -f "$BIN/cplus" ]; then
    build_cplus
else
    log_info "No rebuild needed."
fi

printf "\n"
log_success "C+ compiler successfully installed/updated."

printf "\nCommands available:\n"
printf "  cplus\n"
printf "  c+\n"
printf "  cc+\n"
printf "  tcc\n"

printf "\nRestart your terminal or run:\n"
printf "  . ~/.profile\n"