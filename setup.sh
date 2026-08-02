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

# ------------------------------------------------------------
# Logging
# ------------------------------------------------------------

log_info()
{
    printf "\033[94m[INFO]\033[0m %s\n" "$1"
}

log_success()
{
    printf "\033[32m[SUCCESS]\033[0m %s\n" "$1"
}

log_error()
{
    printf "\033[31m[ERROR]\033[0m %s\n" "$1" >&2
}

# ------------------------------------------------------------
# PATH handling
# ------------------------------------------------------------

add_path_entry()
{
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

absolute_path()
{
    CDPATH= cd -- "$1" && pwd
}

# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

printf "== C+ Compiler Setup ==\n"

# ------------------------------------------------------------
# Validate source
# ------------------------------------------------------------

if [ ! -f "$MAIN_SRC" ]; then
    log_error "Main source file not found: $MAIN_SRC"
    exit 1
fi

# Gather source files: main-cplus.c + all .c files in bootstrap/src/
SRC_FILES="$MAIN_SRC"
if [ -d "$SRC_DIR" ]; then
    for file in "$SRC_DIR"/*.c; do
        if [ -f "$file" ]; then
            SRC_FILES="$SRC_FILES $file"
        fi
    done
fi

# ------------------------------------------------------------
# Find TCC
# ------------------------------------------------------------

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


# ------------------------------------------------------------
# Create directories
# ------------------------------------------------------------

mkdir -p "$BIN"
mkdir -p "$TOOLS"


# ------------------------------------------------------------
# Compile C+
# ------------------------------------------------------------

log_info "Compiling C+ compiler..."

for NAME in cplus c+ cc+
do

    OUTPUT="$BIN/$NAME"

    if "$TCC" -I"$INCLUDE_DIR" -o "$OUTPUT" $SRC_FILES; then
        log_success "Built $OUTPUT"
    else
        log_error "Compilation failed for $NAME"
        exit 1
    fi

done


# ------------------------------------------------------------
# Update PATH
# ------------------------------------------------------------

BINFULL="$(absolute_path "$BIN")"
TOOLSFULL="$(absolute_path "$TOOLS")"
TCCFULL="$(absolute_path "$TCCDIR")"


log_info "Updating PATH..."

add_path_entry "$BINFULL"
add_path_entry "$TOOLSFULL"
add_path_entry "$TCCFULL"


printf "\n"

log_success "C+ compiler successfully built."

printf "\nCommands available:\n"
printf "  cplus\n"
printf "  c+\n"
printf "  cc+\n"
printf "  tcc\n"

printf "\nRestart your terminal or run:\n"
printf "  . ~/.profile\n"