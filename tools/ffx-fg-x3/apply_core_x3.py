#!/usr/bin/env python3
"""Patch pinned FidelityFX FSR3 interpolation core for two samples from one real-frame pair.

Experimental contract (private to our custom provider build):
  RESERVED_1 => x3 sample A at t=1/3, defer temporal commit
  RESERVED_2 => x3 sample B at t=2/3, commit temporal state
Neither flag => stock x2 midpoint t=1/2.

This deliberately consumes existing reserved bits instead of growing the public dispatch ABI.
"""
from pathlib import Path
import sys
if len(sys.argv)!=2: raise SystemExit('usage: apply_core_x3.py <FidelityFX-SDK-root>')
r=Path(sys.argv[1]).resolve()
api=r/'Kits/FidelityFX/framegeneration/fsr3/include/ffx_frameinterpolation.h'
priv=r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_frameinterpolation_private.h'
core=r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_frameinterpolation.cpp'
cb=r/'Kits/FidelityFX/framegeneration/fsr3/include/gpu/frameinterpolation/ffx_frameinterpolation_callbacks_hlsl.h'
shader=r/'Kits/FidelityFX/framegeneration/fsr3/include/gpu/frameinterpolation/ffx_frameinterpolation.h'
setup=r/'Kits/FidelityFX/framegeneration/fsr3/include/gpu/frameinterpolation/ffx_frameinterpolation_setup.h'
provider=r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp'
for p in (api,priv,core,cb,shader,setup,provider):
    if not p.is_file(): raise SystemExit(f'missing pinned SDK source: {p}')
def ro(p): return p.read_text(encoding='utf-8')
def one(t,a,b,label):
    n=t.count(a)
    if n!=1: raise SystemExit(f'{label}: expected 1 anchor, got {n}')
    return t.replace(a,b,1)
def wr(p,t): p.write_text(t,encoding='utf-8')

# Keep struct size exactly unchanged: repurpose the existing spare float.
t=ro(priv)
t=one(t,'    float   _pad1;','    float   interpolationFactor; // x3: 1/3 or 2/3; stock x2: 1/2', 'private interpolation factor')
wr(priv,t)

# HLSL CB layout stays byte-for-byte compatible; expose factor and x3 phase helpers.
t=ro(cb)
t=one(t,'        FfxInt32        _pad1;','        FfxFloat32      interpolationFactor;', 'HLSL interpolation factor')
t=one(t,'''    FfxFloat32 TanHalfFoV()\n    {\n        return fTanHalfFOV;\n    }''','''    FfxFloat32 TanHalfFoV()\n    {\n        return fTanHalfFOV;\n    }\n    FfxFloat32 InterpolationFactor()\n    {\n        return interpolationFactor;\n    }\n    FfxBoolean IsX3FirstSample()\n    {\n        return (dispatchFlags & FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_1) != 0;\n    }''','HLSL factor accessor')
wr(cb,t)

# The first x3 sample must not advance the GPU frame-since-reset counter.
t=ro(setup)
t=one(t,'        } else {\n            FfxUInt32 counter = RWLoadCounter(COUNTER_FRAME_INDEX_SINCE_LAST_RESET);\n            StoreCounter(COUNTER_FRAME_INDEX_SINCE_LAST_RESET, counter + 1);\n        }','''        } else if (!IsX3FirstSample()) {\n            FfxUInt32 counter = RWLoadCounter(COUNTER_FRAME_INDEX_SINCE_LAST_RESET);\n            StoreCounter(COUNTER_FRAME_INDEX_SINCE_LAST_RESET, counter + 1);\n        }''','defer GPU counter commit')
wr(setup,t)

# Generalize midpoint sampling. The vector fields are midpoint-oriented in stock FSR3;
# rescale their previous/current legs around t=0.5. This is exact at stock t=0.5 and
# gives 1/3,2/3 temporal placement without changing vector-field storage formats.
t=ro(shader)
t=one(t,'''InterpolationSourceColor fPrevColorGame = SampleTextureBilinear(false, fUvInScreenSpace, +gameMv.fMotionVector * fUvLetterBoxScale, DisplaySize()); // Get in previous frame buffer, the color of interpolated pixel\nInterpolationSourceColor fCurrColorGame = SampleTextureBilinear(true, fUvInScreenSpace, -gameMv.fMotionVector * fUvLetterBoxScale, DisplaySize()); // Get color in current framebuffer, of color of interpolated pixel\nInterpolationSourceColor fPrevColorOF = SampleTextureBilinear(false, fUvInScreenSpace, +ofMv.fMotionVector * fUvLetterBoxScale, DisplaySize());\nInterpolationSourceColor fCurrColorOF = SampleTextureBilinear(true, fUvInScreenSpace, -ofMv.fMotionVector * fUvLetterBoxScale, DisplaySize());''','''const FfxFloat32 interpolationFactor = ffxSaturate(InterpolationFactor());\nconst FfxFloat32 prevVectorScale = interpolationFactor * 2.0f;\nconst FfxFloat32 currVectorScale = (1.0f - interpolationFactor) * 2.0f;\nInterpolationSourceColor fPrevColorGame = SampleTextureBilinear(false, fUvInScreenSpace, +gameMv.fMotionVector * fUvLetterBoxScale * prevVectorScale, DisplaySize());\nInterpolationSourceColor fCurrColorGame = SampleTextureBilinear(true, fUvInScreenSpace, -gameMv.fMotionVector * fUvLetterBoxScale * currVectorScale, DisplaySize());\nInterpolationSourceColor fPrevColorOF = SampleTextureBilinear(false, fUvInScreenSpace, +ofMv.fMotionVector * fUvLetterBoxScale * prevVectorScale, DisplaySize());\nInterpolationSourceColor fCurrColorOF = SampleTextureBilinear(true, fUvInScreenSpace, -ofMv.fMotionVector * fUvLetterBoxScale * currVectorScale, DisplaySize());''','temporal vector scaling')
t=one(t,'FfxFloat32 t = 0.5f;\n\n        t += 0.5f * (1 - (fDisocclusionFactor.x));\n        t -= 0.5f * (1 - (fDisocclusionFactor.y));','''FfxFloat32 t = interpolationFactor;\n\n        t += (1.0f - interpolationFactor) * (1 - (fDisocclusionFactor.x));\n        t -= interpolationFactor * (1 - (fDisocclusionFactor.y));''','game color factor')
t=one(t,'FfxFloat32 ofT = 0.5f;','FfxFloat32 ofT = interpolationFactor;','OF factor init')
t=one(t,'            ofT = 0.5f;','            ofT = interpolationFactor;','OF factor both-valid')
wr(shader,t)

