# Sanitizers and fuzzing

Two build options look for memory bugs the normal build would let slip by.
Both are off by default and change nothing in a normal build.

## SFE_SANITIZE: AddressSanitizer and UBSan

Builds our own code (not SDL2 or Sentry) with AddressSanitizer, and on GCC
and Clang also UndefinedBehaviorSanitizer. An out-of-bounds read or write,
use after free, signed overflow or bad shift stops the program with a
report naming the line.

    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSFE_SANITIZE=ON
    cmake --build build-asan
    cd build-asan && ctest

Run the game from it as usual. On Linux, `ASAN_OPTIONS=detect_leaks=0`
quiets the leak report SDL's drivers produce at exit.

MSVC 2022: the same option turns on `/fsanitize=address` (MSVC has no
UBSan). Run the program from Visual Studio or a Developer Command Prompt
so it finds the AddressSanitizer DLL.

## SFE_FUZZ: libFuzzer targets

A fuzz target feeds a function millions of generated inputs and keeps
the ones that reach new code, so it finds the inputs that crash it. With
the sanitizers on, any memory error counts as a crash. `SFE_FUZZ` turns
`SFE_SANITIZE` on and needs Clang or MSVC.

`tests/fuzz/fuzz_formats.cpp` covers the data file readers: the original's
files (sprite banks, maps, scenarios, scripts, animations, items, the LZ
archive decoder) and the open pack's (PNG, .sheet, .anim, maps, scenarios,
items). The first byte of each input picks the reader. A reader may refuse
bad data only by throwing `FormatError`, which is what the game catches;
any other exception is a bug too.

    cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSFE_FUZZ=ON
    cmake --build build-fuzz --target fuzz_formats
    python3 tests/fuzz/make_corpus.py content game build-fuzz/corpus
    cd build-fuzz && mkdir -p crashes
    ./fuzz_formats corpus -fork=4 -max_total_time=600 \
        -rss_limit_mb=2048 -malloc_limit_mb=1024 -artifact_prefix=crashes/

Leave `game` out of `make_corpus.py` when you have no original files. The
corpus and crash files hold copies of the original's data, so they stay
in the build folder and are never committed. Replay one crash with
`./fuzz_formats crashes/<file>`.

On Ubuntu, Clang's sanitizer runtimes come in `libclang-rt-18-dev`.

The online messages (`ItemRequest`, `HeroInput`) get their own fuzz
targets the same way once they land: every byte a joining player sends is
untrusted.
