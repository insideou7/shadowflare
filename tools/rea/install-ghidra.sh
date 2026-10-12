#!/bin/sh
# Installs Ghidra 12.1.4 into ~/.cache/rea/ghidra so REA can decompile
# ShadowFlare.exe and its DLLs. Needs a JDK 21+ (java and javac on PATH).
# Run it once per machine or cloud session before native analysis, then
# restart Claude Code (or run /mcp) so the rea server sees it.
set -eu
TAG=Ghidra_12.1.4_build
DEST="$HOME/.cache/rea/ghidra"

if [ -x "$DEST/support/analyzeHeadless" ]; then
  echo "Ghidra already installed in $DEST"; exit 0
fi
command -v javac >/dev/null || { echo "Install a JDK 21 or newer first (javac not found)." >&2; exit 1; }

# Find the release zip. Try the GitHub API first, then the release page,
# since cloud sessions may only be allowed to reach one of them.
url=$(curl -fsSL "https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/tags/$TAG" 2>/dev/null |
  grep -o '"browser_download_url": *"[^"]*\.zip"' | sed 's/.*"\(https[^"]*\)"/\1/' | head -n 1) || true
if [ -z "$url" ]; then
  path=$(curl -fsSL "https://github.com/NationalSecurityAgency/ghidra/releases/expanded_assets/$TAG" |
    grep -o "/NationalSecurityAgency/ghidra/releases/download/$TAG/[^\"]*\.zip" | head -n 1) || true
  [ -n "$path" ] && url="https://github.com$path"
fi
[ -n "$url" ] || { echo "Could not find the Ghidra download. Is github.com reachable?" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "Downloading $url"
curl -fL --retry 3 -o "$tmp/ghidra.zip" "$url"
unzip -q "$tmp/ghidra.zip" -d "$tmp"
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
mv "$tmp"/ghidra_*_PUBLIC "$DEST"
echo "Installed Ghidra in $DEST"
