#!/usr/bin/env python3
"""Fail-fast audit for the exact FSR3 core anchors needed by native x3.
Does not modify the SDK. Run before apply_provider_x3.py once SDK source is present.
"""
from pathlib import Path
import sys
if len(sys.argv) != 2:
    raise SystemExit('usage: audit_core_x3.py <FidelityFX-SDK-root>')
r=Path(sys.argv[1]).resolve()
files={
 'provider':r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp',
 'api':r/'Kits/FidelityFX/framegeneration/fsr3/include/ffx_frameinterpolation.h',
 'private':r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_frameinterpolation_private.h',
 'core':r/'Kits/FidelityFX/framegeneration/fsr3/internal/ffx_frameinterpolation.cpp',
 'shader':r/'Kits/FidelityFX/framegeneration/fsr3/include/gpu/frameinterpolation/ffx_frameinterpolation.h',
}
for k,p in files.items():
    if not p.is_file(): raise SystemExit(f'missing {k}: {p}')
t={k:p.read_text(encoding='utf-8') for k,p in files.items()}
checks=[
 ('provider only outputs[0]', 'fiDispatchDesc.output = desc->outputs[0]', t['provider']),
 ('provider public count exists', 'numGeneratedFrames', t['provider']),
 ('dispatch has no factor', 'interpolationFactor', t['api']),
 ('private spare constant slot', 'float   _pad1;', t['private']),
 ('core writes previous history', 'FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_PREVIOUS_INTERPOLATION_SOURCE', t['core']),
 ('shader midpoint t', 'FfxFloat32 t = 0.5f;', t['shader']),
 ('shader midpoint optical-flow t', 'FfxFloat32 ofT = 0.5f;', t['shader']),
]
failed=[]
for name,needle,txt in checks:
    found=needle in txt
    if name=='dispatch has no factor': found=not found
    print(('PASS' if found else 'FAIL')+': '+name)
    if not found: failed.append(name)
if failed: raise SystemExit('core x3 audit failed: '+', '.join(failed))
print('Core audit confirms pinned FSR3 is midpoint-only and single-output internally.')
