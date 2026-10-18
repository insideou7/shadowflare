#!/usr/bin/env bash
# Builds the browser version of the game into docs/play/, ready for GitHub
# Pages (see docs/web.md). One command, from anywhere in the repository:
#
#     tools/build_web.sh            # build and copy into docs/play/
#     tools/build_web.sh --serve    # the same, then serve it on port 8000
#
# Needs the Emscripten SDK on PATH (source emsdk_env.sh first). If Emscripten
# cannot download its SDL2 port, set EMCC_LOCAL_PORTS=sdl2=<SDL clone>.
#
# content/ is packed into several shadowflare-<part>.data files (see
# tools/web_parts.py), none over SFE_WEB_PART_MB (45 MB by default), because
# GitHub refuses files over 100 MB and warns over 50 MB. The page loads them
# all together. The packing is deterministic, so a part whose files did not
# change is byte for byte the same and git stores it once: committing a
# rebuild adds only the parts that changed.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
build=${SFE_WEB_BUILD_DIR:-$repo/build-web-site}
out=$repo/docs/play
part_mb=${SFE_WEB_PART_MB:-45}

command -v emcc >/dev/null || { echo "emcc not found: source emsdk_env.sh first" >&2; exit 1; }
packager=$(dirname "$(command -v emcc)")/tools/file_packager.py
[ -f "$packager" ] || packager=$(dirname "$(readlink -f "$(command -v emcc)")")/tools/file_packager.py
[ -f "$packager" ] || { echo "file_packager.py not found next to emcc" >&2; exit 1; }

# 1. The engine, without content (the parts are packed below).
emcmake cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DSFE_WEB_PRELOAD_CONTENT=OFF >/dev/null
cmake --build "$build" --target shadowflare -j"$(nproc 2>/dev/null || echo 4)"

# 2. content/ in parts (tools/web_parts.py says which files go in which: the
# scripts and text, each act's maps and monsters, and the shared rest), so a
# change to content/ rebuilds only the parts it touches. Each part gets a
# .data file and a small .js loader, named for the part.
stage=$build/site
rm -rf "$stage"
mkdir -p "$stage"
mapfile -t parts < <(python3 "$repo/tools/web_parts.py" "$repo" "$stage" "$part_mb")
for n in "${parts[@]}"; do
    args=()
    while IFS= read -r rel; do args+=("$rel@/$rel"); done < "$stage/$n.list"
    (cd "$repo" && python3 "$packager" "$stage/shadowflare-$n.data" --preload "${args[@]}" \
        --js-output="$stage/shadowflare-$n.js" --no-node --quiet)
    rm "$stage/$n.list"
done

# 3. The page. Every file is fetched with ?v=<hash of that file>, so a browser
# that has an old copy fetches fresh files after a rebuild, and keeps the
# parts that did not change.
cp "$build/shadowflare.js" "$build/shadowflare.wasm" "$stage/"
versions=
for f in "$stage"/shadowflare*; do
    versions+="${versions:+, }'${f##*/}': '$(sha256sum "$f" | cut -c1-10)'"
done
bytes=$(cat "$stage"/*.data "$stage"/*.wasm "$stage"/*.js | wc -c)
mb=$(( (bytes + 1048575) / 1048576 ))
data_mb=$(( ($(cat "$stage"/*.data | wc -c) + 1048575) / 1048576 ))
# The parts' loaders are not in the page: the page injects them when the
# player presses play, so the visit itself only fetches the engine.
list=
for n in "${parts[@]}"; do
    list+="${list:+, }'shadowflare-$n.js'"
done
tags="<script>var sfVersions = {$versions};
  Module.locateFile = function (path) { return path + '?v=' + sfVersions[path]; };
  sfContentPack($data_mb, [$list]);</script>"
engine=$(sha256sum "$stage/shadowflare.js" | cut -c1-10)
python3 - "$build/shadowflare.html" "$stage/index.html" "$tags" "$engine" <<'EOF'
import re, sys
src, dst, tags, engine = sys.argv[1:]
html = open(src, encoding='utf-8').read()
# Emscripten minifies the page, so find its script tag loosely.
main = re.compile(r'<script\b[^>]*\bsrc=["\']?shadowflare\.js["\']?[^>]*>')
found = main.findall(html)
assert len(found) == 1, 'no single shadowflare.js script tag in the page'
tag = found[0].replace('shadowflare.js', f'shadowflare.js?v={engine}', 1)
if 'src=shadowflare' in tag:  # unquoted after minifying; quote it
    tag = tag.replace(f'src=shadowflare.js?v={engine}', f'src="shadowflare.js?v={engine}"')
html = main.sub(lambda m: tags + '\n' + tag, html, count=1)
open(dst, 'w', encoding='utf-8').write(html)
EOF

# 4. Into docs/play/ (only the files this script makes).
mkdir -p "$out"
rm -f "$out"/index.html "$out"/shadowflare*.js "$out"/shadowflare*.wasm "$out"/shadowflare*.data
cp "$stage"/index.html "$stage"/*.js "$stage"/*.wasm "$stage"/*.data "$out/"
echo "docs/play/: ${#parts[@]} data parts, $mb MB in all"
ls -l "$out"

if [ "${1:-}" = "--serve" ]; then
    echo "Open http://localhost:8000/play/"
    python3 -m http.server -d "$repo/docs" 8000
fi
