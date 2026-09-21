#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"

Texture2D<float> srcDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float4> srcMotionVectors : register(t2);
Texture2D<float4> srcSpecularAlbedo : register(t3);
Texture2D<float4> srcDiffuseAlbedo : register(t4);
Texture2D<float> srcPreviousLinearDepth : register(t5);
SamplerState linearClampSampler : register(s0);

cbuffer ReflectionRRConstants : register(b0)
{
    uint ReflectionRRHistoryValid;
    uint3 ReflectionRRPadding;
};

RWTexture2D<float> outLinearDepth : register(u0);
RWTexture2D<float4> outMotionVectors : register(u1);
RWTexture2D<float4> outNormalRoughness : register(u2);
RWTexture2D<float4> outSpecularAlbedo : register(u3);
RWTexture2D<float4> outDiffuseAlbedo : register(u4);

float2 EncodeAMDNormal(float3 normal)
{
	normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-6);
	float2 oct = normal.xy;
	if (normal.z < 0.0)
		oct = (1.0 - abs(oct.yx)) * (oct.xy >= 0.0 ? 1.0 : -1.0);
	return oct * 0.5 + 0.5;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
	uint width, height;
	outLinearDepth.GetDimensions(width, height);
	if (any(pixel >= uint2(width, height)))
		return;

	const float2 uv = (float2(pixel) + 0.5) / float2(width, height);
	uint depthWidth, depthHeight;
	srcDepth.GetDimensions(depthWidth, depthHeight);
	const uint2 sourcePixel = min(uint2(uv * float2(depthWidth, depthHeight)), uint2(depthWidth - 1, depthHeight - 1));
	const float rawDepth = srcDepth.Load(uint3(sourcePixel, 0));
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	const float2 eyeUV = Stereo::ConvertFromStereoUV(uv, eyeIndex);
	float4 positionVS = float4(float2(eyeUV.x, 1.0 - eyeUV.y) * 2.0 - 1.0, rawDepth, 1.0);
	positionVS = mul(FrameBuffer::CameraProjInverse[eyeIndex], positionVS);
	const float linearDepth = positionVS.z / max(abs(positionVS.w), 1e-6);
	outLinearDepth[pixel] = linearDepth;

	uint motionWidth, motionHeight;
	srcMotionVectors.GetDimensions(motionWidth, motionHeight);
	const uint2 motionPixel = min(uint2(uv * float2(motionWidth, motionHeight)), uint2(motionWidth - 1, motionHeight - 1));
	const float2 motion = srcMotionVectors.Load(uint3(motionPixel, 0)).xy;
	const float2 previousUV = uv + motion;
	float previousDepth = linearDepth;
	if (ReflectionRRHistoryValid != 0 && all(previousUV >= 0.0) && all(previousUV <= 1.0))
		previousDepth = srcPreviousLinearDepth.SampleLevel(linearClampSampler, previousUV, 0);
	outMotionVectors[pixel] = float4(motion, previousDepth - linearDepth, 0.0);

	uint normalWidth, normalHeight;
	srcNormalRoughness.GetDimensions(normalWidth, normalHeight);
	const uint2 normalPixel = min(uint2(uv * float2(normalWidth, normalHeight)), uint2(normalWidth - 1, normalHeight - 1));
	const float4 normalRoughness = srcNormalRoughness.Load(uint3(normalPixel, 0));
	const float3 viewNormal = GBuffer::DecodeNormal(normalRoughness.xy);
	const float3 worldNormal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(viewNormal, 0.0)).xyz);
	outNormalRoughness[pixel] = float4(EncodeAMDNormal(worldNormal), saturate(1.0 - normalRoughness.z), 0.0);

	uint specWidth, specHeight;
	srcSpecularAlbedo.GetDimensions(specWidth, specHeight);
	const uint2 specPixel = min(uint2(uv * float2(specWidth, specHeight)), uint2(specWidth - 1, specHeight - 1));
	outSpecularAlbedo[pixel] = float4(saturate(srcSpecularAlbedo.Load(uint3(specPixel, 0)).rgb), 1.0);

	uint diffuseWidth, diffuseHeight;
	srcDiffuseAlbedo.GetDimensions(diffuseWidth, diffuseHeight);
	const uint2 diffusePixel = min(uint2(uv * float2(diffuseWidth, diffuseHeight)), uint2(diffuseWidth - 1, diffuseHeight - 1));
	outDiffuseAlbedo[pixel] = float4(saturate(srcDiffuseAlbedo.Load(uint3(diffusePixel, 0)).rgb), 1.0);
}
