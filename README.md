# grdMap

A Photoshop .grd gradient mapper for After Effects. It loads solid gradients and maps them to the input image value range.

## Highlights

- Reads Photoshop GRD version 3 and 5 solid gradients.
- Supports RGB, HSB, CMYK, Lab, and Gray stops with opacity and midpoint data.
- Parsed gradients and generated LUTs are cached; unsupported noise gradients are reported as such.

## Build environment

- Windows x64 and Adobe After Effects Effect SDK 2025 (obtain the SDK separately).
- Visual Studio 2022 C++ v143, Windows SDK, and CUDA Toolkit 13.0 for CUDA-enabled projects.
- Supported pixel formats: 8/16/32bpc.

Host compatibility can vary by After Effects version. Refer to a release's validation notes for the versions and render paths verified.

## Build from source

Keep this project under `MyEffects/<Effect>` in the Adobe After Effects SDK 2025 directory; the solution uses paths relative to that workspace. Set `AE_PLUGIN_BUILD_DIR` to a writable output directory, open the solution in `Win/` with Visual Studio, and build `Release|x64`.

Adobe SDK files and generated binaries are not included in the source repository.

## Installation

Download the `.aex` package from GitHub Releases, place it in an After Effects plug-ins folder, and restart After Effects.

## Releases

[GitHub Releases](https://github.com/JasonDreamer/grdMap/releases)

Published releases include the effect binary, matching source revision, build environment, supported After Effects versions, and validation results.

## License

No license is granted for this source code at this time. Do not reuse or redistribute it without the copyright holder's permission. The Adobe After Effects SDK and CUDA Toolkit are separate products and are not included.
