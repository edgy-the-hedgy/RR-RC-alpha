# Windows bring-up: FidelityFX FG x3

Pinned SDK commit: `60f4ea81909200d8542eca14dccb2628b763a9a3`.

`prepare_fidelityfx_x3.ps1` performs the reproducible part of the Windows bring-up:

1. clones/checks out the exact SDK commit;
2. runs the source audit;
3. applies the ABI-safe swapchain/provider patch;
4. applies the FSR3 interpolation-core patch;
5. invokes AMD's own `BuildFrameInterpolationShaders.bat` for the two changed shader entry points (`setup` and `interpolation`);
6. verifies all 8 expected permutation headers exist.

The generated headers live in:
`Kits\FidelityFX\framegeneration\fsr3\dx12\ffx_sc_output`.

## Important packaging finding

The public SDK repository contains the provider/core source and shader compiler, but the documented FSR API consumption path is the prebuilt **signed** effect DLL. The pinned tree does not expose a CMake project at `Kits/FidelityFX` or `framegeneration` for rebuilding `amd_fidelityfx_framegeneration_dx12.dll` directly. Therefore this script deliberately does **not** invent an unsupported DLL build command.

The next bring-up step is to construct/validate a small local MSVC project for the framegeneration effect DLL (or identify AMD's internal/provider project metadata if present in the Windows package) while preserving the loader exports expected by Open Shaders.
