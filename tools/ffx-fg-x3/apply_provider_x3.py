#!/usr/bin/env python3
"""Patch AMD FidelityFX SDK 2.3.0's FSR3 DX12 swapchain for native x3.

This revision deliberately preserves every type/function marked ABI-stable by AMD.
It uses the existing two interpolation textures as A/B and waits for both before
reusing them.  That sacrifices some overlap versus a future private 4-texture
pool, but gives us the smallest correctness-first native-x3 bring-up.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: apply_provider_x3.py <FidelityFX-SDK-root>')
root = Path(sys.argv[1]).resolve()
h = root/'Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.h'
c = root/'Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.cpp'
for p in (h,c):
    if not p.is_file(): raise SystemExit(f'missing pinned SDK source: {p}')

def replace_once(text, old, new, label):
    n=text.count(old)
    if n != 1: raise SystemExit(f'{label}: expected exactly one match, got {n}')
    return text.replace(old,new,1)

# IMPORTANT: do not change FrameType, TFrameInterpolationFrameInfo,
# TFrameInterpolationPacingData, IFrameInterpolationSwapChainDX12, or the base
# TFrameInterpolationSwapChainDX12 layout. AMD explicitly marks those ABI-stable.
ht=h.read_text(encoding='utf-8')
ht=replace_once(ht,
'''struct FrameInterpolationPacingDataExt : public TFrameInterpolationPacingData<FfxApiResource, FrameInterpolationFrameInfoExt>\n{\n};''',
'''struct FrameInterpolationPacingDataExt : public TFrameInterpolationPacingData<FfxApiResource, FrameInterpolationFrameInfoExt>\n{\n    // Native-x3 private extension. The base pacing type is ABI-stable; this\n    // derived current-API type is explicitly extensible.\n    FrameInfo interpolated2{};\n\n    void invalidate()\n    {\n        TFrameInterpolationPacingData<FfxApiResource, FrameInterpolationFrameInfoExt>::invalidate();\n        interpolated2 = {};\n    }\n};''', 'extensible pacing slot')
h.write_text(ht,encoding='utf-8')

ct=c.read_text(encoding='utf-8')
ct=replace_once(ct, '#include <tuple>\n', '#include <tuple>\n#include <type_traits>\n', 'type_traits include')

# Refactor the two internal presentation helpers to accept an arbitrary frame
# info object. This lets the extensible current-API pacing data present B without
# inventing a third FrameType (which would resize ABI-stable arrays).
ct=replace_once(ct,
'''template<typename PresentType, typename PacingData, typename PresentCallbackType>\nHRESULT compositeSwapChainFrame(PresentType* presenter, PacingData* pacingEntry, uint32_t frameID)\n{\n\n    const typename PacingData::FrameInfo& frameInfo = pacingEntry->frames[frameID];''',
'''template<typename PresentType, typename PacingData, typename PresentCallbackType>\nHRESULT compositeSwapChainFrame(PresentType* presenter, PacingData* pacingEntry, const typename PacingData::FrameInfo& frameInfo, bool isInterpolated)\n{''', 'composite arbitrary frame')
ct=ct.replace('if(frameID != FrameType::Real)\n        presenter->presentQueue->Wait', 'if(isInterpolated)\n        presenter->presentQueue->Wait')
ct=ct.replace('frameID != FrameType::Real, ffxGetNamedResourceDX12', 'isInterpolated, ffxGetNamedResourceDX12')
ct=replace_once(ct,
'''template<typename PresentType, typename PacingData>\nvoid presentToSwapChain(PresentType* presenter, PacingData* pacingEntry, FrameType frameType)\n{\n    const typename PacingData::FrameInfo& frameInfo = pacingEntry->frames[frameType];''',
'''template<typename PresentType, typename PacingData>\nvoid presentToSwapChain(PresentType* presenter, PacingData* pacingEntry, const typename PacingData::FrameInfo& frameInfo, bool isInterpolated)\n{''', 'present arbitrary frame')
ct=replace_once(ct, '    bool isInterpolated = frameType != FrameType::Real;\n', '', 'remove frame enum interpolation test')

# Replace the presenter loop as one block: A, optional private B, then real.
old_loop='''                    for (uint32_t frameType = 0; frameType < FrameType::Count; frameType++)\n                    {\n                        const typename PacingData::FrameInfo& frameInfo = entry.frames[frameType];\n                        if (frameInfo.doPresent)\n                        {\n                            compositeSwapChainFrame<PresentType, PacingData, PresentCallbackType>(presenter, &entry, frameType);\n                            // signal replacement buffer availability\n                            if (frameInfo.presentIndex == entry.replacementBufferFenceSignal)\n                            {\n                                presenter->presentQueue->Signal(presenter->replacementBufferFence, entry.replacementBufferFenceSignal);\n                            }\n\n                            MMRESULT result = timeGetDevCaps(&timerCaps, sizeof(timerCaps));\n                            if (result != MMSYSERR_NOERROR || !presenter->allowHybridSpin)\n                            {\n                                timerCaps.wPeriodMin = UNKNOWN_TIMER_RESOlUTION;\n                            }\n                            else\n                            {\n                                timerCaps.wPeriodMin = FFX_MAXIMUM(1, timerCaps.wPeriodMin);\n                            }\n                            // pacing without composition\n                            waitForFenceValue(presenter->compositionFenceGPU, frameInfo.presentIndex);\n                            uint64_t targetQpc = presenter->previousPresentQpc + frameInfo.presentQpcDelta;\n                            waitForPerformanceCount(targetQpc, qpcFrequency, timerCaps.wPeriodMin, presenter->hybridSpinTime);\n                            int64_t currentPresentQPC;\n                            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentPresentQPC));\n                            presenter->previousPresentQpc = currentPresentQPC;\n\n                            presentToSwapChain(presenter, &entry, (FrameType)frameType);\n                        }\n                    }'''
new_loop='''                    auto presentOne = [&](const typename PacingData::FrameInfo& frameInfo, bool isInterpolated)\n                    {\n                        if (!frameInfo.doPresent)\n                            return;\n                        compositeSwapChainFrame<PresentType, PacingData, PresentCallbackType>(presenter, &entry, frameInfo, isInterpolated);\n                        if (frameInfo.presentIndex == entry.replacementBufferFenceSignal)\n                            presenter->presentQueue->Signal(presenter->replacementBufferFence, entry.replacementBufferFenceSignal);\n\n                        MMRESULT result = timeGetDevCaps(&timerCaps, sizeof(timerCaps));\n                        if (result != MMSYSERR_NOERROR || !presenter->allowHybridSpin)\n                            timerCaps.wPeriodMin = UNKNOWN_TIMER_RESOlUTION;\n                        else\n                            timerCaps.wPeriodMin = FFX_MAXIMUM(1, timerCaps.wPeriodMin);\n\n                        waitForFenceValue(presenter->compositionFenceGPU, frameInfo.presentIndex);\n                        uint64_t targetQpc = presenter->previousPresentQpc + frameInfo.presentQpcDelta;\n                        waitForPerformanceCount(targetQpc, qpcFrequency, timerCaps.wPeriodMin, presenter->hybridSpinTime);\n                        int64_t currentPresentQPC;\n                        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentPresentQPC));\n                        presenter->previousPresentQpc = currentPresentQPC;\n                        presentToSwapChain(presenter, &entry, frameInfo, isInterpolated);\n                    };\n\n                    presentOne(entry.frames[FrameType::Interpolated_1], true);\n                    if constexpr (std::is_same_v<PacingData, FrameInterpolationPacingDataExt>)\n                        presentOne(entry.interpolated2, true);\n                    presentOne(entry.frames[FrameType::Real], false);'''
ct=replace_once(ct, old_loop, new_loop, 'presenter A-B-real loop')

# Pacer: keep AMD's measured/conservative x2 result, scale T/2 -> T/3 only for
# current API x3. Legacy ABI instantiations retain stock x2 pacing.
ct=replace_once(ct,
'''                    entry.frames[FrameType::Interpolated_1].presentQpcDelta = deltaToUse;\n                    entry.frames[FrameType::Real].presentQpcDelta           = deltaToUse;\n                    previousDelta                                                       = deltaToUse;''',
'''                    if constexpr (std::is_same_v<PacingData, FrameInterpolationPacingDataExt>)\n                    {\n                        const int64_t x3Delta = (deltaToUse * 2) / 3;\n                        entry.frames[FrameType::Interpolated_1].presentQpcDelta = x3Delta;\n                        entry.interpolated2.presentQpcDelta                     = x3Delta;\n                        entry.frames[FrameType::Real].presentQpcDelta           = x3Delta;\n                        previousDelta                                            = x3Delta;\n                    }\n                    else\n                    {\n                        entry.frames[FrameType::Interpolated_1].presentQpcDelta = deltaToUse;\n                        entry.frames[FrameType::Real].presentQpcDelta           = deltaToUse;\n                        previousDelta                                            = deltaToUse;\n                    }''', 'ABI-scoped x3 pacing')

# Two stock interpolation resources are enough for a correctness-first x3 path:
# A=0, B=1. Wait for both before reusing. No base-class layout change.
ct=replace_once(ct,
'''    // interpolation queue must wait for output resource to become available\n    if (interpolationOutputs[interpolationBufferIndex].availabilityFenceValue != 0)\n        presentInfo.interpolationQueue->Wait(presentInfo.compositionFenceGPU, interpolationOutputs[interpolationBufferIndex].availabilityFenceValue);''',
'''    // Current API native x3 reuses the stock two interpolation resources as A/B.\n    // Wait for both before reuse; this is intentionally conservative for bring-up.\n    if constexpr (std::is_same_v<ConfigType, FfxFrameGenerationConfig>)\n    {\n        for (uint32_t i = 0; i < 2; ++i)\n            if (interpolationOutputs[i].availabilityFenceValue != 0)\n                presentInfo.interpolationQueue->Wait(presentInfo.compositionFenceGPU, interpolationOutputs[i].availabilityFenceValue);\n    }\n    else if (interpolationOutputs[interpolationBufferIndex].availabilityFenceValue != 0)\n    {\n        presentInfo.interpolationQueue->Wait(presentInfo.compositionFenceGPU, interpolationOutputs[interpolationBufferIndex].availabilityFenceValue);\n    }''', 'two-output reuse waits')

# Callback dispatch is x3 only for the current stable API. Registered/manual
# command-list integrations remain x2 until they have an API to expose B.
ct=replace_once(ct,
'''        desc.outputs[0] = interpolationOutput();\n        desc.presentColor = backbuffer;\n        desc.reset = frameInterpolationResetCondition;\n        desc.numGeneratedFrames = 1;''',
'''        desc.outputs[0] = interpolationOutput(0);\n        desc.presentColor = backbuffer;\n        desc.reset = frameInterpolationResetCondition;\n        desc.numGeneratedFrames = 1;\n        if constexpr (std::is_same_v<ConfigType, FfxFrameGenerationConfig>)\n        {\n            desc.outputs[1] = interpolationOutput(1);\n            desc.numGeneratedFrames = 2;\n        }''', 'current API two-output dispatch')
ct=replace_once(ct,
'''            *pInterpolatedFrame = interpolationOutput();''',
'''            pInterpolatedFrame[0] = interpolationOutput(0);\n            if constexpr (std::is_same_v<ConfigType, FfxFrameGenerationConfig>)\n            {\n                if (desc.numGeneratedFrames > 1)\n                    pInterpolatedFrame[1] = interpolationOutput(1);\n            }''', 'return A/B from callback')

# presentInterpolated always supplies a two-element local array, preserving the
# virtual function signature while giving the current template instantiation room for B.
ct=replace_once(ct,
'''    FfxApiResource interpolatedFrame{}, realFrame{};\n    dispatchInterpolationCommands(&interpolatedFrame, &realFrame);''',
'''    FfxApiResource interpolatedFrames[2]{}, realFrame{};\n    dispatchInterpolationCommands(interpolatedFrames, &realFrame);''', 'local A/B outputs')
ct=replace_once(ct,
'''    typename PresentType::PacingData::FrameInfo& fiInterpolated = entry.frames[FrameType::Interpolated_1];\n    if (interpolatedFrame.resource != nullptr)\n    {\n        fiInterpolated.doPresent                        = true;\n        fiInterpolated.resource                         = interpolatedFrame;\n        fiInterpolated.interpolationCompletedFenceValue = interpolationFenceValue;\n        fiInterpolated.presentIndex                     = ++framesSentForPresentation;\n    }''',
'''    typename PresentType::PacingData::FrameInfo& fiInterpolated = entry.frames[FrameType::Interpolated_1];\n    if (interpolatedFrames[0].resource != nullptr)\n    {\n        fiInterpolated.doPresent                        = true;\n        fiInterpolated.resource                         = interpolatedFrames[0];\n        fiInterpolated.interpolationCompletedFenceValue = interpolationFenceValue;\n        fiInterpolated.presentIndex                     = ++framesSentForPresentation;\n    }\n    if constexpr (std::is_same_v<typename PresentType::PacingData, FrameInterpolationPacingDataExt>)\n    {\n        if (interpolatedFrames[1].resource != nullptr)\n        {\n            entry.interpolated2.doPresent                        = true;\n            entry.interpolated2.resource                         = interpolatedFrames[1];\n            entry.interpolated2.interpolationCompletedFenceValue = interpolationFenceValue;\n            entry.interpolated2.presentIndex                     = ++framesSentForPresentation;\n        }\n    }''', 'schedule private B slot')
ct=replace_once(ct,
'''    interpolationOutputs[interpolationBufferIndex].availabilityFenceValue = entry.numFramesSentForPresentationBase + fiInterpolated.doPresent;''',
'''    if constexpr (std::is_same_v<typename PresentType::PacingData, FrameInterpolationPacingDataExt>)\n    {\n        interpolationOutputs[0].availabilityFenceValue = fiInterpolated.doPresent ? fiInterpolated.presentIndex : 0;\n        interpolationOutputs[1].availabilityFenceValue = entry.interpolated2.doPresent ? entry.interpolated2.presentIndex : 0;\n    }\n    else\n    {\n        interpolationOutputs[interpolationBufferIndex].availabilityFenceValue = entry.numFramesSentForPresentationBase + fiInterpolated.doPresent;\n    }''', 'A/B availability')

# The stock interpolationOutput() forcibly ignores its index. Current x3 needs
# index 0/1, but this changes behavior rather than any ABI surface.
ct=replace_once(ct,
'''    index = interpolationBufferIndex;\n    FfxApiResourceDescription interpolateDesc = ffxGetResourceDescriptionDX12(interpolationOutputs[index].resource);''',
'''    index = (interpolationBufferIndex + index) % _countof(interpolationOutputs);\n    FfxApiResourceDescription interpolateDesc = ffxGetResourceDescriptionDX12(interpolationOutputs[index].resource);''', 'honor interpolation output index')

# For current x3 the two stock resources are a fixed pair. Legacy paths keep
# stock ping-pong behavior.
ct=replace_once(ct,
'''    interpolationBufferIndex                                                   = presentCount % _countof(interpolationOutputs);''',
'''    if constexpr (std::is_same_v<ConfigType, FfxFrameGenerationConfig>)\n        interpolationBufferIndex = 0;\n    else\n        interpolationBufferIndex = presentCount % _countof(interpolationOutputs);''', 'current API fixed A/B pair')

c.write_text(ct,encoding='utf-8')
print('Applied ABI-preserving native FSR FG x3 provider patch.')
