# Native FSR3 x3 core implementation

Checkpoint 05 turns the midpoint-only FSR3 interpolation core into an experimental two-sample path without enlarging the public dispatch ABI.

Private contract in the custom provider/core build:
- `FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_1`: sample A, t=1/3, defer temporal commit.
- `FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_2`: sample B, t=2/3, commit temporal state.
- neither: stock x2, t=1/2.

The existing `FrameInterpolationConstants::_pad1` float becomes `interpolationFactor`; constant-buffer byte size is unchanged. HLSL receives the factor through the same slot.

The shader replaces midpoint color blending with the explicit factor and scales the two midpoint-oriented vector-field legs around t=0.5:
- previous leg scale = 2*t
- current leg scale = 2*(1-t)

The first x3 sample does not advance CPU dispatchCount/previousFrameID, does not advance the GPU frame-since-reset counter, and does not copy current interpolation source into previous history. The second sample commits those exactly once.

This is correctness-first and intentionally executes the FI synthesis pipeline twice. It does not yet optimize common preparation passes across A/B. That optimization is deferred until visual correctness is proven.
