#!/usr/bin/env bash
# install.sh — Skeet Code installer
# Usage: curl -fsSL https://raw.githubusercontent.com/your-org/skeet-code/main/scripts/install.sh | bash
set -euo pipefail

REPO="your-org/skeet-code"
BINARY_NAME="skeet"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
VERSION="${VERSION:-latest}"

# ── Colors ─────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()  { printf "${CYAN}[skeet]${RESET} %s\n" "$*"; }
ok()    { printf "${GREEN}[skeet]${RESET} %s\n" "$*"; }
warn()  { printf "${YELLOW}[skeet]${RESET} %s\n" "$*" >&2; }
die()   { printf "${RED}[skeet] ERROR:${RESET} %s\n" "$*" >&2; exit 1; }

# ── OS / Arch detection ────────────────────────────────────────────────────────
detect_platform() {
    local os arch
    os="$(uname -s 2>/dev/null || echo Unknown)"
    arch="$(uname -m 2>/dev/null || echo unknown)"

    case "$os" in
        Linux*)  os_slug="linux" ;;
        Darwin*) os_slug="macos" ;;
        MINGW*|MSYS*|CYGWIN*) os_slug="windows" ;;
        *) die "Unsupported OS: $os" ;;
    esac

    case "$arch" in
        x86_64|amd64) arch_slug="x86_64" ;;
        arm64|aarch64)
            if [ "$os_slug" = "macos" ]; then
                arch_slug="universal"   # universal binary covers arm64
            else
                arch_slug="arm64"
            fi ;;
        *) die "Unsupported architecture: $arch" ;;
    esac

    # macOS always ships a universal binary
    [ "$os_slug" = "macos" ] && arch_slug="universal"

    echo "${os_slug}-${arch_slug}"
}

# ── Resolve latest tag via GitHub API ─────────────────────────────────────────
resolve_version() {
    if [ "$VERSION" = "latest" ]; then
        local tag
        tag="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
              | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\(.*\)".*/\1/')"
        [ -z "$tag" ] && die "Could not resolve latest version. Check your internet connection."
        echo "$tag"
    else
        echo "$VERSION"
    fi
}

# ── Download & install ─────────────────────────────────────────────────────────
main() {
    printf "\n${BOLD}Skeet Code — AI coding agent installer${RESET}\n\n"

    # Prereqs
    command -v curl >/dev/null 2>&1 || die "'curl' is required but not installed."

    local platform version asset_name download_url tmp_dir tmp_bin

    platform="$(detect_platform)"
    info "Platform detected: $platform"

    version="$(resolve_version)"
    info "Version: $version"

    # Build asset filename (must match CI artifact names)
    case "$platform" in
        linux-*)   asset_name="skeet-linux-x86_64" ;;
        macos-*)   asset_name="skeet-macos-universal" ;;
        windows-*) asset_name="skeet-windows-x86_64.exe" ;;
        *) die "No asset for platform: $platform" ;;
    esac

    download_url="https://github.com/${REPO}/releases/download/${version}/${asset_name}"
    info "Downloading ${download_url} ..."

    tmp_dir="$(mktemp -d)"
    trap 'rm -rf "$tmp_dir"' EXIT

    tmp_bin="${tmp_dir}/${BINARY_NAME}"
    curl -fsSL --progress-bar -o "$tmp_bin" "$download_url" \
        || die "Download failed. Is the release published? URL: $download_url"

    chmod +x "$tmp_bin"

    # Install
    local dest="${INSTALL_DIR}/${BINARY_NAME}"
    if [ -w "$INSTALL_DIR" ]; then
        mv "$tmp_bin" "$dest"
    else
        info "Need sudo to write to ${INSTALL_DIR}"
        sudo mv "$tmp_bin" "$dest"
    fi

    ok "Installed: ${dest}"
    ok "Version  : $("$dest" --version 2>/dev/null || echo "$version")"

    printf "\n${BOLD}Quick start:${RESET}\n"
    printf "  ${CYAN}skeet${RESET}                    # interactive session\n"
    printf "  ${CYAN}skeet -m qwen2.5-coder${RESET}  # pick a model\n"
    printf "  ${CYAN}skeet --help${RESET}             # all options\n\n"
    printf "Docs: https://github.com/${REPO}#readme\n\n"
}

main "$@"

