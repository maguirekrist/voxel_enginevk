# AGENTS.md

Repository instructions for future coding agents.

## Build Workflow

Use the repo's build-and-sync scripts as the default and authoritative build entrypoint.

- On Windows, use `powershell -ExecutionPolicy Bypass -File .\build_and_sync.ps1 -Config Debug`
- On Windows release builds, use `powershell -ExecutionPolicy Bypass -File .\build_and_sync.ps1 -Config Release`
- On Unix-like systems, use `./build_and_sync.sh --debug`
- On Unix-like release builds, use `./build_and_sync.sh --release`

These scripts are preferred because they build using the repo's intended build directory and refresh the runtime shader output.

## Avoid Ad Hoc Builds

Do not default to ad hoc `cmake --build`, `ninja`, manual linker invocations, CLion-specific build directories, or manual shader copy steps.

Only fall back to lower-level build commands if:

- the script itself fails
- the user explicitly asks for a different build path
- you are debugging the build script itself

If you do need to fall back, state clearly why the script could not be used.
