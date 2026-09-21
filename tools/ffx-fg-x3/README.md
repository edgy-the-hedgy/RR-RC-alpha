# Native FSR FG x3 provider experiment

Target SDK commit: `60f4ea81909200d8542eca14dccb2628b763a9a3` (the same commit pinned by Open Shaders).

## Apply

```powershell
python .\tools\ffx-fg-x3\apply_provider_x3.py C:\src\FidelityFX-SDK
```

The checkpoint-03 patch is deliberately ABI-preserving:

- no change to `FrameType`;
- no change to `TFrameInterpolationFrameInfo`;
- no change to `TFrameInterpolationPacingData`;
- no change to `IFrameInterpolationSwapChainDX12` virtual signatures;
- no change to `TFrameInterpolationSwapChainDX12` member layout;
- generated frame B lives only in `FrameInterpolationPacingDataExt`, which AMD explicitly marks extensible;
- x3 dispatch/pacing is selected only for the current `FfxFrameGenerationConfig` / `FrameInterpolationPacingDataExt` path;
- legacy SDK1/SDK2 ABI instantiations remain x2.

For first bring-up, the stock two interpolation resources are A and B. The next FG dispatch waits until both have finished composition before reusing them. This is conservative but avoids changing the ABI-stable swapchain base layout. A private larger pool can be optimized later after correctness is proven.

## Checkpoint 04 correction: core algorithm work is required

The pinned FSR3 provider was traced past the public dispatch descriptor. It ignores `outputs[1..3]` during actual FSR3 synthesis: only `outputs[0]` is passed to `ffxFrameInterpolationDispatch`. The FSR3 shader also hard-codes midpoint behavior. Therefore `numGeneratedFrames=2` in the swapchain is necessary but not sufficient.

Before applying the swapchain patch to an SDK checkout, run `audit_core_x3.py`. The next implementation stage adds a factor-aware core synthesis path at 1/3 and 2/3 while committing temporal history only once per real frame.

## Checkpoint 05: explicit 1/3 + 2/3 core sampling
`apply_core_x3.py` patches the pinned FSR3 interpolation core in addition to the swapchain provider. It keeps the public dispatch struct unchanged by using the provider/core-private RESERVED_1/RESERVED_2 flag contract and repurposes the existing constant-buffer `_pad1` float as `interpolationFactor`. Sample A does not commit temporal state; sample B commits exactly once. Stock x2 remains t=0.5 when neither private x3 flag is present.

## Checkpoint 06: Windows shader regeneration boundary

The x3 GPU changes require regenerated embedded shader permutation headers. AMD's pinned `BuildFrameInterpolationShaders.bat` invokes `Kits/FidelityFX/tools/ffx_sc/bin/FidelityFX_SC.exe` and emits four variants per entry point (wave32, wave64, 16-bit, wave64+16-bit). Because x3 changes only the setup and interpolation GPU code and keeps the constant-buffer byte layout unchanged, checkpoint 06 regenerates those two entry points (8 headers total).

Use `windows/prepare_fidelityfx_x3.ps1` on Windows. It pins the exact SDK commit, audits and applies both patchers, runs AMD's shader compiler, and verifies the generated headers.

Do not substitute the stock signed `amd_fidelityfx_framegeneration_dx12.dll`: it cannot contain these modified shaders/core changes.
