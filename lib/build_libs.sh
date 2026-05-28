#!/bin/bash
# Build dependencies for TelaOS test suite
# ckdl: built from local source (lib/ckdl/src/)
# lua:  built from local source (lib/lua54/src/)
#
# Usage:
#   ./lib/build_libs.sh          # build all
#   ./lib/build_libs.sh lua      # build only Lua
#   ./lib/build_libs.sh ckdl     # build only ckdl
#   ./lib/build_libs.sh clean    # remove build artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LUA_VERSION="5.4.7"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'
info()  { echo -e "${GREEN}[OK]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# --- ckdl (from local source) ---
build_ckdl() {
    local dir="${SCRIPT_DIR}/ckdl"
    local lib="${dir}/build/libkdl.a"

    if [ -f "$lib" ]; then
        info "ckdl already built ($(du -h "$lib" | cut -f1)), use 'clean' to rebuild"
        return 0
    fi

    if [ ! -d "${dir}/src" ]; then
        error "ckdl source not found at ${dir}/src/"; exit 1
    fi

    info "Building ckdl from local source..."
    mkdir -p "${dir}/build"

    # C sources
    for f in "${dir}"/src/*.c; do
        gcc -c -fPIC -O2 -I "${dir}/include" -I "${dir}/src" "$f" \
            -o "${dir}/build/$(basename "${f%.c}.o")"
    done

    # C++ binding (needs C++20 for full kdlpp, compile what we can)
    if command -v g++ &>/dev/null; then
        g++ -c -fPIC -O2 -std=c++20 \
            -I "${dir}/include" -I "${dir}/bindings/cpp/include" \
            "${dir}/bindings/cpp/src/kdlpp.cpp" \
            -o "${dir}/build/kdlpp.o" 2>/dev/null && true
    fi

    ar rcs "$lib" "${dir}"/build/*.o
    rm -f "${dir}"/build/*.o

    info "ckdl → $(du -h "$lib" | cut -f1)"
}

# --- Lua 5.4 (from local source) ---
build_lua() {
    local dir="${SCRIPT_DIR}/lua54"
    local lib="${dir}/build/liblua54.a"

    if [ -f "$lib" ]; then
        info "Lua ${LUA_VERSION} already built ($(du -h "$lib" | cut -f1)), use 'clean' to rebuild"
        return 0
    fi

    if [ ! -d "${dir}/src" ]; then
        error "Lua source not found at ${dir}/src/"; exit 1
    fi

    info "Building Lua ${LUA_VERSION} from local source..."
    mkdir -p "${dir}/build"

    for f in "${dir}"/src/*.c; do
        gcc -c -fPIC -O2 -DLUA_USE_POSIX -I "${dir}/src" "$f" \
            -o "${dir}/build/$(basename "${f%.c}.o")"
    done

    ar rcs "$lib" "${dir}"/build/*.o
    rm -f "${dir}"/build/*.o

    info "Lua ${LUA_VERSION} → $(du -h "$lib" | cut -f1)"
}

# --- mbedTLS (host dep for crypto_engine; must match device: 2.28.x) ---
# The device gets mbedTLS bundled with ESP-IDF 4.4 (2.28). For host KATs we
# need the same 2.28 API. Provisioned from the distro package (pins 2.28.x).
build_mbedtls() {
    if [ -f /usr/include/mbedtls/gcm.h ] && \
       ls /usr/lib/*/libmbedcrypto.a >/dev/null 2>&1; then
        info "mbedTLS already available ($(grep -m1 VERSION_STRING /usr/include/mbedtls/version.h | grep -o '[0-9.]*'))"
        return 0
    fi
    info "Installing libmbedtls-dev (host crypto for KATs)..."
    if command -v apt-get &>/dev/null; then
        apt-get install -y libmbedtls-dev >/dev/null 2>&1 \
            && info "mbedTLS installed" \
            || { error "apt install failed — install libmbedtls-dev (2.28.x) manually"; exit 1; }
    else
        error "no apt-get; install mbedTLS 2.28.x dev headers + libmbedcrypto manually"
        exit 1
    fi
}

# --- Clean ---
do_clean() {
    rm -f "${SCRIPT_DIR}/ckdl/build/libkdl.a"
    rm -f "${SCRIPT_DIR}/lua54/build/liblua54.a"
    info "Cleaned. Run build_libs.sh to rebuild."
}

# --- Main ---
command -v gcc &>/dev/null || { error "gcc not found"; exit 1; }

case "${1:-all}" in
    ckdl)  build_ckdl ;;
    lua)   build_lua ;;
    mbedtls) build_mbedtls ;;
    clean) do_clean ;;
    all)   build_ckdl; build_lua; build_mbedtls; echo ""; info "All libraries ready." ;;
    *)     echo "Usage: $0 [ckdl|lua|mbedtls|clean|all]"; exit 1 ;;
esac