# CPU core: derive factor from private reserved flags. A is a non-committing sample;
# B (or stock x2) is the only dispatch that advances CPU temporal bookkeeping/history.
t=ro(core)
t=one(t,'''    const bool bReset = (contextPrivate->dispatchCount == 0) || params->reset;\n    const bool bFrameID_Decreased   = params->frameID < contextPrivate->previousFrameID;\n    const bool bFrameID_Skipped     = (params->frameID - contextPrivate->previousFrameID) > 1;\n    const bool bDisjointFrameID     = bFrameID_Decreased || bFrameID_Skipped;\n    contextPrivate->previousFrameID = params->frameID;\n    contextPrivate->dispatchCount++;''','''    const bool x3FirstSample = (params->flags & FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_1) != 0;\n    const bool x3SecondSample = (params->flags & FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_2) != 0;\n    const bool bReset = (contextPrivate->dispatchCount == 0) || params->reset;\n    const bool bFrameID_Decreased   = params->frameID < contextPrivate->previousFrameID;\n    const bool bFrameID_Skipped     = (params->frameID - contextPrivate->previousFrameID) > 1;\n    const bool bDisjointFrameID     = bFrameID_Decreased || bFrameID_Skipped;\n    if (!x3FirstSample)\n    {\n        contextPrivate->previousFrameID = params->frameID;\n        contextPrivate->dispatchCount++;\n    }''','CPU temporal commit')
t=one(t,'    contextPrivate->constants.dispatchFlags         = params->flags;','''    contextPrivate->constants.dispatchFlags         = params->flags;\n    contextPrivate->constants.interpolationFactor   = x3FirstSample ? (1.0f / 3.0f) : (x3SecondSample ? (2.0f / 3.0f) : 0.5f);''','stage factor')
# Skip current->previous copy for A only. B commits the pair once.
t=one(t,'''        // store current buffer\n        {\n            FfxGpuJobDescription copyJobs[] = { {FFX_GPU_JOB_COPY} };''','''        // Store current buffer exactly once per real frame. The first x3 sample\n        // must leave previousInterpolationSource untouched for sample B.\n        if (!x3FirstSample)\n        {\n            FfxGpuJobDescription copyJobs[] = { {FFX_GPU_JOB_COPY} };''','history copy guard')
wr(core,t)

# Provider: one OF calculation, then two FI syntheses from the same prepared pair.
# We intentionally use reserved bits only inside this custom provider/core build.
t=ro(provider)
old='''            fiDispatchDesc.output = desc->outputs[0];'''
new='''            fiDispatchDesc.output = desc->outputs[0];'''
if t.count(old)!=1: raise SystemExit('provider output anchor mismatch')
# Replace the single dispatch near the end with two when numGeneratedFrames >= 2.
t=one(t,'''            TRY2(ffxFrameInterpolationDispatch(&internal_context->fiContext, &fiDispatchDesc));\n\n            internal_context->lastFrameID = desc->frameID;''','''            if (desc->numGeneratedFrames >= 2 && desc->outputs[1].resource)\n            {\n                // A: synthesize at 1/3 without committing FI temporal history.\n                fiDispatchDesc.output = desc->outputs[0];\n                fiDispatchDesc.flags &= ~FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_2;\n                fiDispatchDesc.flags |= FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_1;\n                TRY2(ffxFrameInterpolationDispatch(&internal_context->fiContext, &fiDispatchDesc));\n\n                // B: synthesize at 2/3 from the same real-frame pair, then commit once.\n                fiDispatchDesc.output = desc->outputs[1];\n                fiDispatchDesc.flags &= ~FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_1;\n                fiDispatchDesc.flags |= FFX_FRAMEINTERPOLATION_DISPATCH_RESERVED_2;\n                TRY2(ffxFrameInterpolationDispatch(&internal_context->fiContext, &fiDispatchDesc));\n            }\n            else\n            {\n                TRY2(ffxFrameInterpolationDispatch(&internal_context->fiContext, &fiDispatchDesc));\n            }\n\n            internal_context->lastFrameID = desc->frameID;''','provider dual FI dispatch')
wr(provider,t)
print('Applied native x3 FSR3 core patch: t=1/3 + t=2/3, one temporal commit.')
