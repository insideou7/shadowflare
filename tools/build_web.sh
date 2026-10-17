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
# content/ is packed into several shadowflare-N.data files, each under
# SFE_WEB_PART_MB (45 MB by default), because GitHub refuses files over
# 100 MB and warns over 50 MB. The page loads them all together.
#
# Every rebuild that changes content/ adds about the whole pack (~124 MB) to
# git history when docs/play/ is committed, so commit it on release days only.
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

# 2. content/ in parts: files in path order, a new part when the next file
# would pass the limit. Each part gets a .data file and a small .js loader.
stage=$build/site
rm -rf "$stage"
mkdir -p "$stage"
python3 - "$repo" "$stage" "$part_mb" <<'EOF'
import os, sys
repo, stage, part_mb = sys.argv[1], sys.argv[2], int(sys.argv[3])
limit = part_mb * 1024 * 1024
files = []
for root, dirs, names in os.walk(os.path.join(repo, 'content')):
    dirs.sort()
    for name in sorted(names):
        path = os.path.join(root, name)
        files.append((os.path.relpath(path, repo).replace(os.sep, '/'), os.path.getsize(path)))
parts, size = [[]], 0
for rel, n in files:
    if n > limit:
        sys.exit(f'{rel} is bigger than {part_mb} MB on its own')
    if size + n > limit and parts[-1]:
        parts.append([])
        size = 0
    parts[-1].append(rel)
    size += n
for i, part in enumerate(parts, 1):
    with open(os.path.join(stage, f'part{i}.list'), 'w') as f:
        f.write('\n'.join(part) + '\n')
EOF
parts=()
for list in "$stage"/part*.list; do
    n=${list##*/part}; n=${n%.list}
    args=()
    while IFS= read -r rel; do args+=("$rel@/$rel"); done < "$list"
    (cd "$repo" && python3 "$packager" "$stage/shadowflare-$n.data" --preload "${args[@]}" \
        --js-output="$stage/shadowflare-$n.js" --no-node --quiet)
    rm "$list"
    parts+=("$n")
done
IFS=$'\n' parts=($(sort -n <<<"${parts[*]}")); unset IFS

# 3. The page: the engine's script gets a version (a hash of every file) so
# browsers fetch fresh files after a rebuild instead of mixing old and new ones.
cp "$build/shadowflare.js" "$build/shadowflare.wasm" "$stage/"
version=$(cat "$stage"/*.data "$stage"/*.js "$stage"/*.wasm | sha256sum | cut -c1-10)
bytes=$(cat "$stage"/*.data "$stage"/*.wasm "$stage"/*.js | wc -c)
mb=$(( (bytes + 1048575) / 1048576 ))
data_mb=$(( ($(cat "$stage"/*.data | wc -c) + 1048575) / 1048576 ))
# The parts' loaders are not in the page: the page injects them when the
# player presses play, so the visit itself only fetches the engine.
list=
for n in "${parts[@]}"; do
    list+="${list:+, }'shadowflare-$n.js?v=$version'"
done
tags="<script>Module.locateFile = function (path) { return path + '?v=$version'; };
  sfContentPack($data_mb, '$version', [$list]);</script>"
python3 - "$build/shadowflare.html" "$stage/index.html" "$version" "$tags" <<'EOF'
import re, sys
src, dst, version, tags = sys.argv[1:]
html = open(src, encoding='utf-8').read()
# Emscripten minifies the page, so find its script tag loosely.
main = re.compile(r'<script\b[^>]*\bsrc=["\']?shadowflare\.js["\']?[^>]*>')
found = main.findall(html)
assert len(found) == 1, 'no single shadowflare.js script tag in the page'
tag = found[0].replace('shadowflare.js', f'shadowflare.js?v={version}', 1)
if 'src=shadowflare' in tag:  # unquoted after minifying; quote it
    tag = tag.replace(f'src=shadowflare.js?v={version}', f'src="shadowflare.js?v={version}"')
html = main.sub(lambda m: tags + '\n' + tag, html, count=1)
open(dst, 'w', encoding='utf-8').write(html)
EOF

# 4. Into docs/play/ (only the files this script makes).
mkdir -p "$out"
rm -f "$out"/index.html "$out"/shadowflare*.js "$out"/shadowflare*.wasm "$out"/shadowflare*.data
cp "$stage"/index.html "$stage"/*.js "$stage"/*.wasm "$stage"/*.data "$out/"
echo "docs/play/: ${#parts[@]} data parts, $mb MB in all, version $version"
ls -l "$out"

if [ "${1:-}" = "--serve" ]; then
    echo "Open http://localhost:8000/play/"
    python3 -m http.server -d "$repo/docs" 8000
fi
