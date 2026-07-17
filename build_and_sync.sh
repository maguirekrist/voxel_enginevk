#!/usr/bin/env bash

set -euo pipefail

config="Debug"
exe_name="game"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release|Release)
            config="Release"
            shift
            ;;
        --debug|Debug)
            config="Debug"
            shift
            ;;
        --exe-name)
            if [[ $# -lt 2 ]]; then
                echo "Usage: $0 [--debug|--release] [--exe-name <name>]" >&2
                exit 1
            fi
            exe_name="$2"
            shift 2
            ;;
        *)
            echo "Usage: $0 [--debug|--release] [--exe-name <name>]" >&2
            exit 1
            ;;
    esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${repo_root}/build/${config}"
shader_source_dir="${repo_root}/shaders"
runtime_dir="${repo_root}/bin/${config}"
runtime_shader_dir="${runtime_dir}/shaders"

generator=""
if command -v ninja >/dev/null 2>&1; then
    generator="Ninja"
else
    generator="Unix Makefiles"
fi

configure_args=(
    -S "${repo_root}"
    -B "${build_dir}"
    -G "${generator}"
    -DVOXEL_ENGINE_OUTPUT_NAME="${exe_name}"
)

case "${generator}" in
    *"Visual Studio"*|Xcode|"Ninja Multi-Config")
        ;;
    *)
        configure_args+=(-DCMAKE_BUILD_TYPE="${config}")
        ;;
esac

echo "Configuring '${config}' in '${build_dir}' with generator '${generator}'..."
cmake "${configure_args[@]}"

echo "Building configuration '${config}'..."
cmake --build "${build_dir}" --config "${config}"

if [[ ! -d "${runtime_dir}" ]]; then
    echo "Runtime output directory not found: ${runtime_dir}" >&2
    exit 1
fi

echo "Refreshing runtime shader directory..."
rm -rf "${runtime_shader_dir}"
mkdir -p "${runtime_shader_dir}"
cp -R "${shader_source_dir}/." "${runtime_shader_dir}/"

echo "Build and shader sync complete."
echo "Executable directory: ${runtime_dir}"
echo "Executable path: ${runtime_dir}/${exe_name}"
