#!/usr/bin/env bash

set -euo pipefail

configuration="Debug"
clean=false

while (($#)); do
    case "$1" in
        Debug|Release)
            configuration="$1"
            ;;
        --clean)
            clean=true
            ;;
        *)
            echo "Usage: $0 [Debug|Release] [--clean]" >&2
            exit 2
            ;;
    esac
    shift
done

preset="linux-${configuration,,}"

if [[ "$clean" == true && -d build ]]; then
    rm -rf -- build
fi

echo "Configuring project: $preset"

# Read the binary directory from CMake's own report rather than re-deriving it,
# so binaryDir stays defined only in CMakePresets.json (the directory name no
# longer matches the preset name). fd 3 keeps the live output on the terminal
# while the command substitution captures a copy.
exec 3>&1
configure_log="$(cmake --preset "$preset" | tee /dev/fd/3)"
exec 3>&-

build_dir="$(sed -n 's/^-- Build files have been written to: //p' \
    <<<"$configure_log" | tail -n 1)"

if [[ -z "$build_dir" ]]; then
    echo "Could not find the binary directory in the CMake output." >&2
    exit 1
fi

# ---- compile_commands.json for clangd ----
# Editor support, not a build concern. cmake --preset already generated the
# compilation database in the preset's build directory. clangd only reads a
# single CompilationDatabase directory (see .clangd: CompilationDatabase:
# build), so update the clangd database there. If the repository provides a
# post-processing script, it can modify the database before clangd uses it;
# otherwise, copy it unchanged.
source_compile_commands="$build_dir/compile_commands.json"
post_process_script=".github-ext/post_process_compile_commands.py"

echo "Updating clangd compile_commands.json..."

if [[ -f "$post_process_script" ]]; then
    python3 "$post_process_script" "$source_compile_commands" build
else
    cmake -E copy_if_different \
        "$source_compile_commands" \
        build/compile_commands.json
fi

echo "Building project..."
cmake --build --preset "$preset"
