[![Latest Release](https://img.shields.io/github/v/release/alandtse/open-shaders)](https://github.com/alandtse/open-shaders/releases)
[![License](https://img.shields.io/github/license/alandtse/open-shaders)](./COPYING)
[![Last Commit](https://img.shields.io/github/last-commit/alandtse/open-shaders)](https://github.com/alandtse/open-shaders/commits/dev)
[![Build Status](https://img.shields.io/github/actions/workflow/status/alandtse/open-shaders/release-build.yaml)](https://github.com/alandtse/open-shaders/actions/workflows/release-build.yaml)
[![Open Issues](https://img.shields.io/github/issues/alandtse/open-shaders)](https://github.com/alandtse/open-shaders/issues)
[![Contributors](https://img.shields.io/github/contributors/alandtse/open-shaders)](https://github.com/alandtse/open-shaders/graphs/contributors)
[![Stars](https://img.shields.io/github/stars/alandtse/open-shaders?style=social)](https://github.com/alandtse/open-shaders/stargazers)

[![Pre-commit CI](https://results.pre-commit.ci/badge/github/alandtse/open-shaders/dev.svg)](https://results.pre-commit.ci/latest/github/alandtse/open-shaders/dev)
![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/alandtse/open-shaders?utm_source=oss&utm_medium=github&utm_campaign=alandtse%2Fopen-shaders&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)
[![Translation status](https://hosted.weblate.org/widget/open-shaders/svg-badge.svg)](https://hosted.weblate.org/engage/open-shaders/)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/alandtse/open-shaders)

# Open Shaders

SKSE core plugin for advanced graphics modifications for Skyrim and fork of Community Shaders.

[Open Shaders developer wiki](https://github.com/alandtse/open-shaders/wiki) · [Upstream Community Shaders on Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/86492) · [Upstream source](https://github.com/community-shaders/skyrim-community-shaders) · [Upstream developer wiki](https://github.com/community-shaders/skyrim-community-shaders/wiki)

## About this fork

**Open Shaders is a fork of [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders).** All of the architecture, the shader pipeline, the feature framework, and the vast majority of the code in this repository originated upstream and is the work of the upstream Community Shaders authors and contributors. Copyrights and authorship are preserved unchanged. See the upstream [contributors page](https://github.com/community-shaders/skyrim-community-shaders/graphs/contributors) for the team behind the project.

**Naming convention used throughout this repo and the in-game UI:**

| Term                                                             | Refers to                                                                                                                                 |
| ---------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| Community Shaders                                                | The upstream project (`community-shaders/skyrim-community-shaders`, Nexus mod 86492)                                                      |
| Open Shaders                                                     | This fork (`alandtse/open-shaders`, Nexus mod 180419)                                                                                     |
| `CommunityShaders` (as a path / filename / identifier in source) | Runtime-compat identifier; intentionally kept identical to upstream so settings, themes, and SKSE plugin discovery work without migration |

The upstream branding (logo, Nexus icon, typography) is non-GPL and not redistributed by this fork — see the "Icons" section under [License](#license) below.

Install from the [Open Shaders Nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/180419), from [GitHub releases](https://github.com/alandtse/open-shaders/releases), or build from source.

## RR-RC-alpha

> [!IMPORTANT]
> RR-RC-alpha is experimental development software. It is not an upstream Open Shaders or Community Shaders release.

### Overview

RR-RC-alpha integrates AMD FidelityFX Radiance Cache (NRC) with the existing AMD FidelityFX Denoiser Ray Regeneration path. NRC supplies supplemental radiance; it does not replace the existing SSGI/reflection signal or Ray Regeneration.

Composition occurs before Ray Regeneration. RR remains the downstream reconstruction stage and continues to receive depth, motion, normal/roughness, specular-albedo, and diffuse-albedo inputs.

In combined mode, valid NRC radiance is added to the existing RR input according to the Radiance Cache contribution setting. The existing signal is retained and its alpha channel is preserved.

### NRC training

NRC learning is enabled. The current implementation uses raw current-frame, pre-temporal SSGI specular radiance as its teacher signal.

This is **SSGI-supervised radiance-cache training / distillation**, not the path-traced future-radiance training target intended for a full physically based NRC integration.

The teacher signal is generated before NRC composition, preventing the composed NRC result from being directly fed back into its own training target.

### Quality modes

| Mode | Query grid | Capture cadence |
| --- | ---: | ---: |
| Potato | 16 x 16 | Every 2 frames |
| Performance | 32 x 32 | Every 2 frames |
| **Balanced (default)** | **64 x 64** | **Every 2 frames** |
| Quality | 64 x 64 | Every frame |

The capture bridge supports up to 4096 queries per capture.

### Default controls

| Setting | Default |
| --- | ---: |
| Radiance Cache contribution | **0.45** |
| Learning | **Enabled** |
| Learning rate | **0.002** |
| Weight smoothing | **0.99** |
| Quality mode | **Balanced** |

Learning can be paused independently of inference. Pausing stops submission of training samples while retaining the learned cache state. Resetting the Radiance Cache clears that state.

### Adaptive spatial domain

The NRC integration uses an adaptive anisotropic spatial domain. X, Y, and Z extents can adapt independently to observed samples.

Domain tiers range from 4096 through 131072 units. Growth is immediate when additional coverage is required, while contraction is deliberately slower to avoid repeated resizing as the camera moves through the world.

Samples outside the active NRC domain do not replace the existing lighting signal; the underlying SSGI/RR path remains available.

### Current limitations

- RR-RC-alpha is a technical preview and development branch.
- NRC training currently uses an SSGI-derived teacher rather than path-traced ground truth.
- NRC is intentionally supplemental to the existing lighting path.
- Capture and inference use an asynchronous bridge to avoid blocking GPU-to-CPU-to-GPU synchronization.
- Adaptive NRC domain changes can reset learned cache state.
- Visual quality and learning behavior remain scene-dependent and experimental.

---

## Requirements

-   Any terminal of your choice (e.g., PowerShell)
-   [Visual Studio Community 2026](https://visualstudio.microsoft.com/)
    -   Desktop development with C++
    -   CMake Tools for Windows
    -   HLSL Tools
-   [Git](https://git-scm.com/downloads)
    -   Edit the `PATH` environment variable and add the Git.exe install path as a new value

## Optional Requirements

```
CMake & Vcpkg comes with Visual Studio in Developer Command Prompts already.
Install them manually only if you want them in everywhere.
```

-   [CMake](https://cmake.org/)
    -   No need to install manually if you have Visual Studio CMake Tools installed
    -   CMake 4.2+ is **required** now
    -   Edit the `PATH` environment variable and add the cmake.exe install path as a new value
    -   Instructions for finding and editing the `PATH` environment variable can be found [here](https://www.java.com/en/download/help/path.html)
-   [Vcpkg](https://github.com/microsoft/vcpkg)
    -   Install vcpkg using the directions in vcpkg's [Quick Start Guide](https://github.com/microsoft/vcpkg#quick-start-windows)
    -   After install, add a new environment variable named `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
    -   Make sure your local vcpkg repo matches the commit id specified in `builtin-baseline` in `vcpkg.json` otherwise you might get another version of a non pinned vcpkg dependency causing undefined behaviour

## User Requirements

-   [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
    -   Needed for SSE/AE
-   [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
    -   Needed for VR

## Build Instructions

### Clone the Repository

RR-RC-alpha vendors the dependency source trees required by the project. A recursive submodule checkout is not required. Clone the repository normally:

```bash
```

> The DLL filename is `CommunityShaders.dll` and the SKSE plugin directory is `SKSE/Plugins/CommunityShaders/` — identical to upstream Community Shaders, so user settings, themes, and mod-manager profiles are drop-in compatible. Only the public name and in-game branding are "Open Shaders".

### Visual Studio build

To build the project, just open `./open-shaders` with Visual Studio's "Open Folder" feature. (Ensure you have `CMake Tools for Windows` selected when installing VS)

Follow the prompts to `Configure` and `Build` the project.
It should generate the AIO package in the `./build/ALL/aio` folder by default.

#### Zip package & Optional targets

If you change the `Solution Explorer` into `CMake Targets View`, you can find optional targets to create zip packages for each feature.
Right click on the target and select `Build` to create the zip package in `./dist/`.

### Command-line build

For the validated Windows build, use Developer PowerShell for Visual Studio 2026 or the x64 Native Tools Command Prompt.

Configure with the repository ALL preset:

    cmake --preset ALL

The ALL preset carries the project configuration required by this source tree, including SKSE_SUPPORT_XBYAK.

Build the CommunityShaders target in Release configuration:

    cmake --build .\build\ALL --config Release --target CommunityShaders --parallel

To create the AIO package, remove the package stamp and build the AIO_ZIP_PACKAGE target:

    Remove-Item .\build\ALL\aio_package.stamp -Force -ErrorAction SilentlyContinue
    cmake --build .\build\ALL --config Release --target AIO_ZIP_PACKAGE --parallel

Generated packages are written under dist/. The AIO staging tree is under build/ALL/aio/.

For a completely clean validation, remove or use a new build directory and configure from the repository preset rather than manually reconstructing its cache options.

#### Visual Studio

The repository can also be opened with Visual Studio Open Folder support. Ensure the Desktop development with C++ and CMake Tools for Windows components are installed.

#### Build notes

- CMake 4.2 or newer is expected by the inherited Open Shaders build configuration.
- Vcpkg configuration and overlays are supplied through the repository build configuration/presets.
- Build directories, generated packages, IDE state, and CMake user presets are intentionally excluded from version control.
- The runtime plugin remains CommunityShaders.dll and retains the existing SKSE/Plugins/CommunityShaders/ compatibility layout.

### Build with Docker

For those who prefer to not install Visual Studio or other build dependencies on their machine, this encapsulates it. This uses Windows Containers, so no WSL for now.

1. Install [Docker](https://www.docker.com/products/docker-desktop/) first if not already there.
2. In a shell of your choice run to switch to Windows containers and create the build container:

```pwsh
& 'C:\Program Files\Docker\Docker\DockerCli.exe' -SwitchWindowsEngine; `
docker build -t open-shaders .
```

3. Then run the build:

```pwsh
docker run -it --rm -v .:C:/open-shaders open-shaders:latest
```

4. Retrieve the generated build files from the `build/aio` folder.
5. In subsequent builds only run the build step (3.)

#### Troubleshooting Build with Docker

If you run into `Access violation` build errors during step 3, you can try adding [`--isolation=process`](https://learn.microsoft.com/en-us/virtualization/windowscontainers/manage-containers/hyperv-container):

```pwsh
docker run -it --rm --isolation=process -v .:C:/open-shaders open-shaders:latest
```

### Build on Linux/macOS (cross-compile, build-only)

For contributors who want to verify their C++ changes compile clean without a Windows machine, install Visual Studio, or use WSL: a Linux/macOS host can cross-compile to the same Windows PE/MSVC-ABI output using `clang-cl`+`lld-link` against an `xwin`-generated Windows SDK/CRT sysroot.

```sh
cmake --preset Linux-ClangCL
cmake --build --preset Linux-ClangCL
```

This proves the toolchain compiles clean; it does not produce a package runnable in-game (struct-layout or vtable mismatches between clang-cl and real MSVC compile fine and only surface as an in-game crash). One-time host setup (xwin, wine, llvm-mingw, vcpkg) is documented in `extern/CommonLibSSE-NG/examples/linux-cross-compile/README.md`.

## RR-RC-alpha Installation

RR-RC-alpha is distributed using the existing Open Shaders / Community Shaders runtime layout.

### Installing a packaged build

Install the generated AIO package with a Skyrim mod manager, or copy the package contents into the game Data directory while preserving the directory structure.

The core SKSE plugin remains:

    SKSE/Plugins/CommunityShaders.dll

Runtime data and shader resources continue to use the existing CommunityShaders directory layout for compatibility.

### FidelityFX runtime providers

The RR-RC-alpha AIO package includes the FidelityFX runtime providers required by the integrated graphics features. The validated package contains:

- amd_fidelityfx_denoiser_dx12.dll
- amd_fidelityfx_framegeneration_dx12.dll
- amd_fidelityfx_loader_dx12.dll
- amd_fidelityfx_radiancecache_dx12.dll
- amd_fidelityfx_upscaler_dx12.dll

Do not omit the Radiance Cache or Denoiser providers when installing a build intended to use RR-RC-alpha.

### Runtime requirements

- Skyrim Special Edition / Anniversary Edition with SKSE and the appropriate Address Library remains subject to the inherited Open Shaders requirements above.
- RR-RC-alpha currently targets the project DirectX runtime path used by its FidelityFX integration.
- Existing Community Shaders/Open Shaders settings and runtime-compatible identifiers are intentionally retained.

### First run

Radiance Cache inference and capture update automatically when the feature is enabled. Learning is enabled by default and can be paused independently without discarding the learned state.

The default NRC configuration is Balanced quality, 0.45 contribution, 0.002 learning rate, and 0.99 weight smoothing.

Use Reset Radiance Cache when an explicit reset of the learned NRC state is required.

RR-RC-alpha is experimental software. Keep a known-good mod configuration or backup when testing development builds.

## Debugging

### Launching MO2-SKSE-Skyrim from commandline

1. Open Steam
2. Close ModOrganizer GUI
3. Add `ModOrganizer.exe` (MO2 Folder) to your PATH, or use the path of it
4. Run the commands:

```pwsh
# Change Working Directory
cd "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition"
# Launch SKSE with MO2
ModOrganizer.exe --log run "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\skse64_loader.exe"
```

### Capture with RenderDoc

In Launch Application Menu, use the following settings:

-   Executable Path: `PATH/TO/ModOrganizer.exe`
-   Working Directory: `C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition`
-   Command-line Arguments: `--log run "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\skse64_loader.exe"`
-   [x] **Capture Child Process**

## License

Third-party software, runtime components, and bundled dependency attribution are documented in THIRD_PARTY_NOTICES.md. The original license files distributed with those components remain authoritative.

### Default

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS.md).
Specifically, the Modded Code includes:

-   Skyrim (and its variants)
-   Hardware drivers to enable additional functionality provided via proprietary SDKs, such as [Nvidia DLSS](https://developer.nvidia.com/rtx/dlss/get-started) and [AMD FidelityFX FSR3](https://gpuopen.com/fidelityfx-super-resolution-3/)

The Modding Libraries include:

-   [SKSE](https://skse.silverlock.org/)
-   Commonlib (and variants).

### Shaders

See LICENSE within each directory; if none, it's [Default](#default)

-   [Features Shaders](features)
-   [Package Shaders](package/Shaders/)

### Icons

Open Shaders does not ship the upstream Community Shaders logo. The upstream logo is non-GPL, not trademark-licensed, and may only be used in unmodified form with the Community Shaders team's permission — none of which extends to forks. Action icons and category icons are bundled as before; the upstream Discord banner has been removed since the fork has no affiliated Discord channel. The menu renders without a logo image when none is present (the load path is null-safe).

Open Shaders' own logo ([.github/assets/logo](.github/assets/logo)), used in the FOMOD installer header, is non-GPL and unmodified-use only — see the LICENSE file in that directory.
