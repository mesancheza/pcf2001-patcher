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

Everything is built as 32-bit, because it all runs inside the game's 32-bit process. The build stops with an error if it detects a 64-bit compiler.

## Outputs

| File | What it is |
|---|---|
| `build\<config>\tools\backend-trace\dcogl.dll` | Development tool: logs how the game uses its graphics interface. See [TOOLS.md](TOOLS.md). |

The new graphics backend and the patcher will be added here as they are written.

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

1. Check out the release's tag: `git checkout <tag>`.
2. Install the toolchain versions listed in that release's notes. Other compiler versions produce different, but equally valid, binaries.
3. Run `build.bat`.
4. Compare the SHA-256 of your files with the hashes published in the release, for example with `certutil -hashfile <file> SHA256`.
