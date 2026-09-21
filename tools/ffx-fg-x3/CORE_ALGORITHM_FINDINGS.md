# Checkpoint 04 — core FSR3 multi-frame findings

Pinned SDK: `60f4ea81909200d8542eca14dccb2628b763a9a3`.

## Critical correction to checkpoint 03

The public API advertises `outputs[4]` and `numGeneratedFrames`, but the pinned FSR3 provider does **not** consume them as a multi-output implementation. In `ffx_provider_fsr3framegeneration.cpp` it constructs exactly one `FfxFrameInterpolationDispatchDescription`, assigns only `desc->outputs[0]`, and calls `ffxFrameInterpolationDispatch` exactly once.

Therefore simply setting `numGeneratedFrames = 2` in the swapchain would produce only A; B would never be generated. Checkpoint 03's swapchain A/B plumbing remains useful, but the core provider must also be extended.

## Midpoint is hard-coded in the FSR3 shader

The FSR3 frame-interpolation shader currently assumes the generated frame is at the midpoint. In `include/gpu/frameinterpolation/ffx_frameinterpolation.h`, `computeInterpolatedColor` uses symmetric full vector-field reprojection and hard-coded `0.5f` blend decisions. There is no interpolation-factor field in `FfxFrameInterpolationDispatchDescription`.

This means true x3 requires the core algorithm to receive an explicit interpolation position. For two generated frames that position must be `1/3` and `2/3` of the real-frame interval.

## Safe implementation direction

Do not issue two unmodified `ffxFrameInterpolationDispatch` calls. The dispatch updates temporal history (`PREVIOUS_INTERPOLATION_SOURCE`, dispatch count, previous frame ID). A naive second dispatch would see state already advanced by the first output.

The core change must separate **per-real-pair preparation/history** from **per-generated-output synthesis**:

1. Optical flow and real-pair preparation once.
2. Preserve the same previous/current real sources for both outputs.
3. Synthesize A with factor `1/3` into `outputs[0]`.
4. Synthesize B with factor `2/3` into `outputs[1]`.
5. Commit current real frame to temporal history only after the final generated output.
6. Advance frame ID/history exactly once per real frame.

The existing `FrameInterpolationConstants::_pad1` is a useful 32-bit slot for a private `interpolationFactor` without growing the GPU constant-buffer layout. It can be renamed/reused in the custom provider/shaders.

## Consequence

Native x3 is still viable, but `numGeneratedFrames` is an API transport mechanism here, not an already-implemented FSR3 multi-frame loop. We now need a small core-FSR3 patch in addition to the swapchain patch.
