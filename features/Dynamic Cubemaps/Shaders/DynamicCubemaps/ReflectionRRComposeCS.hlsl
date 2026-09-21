Texture2D<float4> denoisedReflection : register(t0);
Texture2D<float4> engineReflection : register(t1);
RWTexture2D<float4> outputReflection : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
	uint width, height;
	outputReflection.GetDimensions(width, height);
	if (any(pixel >= uint2(width, height)))
		return;

	const float3 radiance = denoisedReflection.Load(uint3(pixel, 0)).rgb;
	const float confidence = engineReflection.Load(uint3(pixel, 0)).a;
	outputReflection[pixel] = float4(radiance, confidence);
}
