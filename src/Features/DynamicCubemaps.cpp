#include "DynamicCubemaps.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <cassert>

#include "GpuPass.h"
#include "Deferred.h"
#include "Upscaling.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.dynamic_cubemaps."

constexpr auto MIPLEVELS = 9;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DynamicCubemaps::Settings,
	EnabledSSR,
	EnabledCreator,
	EnableReflectionRayRegeneration);

std::vector<std::pair<std::string_view, std::string_view>> DynamicCubemaps::GetShaderDefineOptions()
{
	std::vector<std::pair<std::string_view, std::string_view>> result;
	if (settings.EnabledSSR) {
		result.push_back({ "ENABLESSR", "" });
	}

	return result;
}

void DynamicCubemaps::DrawSettings()
{
	recompileFlag |= Util::CheckboxFlag(T(TKEY("enable_ssr"), "Enable Screen Space Reflections"), settings.EnabledSSR);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_ssr_tooltip"), "Enable Screen Space Reflections on Water"));
	}
	if (globals::game::isVR)
		Util::UI::DrawSettingDiff(bootSnapshot, settings, &Settings::EnabledSSR);

	if (ImGui::TreeNode(T(TKEY("dynamic_cubemap_creator"), "Dynamic Cubemap Creator"))) {
		ImGui::Text("%s", T(TKEY("creator_info"), "You must enable creator mode by adding the shader define CREATOR"));
		Util::CheckboxFlag(T(TKEY("enable_creator"), "Enable Creator"), settings.EnabledCreator);
		if (settings.EnabledCreator) {
			ImGui::ColorEdit3(T(TKEY("color"), "Color"), reinterpret_cast<float*>(&settings.CubemapColor));
			ImGui::SliderFloat(T(TKEY("roughness"), "Roughness"), &settings.CubemapColor.w, 0.0f, 1.0f, "%.2f");
			if (ImGui::Button(T(TKEY("export"), "Export"))) {
				auto device = globals::d3d::device;
				auto context = globals::d3d::context;

				D3D11_TEXTURE2D_DESC texDesc{};
				texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				texDesc.Height = 1;
				texDesc.Width = 1;
				texDesc.ArraySize = 6;
				texDesc.MipLevels = 1;
				texDesc.SampleDesc.Count = 1;
				texDesc.Usage = D3D11_USAGE_DEFAULT;
				texDesc.BindFlags = 0;
				texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

				D3D11_SUBRESOURCE_DATA subresourceData[6];

				struct PixelData
				{
					uint8_t r, g, b, a;
				};

				static PixelData colorPixel{};

				colorPixel = { (uint8_t)((settings.CubemapColor.x * 255.0f) + 0.5f),
					(uint8_t)((settings.CubemapColor.y * 255.0f) + 0.5f),
					(uint8_t)((settings.CubemapColor.z * 255.0f) + 0.5f),
					std::min((uint8_t)254u, (uint8_t)((settings.CubemapColor.w * 255.0f) + 0.5f)) };

				static PixelData emptyPixel{};

				subresourceData[0].pSysMem = &colorPixel;
				subresourceData[0].SysMemPitch = sizeof(PixelData);
				subresourceData[0].SysMemSlicePitch = sizeof(PixelData);

				for (uint i = 1; i < 6; i++) {
					subresourceData[i].pSysMem = &emptyPixel;
					subresourceData[i].SysMemPitch = sizeof(PixelData);
					subresourceData[i].SysMemSlicePitch = sizeof(PixelData);
				}

				winrt::com_ptr<ID3D11Texture2D> tempTexture;
				DirectX::ScratchImage image;

				try {
					DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, subresourceData, tempTexture.put()));
					DX::ThrowIfFailed(CaptureTexture(device, context, tempTexture.get(), image));

					if (std::filesystem::create_directories(defaultDynamicCubeMapSavePath)) {
						logger::info("Missing DynamicCubeMap Creator directory created: {}", defaultDynamicCubeMapSavePath);
					}

					std::filesystem::path DynamicCubeMapSavePath = defaultDynamicCubeMapSavePath;
					std::filesystem::path filename(std::format("R{:03d}G{:03d}B{:03d}A{:03d}.dds", colorPixel.r, colorPixel.g, colorPixel.b, colorPixel.a));
					DynamicCubeMapSavePath /= filename;

					if (std::filesystem::exists(DynamicCubeMapSavePath)) {
						logger::info("DynamicCubeMap Creator file for {} already exists, skipping.", filename.string());
					} else {
						DX::ThrowIfFailed(SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DynamicCubeMapSavePath.c_str()));
						logger::info("DynamicCubeMap Creator file for {} written", filename.string());
					}

				} catch (const std::exception& e) {
					logger::error("Failed in DynamicCubeMap Creator file: {} {}", defaultDynamicCubeMapSavePath, e.what());
				}

				image.Release();
			}
		}
		ImGui::TreePop();
	}
	if (globals::game::isVR) {
		if (ImGui::TreeNodeEx(T(TKEY("advanced_vr_settings"), "Advanced VR Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
			Util::RenderImGuiSettingsTree(iniVRCubeMapSettings, "VR");
			Util::RenderImGuiSettingsTree(hiddenVRCubeMapSettings, "hiddenVR");
			ImGui::TreePop();
		}
	}
}

void DynamicCubemaps::LoadSettings(json& o_json)
{
	settings = o_json;
	reflectionRRDepthHistoryValid = false;
	reflectionRRResultValid = false;
	if (globals::game::isVR) {
		Util::LoadGameSettings(iniVRCubeMapSettings);
	}
	recompileFlag = true;
}

void DynamicCubemaps::SaveSettings(json& o_json)
{
	o_json = settings;
	if (globals::game::isVR) {
		Util::SaveGameSettings(iniVRCubeMapSettings);
	}
}

void DynamicCubemaps::RestoreDefaultSettings()
{
	settings = {};
	if (globals::game::isVR) {
		Util::ResetGameSettingsToDefaults(iniVRCubeMapSettings);
		Util::ResetGameSettingsToDefaults(hiddenVRCubeMapSettings);
	}
	recompileFlag = true;
}

void DynamicCubemaps::DataLoaded()
{
	if (globals::game::isVR) {
		// enable cubemap settings in VR
		Util::EnableBooleanSettings(iniVRCubeMapSettings, GetName());
		Util::EnableBooleanSettings(hiddenVRCubeMapSettings, GetName());
	}
}

void DynamicCubemaps::PostPostLoad()
{
	bootSnapshot.LatchIfNeeded(settings);
	if (globals::game::isVR && settings.EnabledSSR) {
		std::map<std::string, uintptr_t> earlyhiddenVRCubeMapSettings{
			{ "bScreenSpaceReflectionEnabled:Display", 0x1ED5BC0 },
		};
		for (const auto& settingPair : earlyhiddenVRCubeMapSettings) {
			const auto& settingName = settingPair.first;
			const auto address = REL::Offset{ settingPair.second }.address();
			bool* setting = reinterpret_cast<bool*>(address);
			if (!*setting) {
				logger::info("[PostPostLoad] Changing {} from {} to {} to support Dynamic Cubemaps", settingName, *setting, true);
				*setting = true;
			}
		}
	}
}

void DynamicCubemaps::OnSceneTransitionReset(bool opening)
{
	// On LoadingMenu close (new cell loaded) re-trigger the cubemap capture. Dispatched on the
	// render thread by Feature::DrainSceneTransitions; resetCapture is consumed there too.
	if (!opening) {
		resetCapture[0] = true;
		resetCapture[1] = true;
	}
}

void DynamicCubemaps::ClearShaderCache()
{
	reflectionRRPrepareCS.Reset();
	reflectionRRComposeCS.Reset();
	updateCubemapCS.Reset();
	updateCubemapReflectionsCS.Reset();
	updateCubemapFakeReflectionsCS.Reset();
	inferCubemapCS.Reset();
	inferCubemapReflectionsCS.Reset();
	inferCubemapFakeReflectionsCS.Reset();
	specularIrradianceCS.Reset();
	bc6hEncodeCS.Reset();
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdate()
{
	return updateCubemapCS.Get(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", {}, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateReflections()
{
	return updateCubemapReflectionsCS.Get(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", { { "REFLECTIONS", "" } }, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateFakeReflections()
{
	return updateCubemapFakeReflectionsCS.Get(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", { { "FAKEREFLECTIONS", "" } }, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrence()
{
	return inferCubemapCS.Get(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", {}, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceReflections()
{
	return inferCubemapReflectionsCS.Get(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", { { "REFLECTIONS", "" } }, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceFakeReflections()
{
	return inferCubemapFakeReflectionsCS.Get(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", { { "FAKEREFLECTIONS", "" } }, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderSpecularIrradiance()
{
	return specularIrradianceCS.Get(L"Data\\Shaders\\DynamicCubemaps\\SpecularIrradianceCS.hlsl", {}, "cs_5_0");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderBC6HEncode()
{
	return bc6hEncodeCS.Get(L"Data\\Shaders\\DynamicCubemaps\\BC6HEncodeCS.hlsl", {}, "cs_5_0");
}

bool DynamicCubemaps::UpdateCubemapCapture(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	ID3D11ShaderResourceView* srvs[2] = { Util::AsReal(depth.depthSRV), Util::AsReal(main.SRV) };
	context->CSSetShaderResources(0, 2, srvs);

	uint index = a_reflections ? 1 : 0;

	ID3D11UnorderedAccessView* uavs[3];
	if (a_reflections) {
		uavs[0] = envCaptureReflectionsTexture->uav.get();
		uavs[1] = envCaptureRawReflectionsTexture->uav.get();
		uavs[2] = envCapturePositionReflectionsTexture->uav.get();
	} else {
		uavs[0] = envCaptureTexture->uav.get();
		uavs[1] = envCaptureRawTexture->uav.get();
		uavs[2] = envCapturePositionTexture->uav.get();
	}

	if (resetCapture[index]) {
		float clearColor[4]{ 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewFloat(uavs[0], clearColor);
		context->ClearUnorderedAccessViewFloat(uavs[1], clearColor);
		context->ClearUnorderedAccessViewFloat(uavs[2], clearColor);
		resetCapture[index] = false;
	}

	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	UpdateCubemapCB updateData{};

	static float3 cameraPreviousPosAdjust[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
	updateData.CameraPreviousPosAdjust = cameraPreviousPosAdjust[index];

	auto eyePosition = Util::GetEyePosition(0);

	cameraPreviousPosAdjust[index] = float3{ eyePosition.x, eyePosition.y, eyePosition.z };

	updateCubemapCB->Update(updateData);

	ID3D11Buffer* buffer = updateCubemapCB->CB();
	context->CSSetConstantBuffers(0, 1, &buffer);

	context->CSSetSamplers(0, 1, &computeSampler);

	auto* shader = a_reflections ? (fakeReflections ? GetComputeShaderUpdateFakeReflections() : GetComputeShaderUpdateReflections()) : GetComputeShaderUpdate();
	if (shader) {
		context->CSSetShader(shader, nullptr, 0);

		{
			CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::CaptureReflections", "DynamicCubemaps::Capture");
			context->Dispatch((uint32_t)std::ceil(envCaptureTexture->desc.Width / 8.0f), (uint32_t)std::ceil(envCaptureTexture->desc.Height / 8.0f), 6);
		}
	}

	uavs[0] = nullptr;
	uavs[1] = nullptr;
	uavs[2] = nullptr;
	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	context->CSSetShaderResources(0, 2, srvs);

	buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &buffer);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11SamplerState* nullSampler = { nullptr };
	context->CSSetSamplers(0, 1, &nullSampler);

	return shader != nullptr;
}

/**
 * @brief Infers local reflection information from captured cubemap data.
 *
 * @param a_reflections If true, infers from the reflections capture; otherwise from the base capture.
 */
bool DynamicCubemaps::Inferrence(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Infer local reflection information
	ID3D11UnorderedAccessView* uav = envInferredTexture->uav.get();

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->GenerateMips((a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get());

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	ID3D11ShaderResourceView* srvs[3] = { (a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get(), Util::AsReal(cubemap.SRV), defaultCubemap };
	context->CSSetShaderResources(0, 3, srvs);

	context->CSSetSamplers(0, 1, &computeSampler);

	auto* shader = a_reflections ? (fakeReflections ? GetComputeShaderInferrenceFakeReflections() : GetComputeShaderInferrenceReflections()) : GetComputeShaderInferrence();
	if (shader) {
		context->CSSetShader(shader, nullptr, 0);

		{
			CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::InferReflections", "DynamicCubemaps::Infer");
			context->Dispatch((uint32_t)std::ceil(envCaptureTexture->desc.Width / 8.0f), (uint32_t)std::ceil(envCaptureTexture->desc.Height / 8.0f), 6);
		}
	}

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	srvs[2] = nullptr;
	context->CSSetShaderResources(0, 3, srvs);

	uav = nullptr;

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetShader(nullptr, 0, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	return shader != nullptr;
}

/**
 * @brief Computes pre-filtered specular environment maps for a specified mip range.
 *
 * When `a_doSetup` is true, copies inferred cubemap faces and regenerates mipmaps before filtering.
 *
 * @param a_reflections If true, processes reflections texture; otherwise, base texture.
 * @param a_startLevel Starting mip level for filtering (inclusive).
 * @param a_endLevel Ending mip level for filtering (exclusive).
 * @param a_doSetup If true, performs face copying and mipmap generation before filtering.
 */
bool DynamicCubemaps::Irradiance(bool a_reflections, uint32_t a_startLevel, uint32_t a_endLevel, bool a_doSetup)
{
	auto context = globals::d3d::context;

	if (a_doSetup) {
		// Copy the inferred cubemap faces into the env texture used by downstream passes.
		for (uint face = 0; face < 6; face++) {
			uint srcSubresourceIndex = D3D11CalcSubresource(0, face, MIPLEVELS);
			context->CopySubresourceRegion(a_reflections ? envReflectionsTexture->resource.get() : envTexture->resource.get(), D3D11CalcSubresource(0, face, MIPLEVELS), 0, 0, 0, envInferredTexture->resource.get(), srcSubresourceIndex, nullptr);
		}

		auto srv = envInferredTexture->srv.get();
		context->GenerateMips(srv);
	}

	// Compute pre-filtered specular environment map for the requested mip range.
	auto* shader = GetComputeShaderSpecularIrradiance();
	{
		auto srv = envInferredTexture->srv.get();
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetSamplers(0, 1, &computeSampler);

		if (shader) {
			context->CSSetShader(shader, nullptr, 0);

			ID3D11Buffer* buffer = spmapCB->CB();
			context->CSSetConstantBuffers(0, 1, &buffer);

			float const delta_roughness = 1.0f / std::max(float(MIPLEVELS - 1), 1.0f);

			// Advance size to match a_startLevel.
			std::uint32_t size = std::max(envTexture->desc.Width, envTexture->desc.Height) / 2;
			for (uint32_t i = 1; i < a_startLevel; i++)
				size /= 2;

			// Suffix: A = level 1, BA = levels 2..N-1, BB = last level.
			const char* suffix = (a_startLevel == 1) ? "A" : (a_endLevel == MIPLEVELS) ? "BB" :
			                                                                             "BA";
			const auto passName = a_reflections ? std::format("DynamicCubemaps::IrradianceReflections{}", suffix) : std::format("DynamicCubemaps::Irradiance{}", suffix);
			CS_GPU_PASS_DYNAMIC(passName);
			for (std::uint32_t level = a_startLevel; level < a_endLevel; level++, size /= 2) {
				const UINT numGroups = (UINT)std::max(1u, (size + 7u) / 8u);

				const SpecularMapFilterSettingsCB spmapConstants = { level * delta_roughness, {} };
				spmapCB->Update(spmapConstants);

				auto uav = a_reflections ? uavReflectionsArray[level - 1] : uavArray[level - 1];

				context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
				context->Dispatch(numGroups, numGroups, 6);
			}
		}
	}

	ID3D11ShaderResourceView* nullSRV = { nullptr };
	ID3D11SamplerState* nullSampler = { nullptr };
	ID3D11Buffer* nullBuffer = { nullptr };
	ID3D11UnorderedAccessView* nullUAV = { nullptr };

	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetShader(nullptr, 0, 0);
	context->CSSetConstantBuffers(0, 1, &nullBuffer);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	return shader != nullptr;
}

bool DynamicCubemaps::CompressToBC6H(bool a_reflections)
{
	auto context = globals::d3d::context;

	auto shader = GetComputeShaderBC6HEncode();
	if (!shader) {
		logger::error("BC6HEncodeCS failed to compile; BC6H compression disabled");
		return false;
	}

	auto* srcSRV = a_reflections ? envReflectionsTextureArraySRV : envTextureArraySRV;

	context->CSSetShader(shader, nullptr, 0);
	context->CSSetShaderResources(0, 1, &srcSRV);

	ID3D11Buffer* cb = bc6hEncodeCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);

	std::uint32_t mipDim = std::max(envTexture->desc.Width, envTexture->desc.Height);

	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::BC6HReflections", "DynamicCubemaps::BC6H");
		for (std::uint32_t level = 0; level < bc6hMipLevels; ++level) {
			std::uint32_t srcWidth = std::max(1u, mipDim >> level);
			std::uint32_t srcHeight = std::max(1u, mipDim >> level);
			std::uint32_t blocksX = std::max(1u, srcWidth / 4);
			std::uint32_t blocksY = std::max(1u, srcHeight / 4);

			BC6HEncodeCB cbData{};
			cbData.TextureSizeInBlocksX = blocksX;
			cbData.TextureSizeInBlocksY = blocksY;
			cbData.MipLevel = level;
			bc6hEncodeCB->Update(cbData);

			context->CSSetUnorderedAccessViews(0, 1, &bc6hScratchUAVs[level], nullptr);

			std::uint32_t dispatchX = std::max(1u, (blocksX + 7) / 8);
			std::uint32_t dispatchY = std::max(1u, (blocksY + 7) / 8);
			context->Dispatch(dispatchX, dispatchY, 6);
		}
	}

	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	auto dst = a_reflections ? envReflectionsTextureBC6H : envTextureBC6H;
	context->CopyResource(dst->resource.get(), bc6hScratchTexture->resource.get());

	return true;
}

/**
 * @brief Advances the cubemap update pipeline state machine by one task.
 *
 * Executes the next step in a multi-frame sequence that captures, infers, filters,
 * and compresses environment cubemaps. Resets capture when game time jumps
 * significantly and recompiles shaders if needed. Processes either the base or
 * reflection variant depending on the current task.
 */
void DynamicCubemaps::UpdateCubemap()
{
	ZoneScoped;
	CS_GPU_PASS("DynamicCubemaps::UpdateCubemap");

	auto context = globals::d3d::context;
	ID3D11Buffer* sharedBuffers[2]{ globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->CSSetConstantBuffers(5, 2, sharedBuffers);

	// Reset capture when game time jumps (wait menu, timescale changes, console commands)
	if (auto calendar = globals::game::calendar) {
		float currentHoursPassed = calendar->GetHoursPassed();
		float hoursPassedDiff = std::abs(currentHoursPassed - previousHoursPassed);
		previousHoursPassed = currentHoursPassed;

		if (hoursPassedDiff >= 0.01f) {  // ~36 seconds game time
			resetCapture[0] = true;
			resetCapture[1] = true;
			// Restart the split pipeline so the stale mid/last irradiance mips
			// from the pre-jump capture aren't compressed before the recapture.
			nextTask = NextTask::kCaptureInferAndIrradianceA;
		}
	}

	if (recompileFlag) {
		logger::debug("Recompiling for Dynamic Cubemaps");
		auto shaderCache = globals::shaderCache;
		if (!shaderCache->Clear("Data//Shaders//ISReflectionsRayTracing.hlsl"))
			// if can't find specific hlsl file cache, clear all image space files
			shaderCache->Clear(RE::BSShader::Types::ImageSpace);
		recompileFlag = false;
	}

	static constexpr uint32_t kIrradianceSplit = 2;
	static constexpr uint32_t kIrradianceSplitB = MIPLEVELS - 1;

	// A stage's LazyShader can be permanently unavailable after a failed compile.
	// Don't advance nextTask past a stage that couldn't dispatch -- retry the same
	// stage next cycle rather than feeding a later stage stale or uninitialized
	// input from the skipped one.
	switch (nextTask) {
	case NextTask::kCaptureInferAndIrradianceA:
		if (UpdateCubemapCapture(false) && Inferrence(false) && Irradiance(false, 1, kIrradianceSplit, /*doSetup=*/true))
			nextTask = NextTask::kIrradianceBA;
		break;

	case NextTask::kIrradianceBA:
		if (Irradiance(false, kIrradianceSplit, kIrradianceSplitB, /*doSetup=*/false))
			nextTask = NextTask::kIrradianceBBAndBC6H;
		break;

	case NextTask::kIrradianceBBAndBC6H:
		if (Irradiance(false, kIrradianceSplitB, MIPLEVELS, /*doSetup=*/false) && CompressToBC6H(false))
			nextTask = activeReflections ? NextTask::kCaptureInferAndIrradianceA2 : NextTask::kCaptureInferAndIrradianceA;
		break;

	case NextTask::kCaptureInferAndIrradianceA2:
		if (UpdateCubemapCapture(true) && Inferrence(true) && Irradiance(true, 1, kIrradianceSplit, /*doSetup=*/true))
			nextTask = NextTask::kIrradianceBA2;
		break;

	case NextTask::kIrradianceBA2:
		if (Irradiance(true, kIrradianceSplit, kIrradianceSplitB, /*doSetup=*/false))
			nextTask = NextTask::kIrradianceBBAndBC6H2;
		break;

	case NextTask::kIrradianceBBAndBC6H2:
		if (Irradiance(true, kIrradianceSplitB, MIPLEVELS, /*doSetup=*/false) && CompressToBC6H(true))
			nextTask = NextTask::kCaptureInferAndIrradianceA;
		break;
	}
}

void DynamicCubemaps::PostDeferred()
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* views[2] = {
		(activeReflections ? envReflectionsTextureBC6H : envTextureBC6H)->srv.get(),
		envTextureBC6H->srv.get()
	};
	context->PSSetShaderResources(30, 2, views);
}

void DynamicCubemaps::SetReflectionRayRegenerationEnabled(bool enabled)
{
	const bool wasEnabled = settings.EnableReflectionRayRegeneration != 0;
	settings.EnableReflectionRayRegeneration = enabled;
	if (wasEnabled != enabled) {
		reflectionRRDepthHistoryValid = false;
		reflectionRRResultValid = false;
	}
}

bool DynamicCubemaps::EnsureReflectionRayRegenerationResources()
{
	
    static bool rrTraceEnsureLogged = false;
    if (!rrTraceEnsureLogged) {
        const bool rrCanDispatch = globals::features::upscaling.fidelityFX.CanDispatchRayRegeneration();
        logger::info(
            "[ReflectionRRTrace] EnsureResources ENTER: EnabledSSR={} EnableReflectionRR={} CanDispatchRR={}",
            settings.EnabledSSR,
            settings.EnableReflectionRayRegeneration != 0,
            rrCanDispatch);
        rrTraceEnsureLogged = true;
    }
if (!settings.EnabledSSR || !settings.EnableReflectionRayRegeneration || !globals::features::upscaling.fidelityFX.CanDispatchRayRegeneration())
		return false;

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	auto& waterReflections = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kWATER_REFLECTIONS];
	if (!waterReflections.texture || !waterReflections.SRV)
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	waterReflections.texture->GetDesc(Util::AsW32(&sourceDesc));
	if (reflectionRRSignal && reflectionRRWidth == sourceDesc.Width && reflectionRRHeight == sourceDesc.Height)
		return true;

	reflectionRRSignal.reset();
	reflectionRROutput.reset();
	reflectionRRComposed.reset();
	for (auto& depth : reflectionRRLinearDepth)
		depth.reset();
	reflectionRRMotionVectors.reset();
	reflectionRRNormalRoughness.reset();
	reflectionRRSpecularAlbedo.reset();
	reflectionRRDiffuseAlbedo.reset();
	globals::features::upscaling.fidelityFX.ResetReflectionRayRegeneration();

	D3D11_TEXTURE2D_DESC rgbaDesc{};
	rgbaDesc.Width = sourceDesc.Width;
	rgbaDesc.Height = sourceDesc.Height;
	rgbaDesc.MipLevels = 1;
	rgbaDesc.ArraySize = 1;
	rgbaDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rgbaDesc.SampleDesc.Count = 1;
	rgbaDesc.Usage = D3D11_USAGE_DEFAULT;
	rgbaDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;

	D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = rgbaDesc.Format;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
	uav.Format = rgbaDesc.Format;
	uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	D3D11_RENDER_TARGET_VIEW_DESC rtv{};
	rtv.Format = rgbaDesc.Format;
	rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	reflectionRRSignal = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRSignal");
	reflectionRRSignal->CreateSRV(srv);
	reflectionRRSignal->CreateRTV(rtv);
	reflectionRROutput = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRROutput");
	reflectionRROutput->CreateSRV(srv);
	reflectionRROutput->CreateUAV(uav);
	reflectionRRComposed = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRComposed");
	reflectionRRComposed->CreateSRV(srv);
	reflectionRRComposed->CreateUAV(uav);
	reflectionRRMotionVectors = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRMotion");
	reflectionRRMotionVectors->CreateSRV(srv);
	reflectionRRMotionVectors->CreateUAV(uav);
	reflectionRRNormalRoughness = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRNormalRoughness");
	reflectionRRNormalRoughness->CreateSRV(srv);
	reflectionRRNormalRoughness->CreateUAV(uav);
	reflectionRRSpecularAlbedo = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRSpecularAlbedo");
	reflectionRRSpecularAlbedo->CreateSRV(srv);
	reflectionRRSpecularAlbedo->CreateUAV(uav);
	reflectionRRDiffuseAlbedo = eastl::make_unique<Texture2D>(rgbaDesc, "DynamicCubemaps::ReflectionRRDiffuseAlbedo");
	reflectionRRDiffuseAlbedo->CreateSRV(srv);
	reflectionRRDiffuseAlbedo->CreateUAV(uav);

	D3D11_TEXTURE2D_DESC depthDesc = rgbaDesc;
	depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SHADER_RESOURCE_VIEW_DESC depthSrv = srv;
	depthSrv.Format = depthDesc.Format;
	D3D11_UNORDERED_ACCESS_VIEW_DESC depthUav = uav;
	depthUav.Format = depthDesc.Format;
	for (uint32_t i = 0; i < 2; ++i) {
		reflectionRRLinearDepth[i] = eastl::make_unique<Texture2D>(depthDesc, i ? "DynamicCubemaps::ReflectionRRDepth[1]" : "DynamicCubemaps::ReflectionRRDepth[0]");
		reflectionRRLinearDepth[i]->CreateSRV(depthSrv);
		reflectionRRLinearDepth[i]->CreateUAV(depthUav);
	}

	if (!reflectionRRLinearClampSampler) {
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&sampler, reflectionRRLinearClampSampler.put()));
		Util::SetResourceName(reflectionRRLinearClampSampler.get(), "DynamicCubemaps::ReflectionRRLinearClamp");
	}

	reflectionRRWidth = sourceDesc.Width;
	reflectionRRHeight = sourceDesc.Height;
	reflectionRRDepthHistoryIdx = 0;
	reflectionRRDepthHistoryValid = false;
	reflectionRRResultValid = false;
	return true;
}

void DynamicCubemaps::ReleaseReflectionRayRegenerationBindings()
{
	for (auto*& rtv : reflectionRRSavedRTVs) {
		if (rtv) {
			rtv->Release();
			rtv = nullptr;
		}
	}
	if (reflectionRRSavedDSV) {
		reflectionRRSavedDSV->Release();
		reflectionRRSavedDSV = nullptr;
	}
}

bool DynamicCubemaps::BeginReflectionRayRegenerationCapture()
{
	
    static bool rrTraceBeginLogged = false;
    if (!rrTraceBeginLogged) {
        logger::info("[ReflectionRRTrace] BeginCapture ENTER");
        rrTraceBeginLogged = true;
    }
reflectionRRCaptureActive = false;
	if (!EnsureReflectionRayRegenerationResources())
		return false;

	auto context = globals::d3d::context;
	context->OMGetRenderTargets(static_cast<UINT>(reflectionRRSavedRTVs.size()), reflectionRRSavedRTVs.data(), &reflectionRRSavedDSV);
	if (!reflectionRRSavedRTVs[0] || reflectionRRSavedRTVs[1]) {
		ReleaseReflectionRayRegenerationBindings();
		return false;
	}

	const float clearSignal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(reflectionRRSignal->rtv.get(), clearSignal);
	ID3D11RenderTargetView* targets[2] = { reflectionRRSavedRTVs[0], reflectionRRSignal->rtv.get() };
	context->OMSetRenderTargets(2, targets, reflectionRRSavedDSV);
	reflectionRRCaptureActive = true;
	
    static bool rrTraceCaptureLogged = false;
    if (!rrTraceCaptureLogged) {
        logger::info(
            "[ReflectionRRTrace] CAPTURE ACTIVE: size={}x{} RTV0={} RTV1={} DSV={}",
            reflectionRRWidth,
            reflectionRRHeight,
            reflectionRRSavedRTVs[0] != nullptr,
            reflectionRRSavedRTVs[1] != nullptr,
            reflectionRRSavedDSV != nullptr);
        rrTraceCaptureLogged = true;
    }

    return true;
}

namespace
{
	struct ReflectionRRPrepareConstants
	{
		uint32_t historyValid;
		uint32_t padding[3];
	};
}

void DynamicCubemaps::EndReflectionRayRegenerationCapture()
{
	if (!reflectionRRCaptureActive)
		return;
	reflectionRRCaptureActive = false;

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	auto restoreTargets = [&]() {
		context->OMSetRenderTargets(static_cast<UINT>(reflectionRRSavedRTVs.size()), reflectionRRSavedRTVs.data(), reflectionRRSavedDSV);
		ReleaseReflectionRayRegenerationBindings();
	};

	auto* prepare = reflectionRRPrepareCS.Get(L"Data\\Shaders\\DynamicCubemaps\\ReflectionRRPrepareCS.hlsl", {}, "cs_5_0");
	auto* compose = reflectionRRComposeCS.Get(L"Data\\Shaders\\DynamicCubemaps\\ReflectionRRComposeCS.hlsl", {}, "cs_5_0");
	if (!prepare || !compose) {
		restoreTargets();
		return;
	}

	auto renderer = globals::game::renderer;
	auto& rts = renderer->GetRuntimeData().renderTargets;
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& waterReflections = rts[RE::RENDER_TARGETS::kWATER_REFLECTIONS];
	const uint32_t previousDepth = reflectionRRDepthHistoryIdx;
	const uint32_t currentDepth = !reflectionRRDepthHistoryIdx;

	ID3D11ShaderResourceView* prepareSrvs[6] = {
		Util::AsReal(depth.depthSRV),
		Util::AsReal(rts[NORMALROUGHNESS].SRV),
		Util::AsReal(rts[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV),
		Util::AsReal(rts[REFLECTANCE].SRV),
		Util::AsReal(rts[ALBEDO].SRV),
		reflectionRRLinearDepth[previousDepth]->srv.get()
	};
	ID3D11UnorderedAccessView* prepareUavs[5] = {
		reflectionRRLinearDepth[currentDepth]->uav.get(),
		reflectionRRMotionVectors->uav.get(),
		reflectionRRNormalRoughness->uav.get(),
		reflectionRRSpecularAlbedo->uav.get(),
		reflectionRRDiffuseAlbedo->uav.get()
	};
	ID3D11SamplerState* sampler = reflectionRRLinearClampSampler.get();
	ID3D11SamplerState* savedSampler = nullptr;
	context->CSGetSamplers(0, 1, &savedSampler);
	ID3D11Buffer* savedFrameBuffers[2]{};
	context->CSGetConstantBuffers(12, 2, savedFrameBuffers);
	ID3D11Buffer* perFrame = *globals::game::perFrame.get();
	context->CSSetConstantBuffers(12, 1, &perFrame);
	if (globals::game::isVR) {
		static REL::Relocation<ID3D11Buffer**> vrValues{ REL::Offset(0x3180688) };
		ID3D11Buffer* vrBuffer = *vrValues.get();
		context->CSSetConstantBuffers(13, 1, &vrBuffer);
	}
	ReflectionRRPrepareConstants rrPrepareConstants{};
	rrPrepareConstants.historyValid = reflectionRRDepthHistoryValid ? 1u : 0u;

	D3D11_BUFFER_DESC rrConstantsDesc{};
	rrConstantsDesc.ByteWidth = sizeof(ReflectionRRPrepareConstants);
	rrConstantsDesc.Usage = D3D11_USAGE_IMMUTABLE;
	rrConstantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA rrConstantsData{};
	rrConstantsData.pSysMem = &rrPrepareConstants;

	Microsoft::WRL::ComPtr<ID3D11Buffer> rrConstants;
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&rrConstantsDesc, &rrConstantsData, rrConstants.GetAddressOf()));
	ID3D11Buffer* rrConstantsRaw = rrConstants.Get();

	context->CSSetShaderResources(0, 6, prepareSrvs);
	context->CSSetUnorderedAccessViews(0, 5, prepareUavs, nullptr);
	context->CSSetSamplers(0, 1, &sampler);
	context->CSSetConstantBuffers(0, 1, &rrConstantsRaw);
	context->CSSetShader(prepare, nullptr, 0);
	{
		CS_GPU_PASS("DynamicCubemaps::ReflectionRRPrepare");
		context->Dispatch((reflectionRRWidth + 7) / 8, (reflectionRRHeight + 7) / 8, 1);
	}
	ID3D11ShaderResourceView* nullSrvs[6]{};
	ID3D11UnorderedAccessView* nullUavs[5]{};
	context->CSSetShaderResources(0, 6, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 5, nullUavs, nullptr);
	ID3D11Buffer* nullRRConstants = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullRRConstants);
	context->CSSetConstantBuffers(12, 2, savedFrameBuffers);
	context->CSSetSamplers(0, 1, &savedSampler);
	if (savedSampler) {
		savedSampler->Release();
		savedSampler = nullptr;
	}
	for (auto*& buffer : savedFrameBuffers) {
		if (buffer) {
			buffer->Release();
			buffer = nullptr;
		}
	}

	const bool resetHistory = !reflectionRRDepthHistoryValid || (globals::state && globals::state->IsMainOrLoadingMenuOpen());
	const bool rrOk = globals::features::upscaling.fidelityFX.DispatchReflectionRayRegeneration(
		reflectionRRSignal->resource.get(), reflectionRRLinearDepth[currentDepth]->resource.get(), reflectionRRMotionVectors->resource.get(),
		reflectionRRNormalRoughness->resource.get(), reflectionRRSpecularAlbedo->resource.get(), reflectionRRDiffuseAlbedo->resource.get(),
		reflectionRROutput->resource.get(), reflectionRRWidth, reflectionRRHeight, resetHistory);

	reflectionRRDepthHistoryIdx = currentDepth;
	reflectionRRDepthHistoryValid = rrOk;
	reflectionRRResultValid = false;
	if (!rrOk) {
		restoreTargets();
		return;
	}

	ID3D11ShaderResourceView* composeSrvs[2] = { reflectionRROutput->srv.get(), Util::AsReal(waterReflections.SRV) };
	ID3D11UnorderedAccessView* composeUav = reflectionRRComposed->uav.get();
	context->CSSetShaderResources(0, 2, composeSrvs);
	context->CSSetUnorderedAccessViews(0, 1, &composeUav, nullptr);
	context->CSSetShader(compose, nullptr, 0);
	{
		CS_GPU_PASS("DynamicCubemaps::ReflectionRRCompose");
		context->Dispatch((reflectionRRWidth + 7) / 8, (reflectionRRHeight + 7) / 8, 1);
	}
	ID3D11ShaderResourceView* nullComposeSrvs[2]{};
	ID3D11UnorderedAccessView* nullComposeUav = nullptr;
	context->CSSetShaderResources(0, 2, nullComposeSrvs);
	context->CSSetUnorderedAccessViews(0, 1, &nullComposeUav, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
	reflectionRRResultValid = true;
	restoreTargets();
}

bool DynamicCubemaps::BeginReflectionRayRegenerationApply()
{
	reflectionRRApplyActive = false;
	if (!settings.EnableReflectionRayRegeneration || !reflectionRRResultValid || !reflectionRRComposed)
		return false;
	auto context = globals::d3d::context;
	context->PSGetShaderResources(0, 1, &reflectionRRSavedApplySRV);
	ID3D11ShaderResourceView* denoised = reflectionRRComposed->srv.get();
	context->PSSetShaderResources(0, 1, &denoised);
	reflectionRRApplyActive = true;
	return true;
}

void DynamicCubemaps::EndReflectionRayRegenerationApply()
{
	if (!reflectionRRApplyActive)
		return;
	reflectionRRApplyActive = false;
	auto context = globals::d3d::context;
	context->PSSetShaderResources(0, 1, &reflectionRRSavedApplySRV);
	if (reflectionRRSavedApplySRV) {
		reflectionRRSavedApplySRV->Release();
		reflectionRRSavedApplySRV = nullptr;
	}
}

void DynamicCubemaps::SetupResources()
{
	reflectionRRPrepareCS.Get(L"Data\\Shaders\\DynamicCubemaps\\ReflectionRRPrepareCS.hlsl", {}, "cs_5_0");
	reflectionRRComposeCS.Get(L"Data\\Shaders\\DynamicCubemaps\\ReflectionRRComposeCS.hlsl", {}, "cs_5_0");
	GetComputeShaderUpdate();
	GetComputeShaderUpdateReflections();
	GetComputeShaderInferrence();
	GetComputeShaderInferrenceReflections();
	GetComputeShaderSpecularIrradiance();
	GetComputeShaderBC6HEncode();

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &computeSampler));
		Util::SetResourceName(computeSampler, "DynamicCubemaps::ComputeSampler");
	}

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	{
		D3D11_TEXTURE2D_DESC texDesc;
		cubemap.texture->GetDesc(Util::AsW32(&texDesc));
		assert(texDesc.Width == (1u << (MIPLEVELS - 1)));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		cubemap.SRV->GetDesc(Util::AsW32(&srvDesc));

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		// Create additional resources

		texDesc.MipLevels = MIPLEVELS;
		texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		srvDesc.TextureCube.MipLevels = MIPLEVELS;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = texDesc.ArraySize;

		envCaptureTexture = new Texture2D(texDesc);
		envCaptureTexture->CreateSRV(srvDesc);
		envCaptureTexture->CreateUAV(uavDesc);

		envCaptureRawTexture = new Texture2D(texDesc);
		envCaptureRawTexture->CreateSRV(srvDesc);
		envCaptureRawTexture->CreateUAV(uavDesc);

		envCapturePositionTexture = new Texture2D(texDesc);
		envCapturePositionTexture->CreateSRV(srvDesc);
		envCapturePositionTexture->CreateUAV(uavDesc);

		envCaptureReflectionsTexture = new Texture2D(texDesc);
		envCaptureReflectionsTexture->CreateSRV(srvDesc);
		envCaptureReflectionsTexture->CreateUAV(uavDesc);

		envCaptureRawReflectionsTexture = new Texture2D(texDesc);
		envCaptureRawReflectionsTexture->CreateSRV(srvDesc);
		envCaptureRawReflectionsTexture->CreateUAV(uavDesc);

		envCapturePositionReflectionsTexture = new Texture2D(texDesc);
		envCapturePositionReflectionsTexture->CreateSRV(srvDesc);
		envCapturePositionReflectionsTexture->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		envTexture = new Texture2D(texDesc);
		envTexture->CreateSRV(srvDesc);
		envTexture->CreateUAV(uavDesc);

		envReflectionsTexture = new Texture2D(texDesc);
		envReflectionsTexture->CreateSRV(srvDesc);
		envReflectionsTexture->CreateUAV(uavDesc);

		// Texture2DArray SRVs used by BC6H encoder (Load() requires array dimension, not TextureCube)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC arraySRVDesc = {};
			arraySRVDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			arraySRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			arraySRVDesc.Texture2DArray.FirstArraySlice = 0;
			arraySRVDesc.Texture2DArray.ArraySize = 6;
			arraySRVDesc.Texture2DArray.MostDetailedMip = 0;
			arraySRVDesc.Texture2DArray.MipLevels = MIPLEVELS;
			DX::ThrowIfFailed(device->CreateShaderResourceView(envTexture->resource.get(), &arraySRVDesc, &envTextureArraySRV));
			Util::SetResourceName(envTextureArraySRV, "DynamicCubemaps::EnvTexture ArraySRV");
			DX::ThrowIfFailed(device->CreateShaderResourceView(envReflectionsTexture->resource.get(), &arraySRVDesc, &envReflectionsTextureArraySRV));
			Util::SetResourceName(envReflectionsTextureArraySRV, "DynamicCubemaps::EnvReflections ArraySRV");
		}

		envInferredTexture = new Texture2D(texDesc, "DynamicCubemaps::EnvInferred");
		envInferredTexture->CreateSRV(srvDesc);
		envInferredTexture->CreateUAV(uavDesc);

		// BC6H scratch: R32G32B32A32_UINT at quarter-resolution, 6-face array.
		// Encoded directly into here via UAV, then CopyResource'd to the BC6H texture.
		// Mip count must match the BC6H target so block-equivalent dimensions align.
		{
			std::uint32_t scratchBase = std::max(1u, texDesc.Width / 4);
			bc6hMipLevels = 0;
			for (std::uint32_t d = scratchBase; d > 0; d >>= 1)
				++bc6hMipLevels;
			// Clamp: must not exceed envTexture's mip count (source reads) or the UAV array size.
			bc6hMipLevels = std::min<std::uint32_t>(bc6hMipLevels, MIPLEVELS);
			bc6hMipLevels = std::min<std::uint32_t>(bc6hMipLevels, static_cast<std::uint32_t>(std::size(bc6hScratchUAVs)));

			D3D11_TEXTURE2D_DESC scratchDesc = {};
			scratchDesc.Width = scratchBase;
			scratchDesc.Height = std::max(1u, texDesc.Height / 4);
			scratchDesc.MipLevels = bc6hMipLevels;
			scratchDesc.ArraySize = 6;
			scratchDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			scratchDesc.SampleDesc.Count = 1;
			scratchDesc.Usage = D3D11_USAGE_DEFAULT;
			scratchDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			scratchDesc.MiscFlags = 0;
			bc6hScratchTexture = new Texture2D(scratchDesc, "DynamicCubemaps::BC6HScratch");

			D3D11_UNORDERED_ACCESS_VIEW_DESC scratchUAVDesc = {};
			scratchUAVDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			scratchUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			scratchUAVDesc.Texture2DArray.FirstArraySlice = 0;
			scratchUAVDesc.Texture2DArray.ArraySize = 6;
			for (std::uint32_t level = 0; level < bc6hMipLevels; ++level) {
				scratchUAVDesc.Texture2DArray.MipSlice = level;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(bc6hScratchTexture->resource.get(), &scratchUAVDesc, &bc6hScratchUAVs[level]));
				Util::SetResourceName(bc6hScratchUAVs[level], "DynamicCubemaps::BC6HScratch UAV mip%u", level);
			}
		}

		// BC6H compressed cubemap textures (shader-read-only).
		{
			D3D11_TEXTURE2D_DESC bc6hDesc = {};
			bc6hDesc.Width = texDesc.Width;
			bc6hDesc.Height = texDesc.Height;
			bc6hDesc.MipLevels = bc6hMipLevels;
			bc6hDesc.ArraySize = 6;
			bc6hDesc.Format = DXGI_FORMAT_BC6H_UF16;
			bc6hDesc.SampleDesc.Count = 1;
			bc6hDesc.Usage = D3D11_USAGE_DEFAULT;
			bc6hDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bc6hDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

			D3D11_SHADER_RESOURCE_VIEW_DESC bc6hSRVDesc = {};
			bc6hSRVDesc.Format = DXGI_FORMAT_BC6H_UF16;
			bc6hSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			bc6hSRVDesc.TextureCube.MostDetailedMip = 0;
			bc6hSRVDesc.TextureCube.MipLevels = bc6hMipLevels;

			envTextureBC6H = new Texture2D(bc6hDesc, "DynamicCubemaps::EnvTextureBC6H");
			envTextureBC6H->CreateSRV(bc6hSRVDesc);

			envReflectionsTextureBC6H = new Texture2D(bc6hDesc, "DynamicCubemaps::EnvReflectionsBC6H");
			envReflectionsTextureBC6H->CreateSRV(bc6hSRVDesc);
		}

		updateCubemapCB = new ConstantBuffer(ConstantBufferDesc<UpdateCubemapCB>(), "DynamicCubemaps::UpdateCubemapCB");
	}

	{
		bc6hEncodeCB = new ConstantBuffer(ConstantBufferDesc<BC6HEncodeCB>(), "DynamicCubemaps::BC6HEncodeCB");
	}

	{
		spmapCB = new ConstantBuffer(ConstantBufferDesc<SpecularMapFilterSettingsCB>(), "DynamicCubemaps::SpmapCB");
	}

	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = envTexture->desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;

		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = envTexture->desc.ArraySize;

		for (std::uint32_t level = 1; level < MIPLEVELS; ++level) {
			uavDesc.Texture2DArray.MipSlice = level;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(envTexture->resource.get(), &uavDesc, &uavArray[level - 1]));
			Util::SetResourceName(uavArray[level - 1], "DynamicCubemaps::EnvTexture UAV mip%u", level);
		}

		for (std::uint32_t level = 1; level < MIPLEVELS; ++level) {
			uavDesc.Texture2DArray.MipSlice = level;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(envReflectionsTexture->resource.get(), &uavDesc, &uavReflectionsArray[level - 1]));
			Util::SetResourceName(uavReflectionsArray[level - 1], "DynamicCubemaps::EnvReflections UAV mip%u", level);
		}
	}

	{
		DirectX::CreateDDSTextureFromFile(device, L"Data\\Shaders\\DynamicCubemaps\\defaultcubemap.dds", nullptr, &defaultCubemap);
	}
}

void DynamicCubemaps::Reset()
{
	reflectionRRDepthHistoryValid = false;
	reflectionRRResultValid = false;
	activeReflections = globals::state->activeReflections;

	if (globals::game::sky)
		fakeReflections = activeReflections && globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
	else
		fakeReflections = false;

	if (!activeReflections && !Util::IsInterior()) {
		activeReflections = true;
		fakeReflections = true;
	}
}
#undef I18N_KEY_PREFIX
