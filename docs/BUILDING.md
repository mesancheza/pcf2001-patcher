# Building from source

Everything in this repository can be built with free Microsoft tools by running one script.

## Requirements

- Windows 10 or 11.
- **Visual Studio 2022**, any edition including the free Community edition, **or** the free **Build Tools for Visual Studio 2022**, with the **"Desktop development with C++"** workload. That workload includes everything needed:
  - the MSVC compiler with 32-bit (x86) support;
  - the Windows SDK;
  - CMake and Ninja.

To install only the build tools from the command line:

```
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

No other software is needed.

## Build

```
git clone https://github.com/mesancheza/pcf2001-patcher.git
cd pcf2001-patcher
build.bat
```

`build.bat` finds your Visual Studio installation, sets up the 32-bit compiler environment, and builds everything with CMake and Ninja.

- `build.bat` or `build.bat Release`: optimized build in `build\Release\`.
- `build.bat Debug`: debug build in `build\Debug\`.
- `build.bat Release <version>`, for example `build.bat Release 0.1.0`: the built files report that version (for example in `dcogl.log`). Releases are built this way ([RELEASING.md](RELEASING.md)); without it, the version in `CMakeLists.txt` is used.

Everything is built as 32-bit, because it all runs inside the game's 32-bit process. The build stops with an error if it detects a 64-bit compiler.

## Outputs

| File | What it is |
|---|---|
| `build\<config>\src\installer\pcf2001-setup.exe` | The game installer. See [INSTALLER.md](INSTALLER.md). Released. |
| `build\<config>\src\patcher\pcf2001-patch.exe` | The patcher, with `dcogl.dll` inside. See [PATCHER.md](PATCHER.md). Released. |
| `build\<config>\src\backend\dcogl.dll` | The new graphics component (Direct3D 11) and the memory compatibility layer. Released inside the patcher. |
| `build\<config>\tools\backend-trace\dcogl.dll` | Development tool: logs how the game uses its graphics interface. See [TOOLS.md](TOOLS.md). Not released. |
| `build\<config>\tools\is6-cab\is6-cab.exe` | Development tool: lists, checks and extracts the CD's InstallShield archive. See [TOOLS.md](TOOLS.md#is6-cab). Not released. |

`cmake --install build\Release --prefix <folder>` copies exactly the files a release ships into `<folder>`.

## Toolchain used for development

| Tool | Version |
|---|---|
| MSVC compiler | 19.44 (toolset 14.44.35207) |
| Windows SDK | 10.0.26100.0 |
| CMake | 3.31.6 (bundled with Visual Studio) |
| Ninja | 1.12.1 (bundled with Visual Studio) |

Newer versions of these tools should also work.

## Comparing your build with a release

Builds are **reproducible**: the compiler and linker run with `/Brepro`, which replaces the timestamps Microsoft's tools normally embed with values derived from the content. Building the same source with the same toolchain gives byte-identical files, regardless of when you build or which folder the source is in.

To check a release:

1. Check out the release's tag: `git checkout v<version>`.
2. Install the toolchain versions listed in that release's notes. Other compiler versions produce different, but equally valid, binaries.
3. Run `build.bat Release <version>`, with the release's version: the version is part of the built files.
4. Compare the SHA-256 of your files with the hashes in the release's `SHA256SUMS.txt`, for example with `certutil -hashfile build\Release\src\backend\dcogl.dll SHA256`. Compare the files inside the patch `.zip`, not the `.zip` itself: zip files also store file dates.
