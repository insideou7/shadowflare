#!/bin/sh
# Disassemble an address range of a game module, annotating calls to
# imported and exported functions with their names.
# usage: disasm.sh module.dll 0x10004960 0x10004b00 out.asm
dir=$(dirname "$0")
objdump -d -M intel --no-show-raw-insn --start-address="$2" --stop-address="$3" "$1" | tail -n +7 > "$4.raw"
python3 -c "
import sys; sys.path.insert(0, sys.argv[4]); import pe
open(sys.argv[3], 'w').write(pe.annotate(sys.argv[1], sys.argv[2]))" "$1" "$4.raw" "$4" "$dir"
rm "$4.raw"
