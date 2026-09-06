// SPDX-License-Identifier: GPL-3.0+

#include "GSDLSS5NR.h"

#ifdef _WIN32

#include "common/Console.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	using NVSDK_NGX_Result = int;
	static constexpr NVSDK_NGX_Result NGX_SUCCESS = 1;
	static constexpr int NR_FEATURE_ID = 18;

	struct NVSDK_NGX_PathListInfo
	{
		const wchar_t* const* Path;
		unsigned int Length;
	};

	enum NVSDK_NGX_Logging_Level
	{
		NVSDK_NGX_LOGGING_LEVEL_OFF = 0,
		NVSDK_NGX_LOGGING_LEVEL_ON,
		NVSDK_NGX_LOGGING_LEVEL_VERBOSE
	};

	using NVSDK_NGX_AppLogCallback = void(__cdecl*)(const char*, NVSDK_NGX_Logging_Level, int);

	struct NVSDK_NGX_LoggingInfo
	{
		NVSDK_NGX_Logging_Level LoggingLevel;
		NVSDK_NGX_AppLogCallback Callback;
		void* UserData;
		bool DisableOtherLoggingSinks;
	};

	struct NVSDK_NGX_FeatureCommonInfo_Internal;

	struct NVSDK_NGX_FeatureCommonInfo
	{
		NVSDK_NGX_PathListInfo PathListInfo;
		NVSDK_NGX_FeatureCommonInfo_Internal* InternalData;
		NVSDK_NGX_LoggingInfo LoggingInfo;
	};

	using PFN_InitProjectID = NVSDK_NGX_Result(*)(const char*, int, const char*, const wchar_t*,
		ID3D12Device*, int, const void*);
	using PFN_InitExt = NVSDK_NGX_Result(*)(unsigned long long, const wchar_t*, ID3D12Device*, int, const void*);
	using PFN_AllocateParameters = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter**);
	using PFN_CreateFeature = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*, int,
		NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
	using PFN_EvaluateFeature = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*,
		const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, void*);
	using PFN_ReleaseFeature = NVSDK_NGX_Result(*)(NVSDK_NGX_Handle*);
	using PFN_Shutdown = NVSDK_NGX_Result(*)();

	using PFN_ShimInit = NVSDK_NGX_Result(*)(void*, unsigned long long, const wchar_t*,
		ID3D12Device*, int, const void*);
	using PFN_ShimCreate = NVSDK_NGX_Result(*)(void*, ID3D12GraphicsCommandList*, int,
		NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
	using PFN_ShimEvaluate = NVSDK_NGX_Result(*)(void*, ID3D12GraphicsCommandList*,
		const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, void*);
	using PFN_ShimRelease = NVSDK_NGX_Result(*)(void*, NVSDK_NGX_Handle*);

	PFN_InitProjectID g_init_project_id = nullptr;
	PFN_AllocateParameters g_allocate_parameters = nullptr;
	PFN_Shutdown g_shutdown = nullptr;

	PFN_InitExt g_nr_init = nullptr;
	PFN_CreateFeature g_nr_create = nullptr;
	PFN_EvaluateFeature g_nr_evaluate = nullptr;
	PFN_ReleaseFeature g_nr_release = nullptr;

	PFN_ShimInit g_shim_init = nullptr;
	PFN_ShimCreate g_shim_create = nullptr;
	PFN_ShimEvaluate g_shim_evaluate = nullptr;
	PFN_ShimRelease g_shim_release = nullptr;

	std::filesystem::path ExecutableDirectory()
	{
		std::array<wchar_t, 32768> path{};
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size())
			return std::filesystem::current_path();

		return std::filesystem::path(path.data()).parent_path();
	}

	DXGI_FORMAT NormalizeSwapFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			default:
				return format;
		}
	}

	bool IsSupportedSwapFormat(DXGI_FORMAT format)
	{
		format = NormalizeSwapFormat(format);
		return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			format == DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}

namespace GSDLSS5NR
{
	DirectRenderer::~DirectRenderer()
	{
		Shutdown();
	}

	void DirectRenderer::Log(const char* format, ...)
	{
		std::array<char, 2048> text{};
		std::va_list args;
		va_start(args, format);
		vsnprintf(text.data(), text.size(), format, args);
		va_end(args);
		Console.WriteLn(Color_StrongGreen, "DLSS5-NR: %s", text.data());
	}

	void DirectRenderer::LogFailureOnce(const char* message)
	{
		if (m_logged_failure)
			return;

		m_logged_failure = true;
		Console.Error("DLSS5-NR: %s", message);
	}

	D3D12_RESOURCE_BARRIER DirectRenderer::Transition(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		return barrier;
	}

	bool DirectRenderer::CreateD3D12Companion(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
			FAILED(dxgi_device->GetAdapter(&adapter)))
		{
			LogFailureOnce("cannot resolve the D3D11 adapter.");
			return false;
		}

		if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d12_device))))
		{
			LogFailureOnce("D3D12 companion device creation failed. DLSS 5 NR currently needs a D3D12-capable NVIDIA adapter.");
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC queue_desc{};
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(m_d12_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_d12_queue))) ||
			FAILED(m_d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_d12_allocator))) ||
			FAILED(m_d12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_d12_allocator.Get(),
				nullptr, IID_PPV_ARGS(&m_d12_list))) ||
			FAILED(m_d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d12_fence))))
		{
			LogFailureOnce("D3D12 command infrastructure creation failed.");
			return false;
		}

		m_d12_list->Close();

		device->QueryInterface(IID_PPV_ARGS(&m_d11_device5));
		context->QueryInterface(IID_PPV_ARGS(&m_d11_context4));

		if (m_d11_device5 && m_d11_context4 &&
			SUCCEEDED(m_d11_device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_d11_fence))))
		{
			HANDLE shared_fence = nullptr;
			if (SUCCEEDED(m_d11_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared_fence)))
			{
				m_d12_device->OpenSharedHandle(shared_fence, IID_PPV_ARGS(&m_d12_shared_fence));
				CloseHandle(shared_fence);
			}
		}

		return true;
	}

	void DirectRenderer::DestroySizeDependentResources()
	{
		if (m_ngx_feature)
		{
			if (g_shim_release && g_nr_release)
				g_shim_release(reinterpret_cast<void*>(g_nr_release), m_ngx_feature);
			else if (g_nr_release)
				g_nr_release(m_ngx_feature);
			m_ngx_feature = nullptr;
		}

		m_convert_from_nr.Reset();
		m_convert_to_nr.Reset();
		m_root_signature.Reset();
		m_descriptor_heap.Reset();
		m_nr_output.Reset();
		m_nr_input.Reset();
		m_d12_shared_output.Reset();
		m_d12_shared_input.Reset();
		m_d11_shared_output.Reset();
		m_d11_shared_input.Reset();

		m_width = 0;
		m_height = 0;
		m_backbuffer_format = DXGI_FORMAT_UNKNOWN;
	}

	bool DirectRenderer::CreateInteropResources(unsigned int width, unsigned int height, DXGI_FORMAT format)
	{
		format = NormalizeSwapFormat(format);
		if (!IsSupportedSwapFormat(format))
		{
			LogFailureOnce("swap-chain format is not RGBA8/BGRA8.");
			return false;
		}

		D3D11_TEXTURE2D_DESC shared_desc{};
		shared_desc.Width = width;
		shared_desc.Height = height;
		shared_desc.MipLevels = 1;
		shared_desc.ArraySize = 1;
		shared_desc.Format = format;
		shared_desc.SampleDesc.Count = 1;
		shared_desc.Usage = D3D11_USAGE_DEFAULT;
		shared_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		shared_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		if (FAILED(m_host_device->CreateTexture2D(&shared_desc, nullptr, &m_d11_shared_input)) ||
			FAILED(m_host_device->CreateTexture2D(&shared_desc, nullptr, &m_d11_shared_output)))
		{
			LogFailureOnce("failed to create D3D11 shared frame textures.");
			return false;
		}

		auto open_shared = [&](ID3D11Texture2D* source, ComPtrD12Resource& destination) -> bool
		{
			Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
			HANDLE handle = nullptr;
			if (FAILED(source->QueryInterface(IID_PPV_ARGS(&dxgi_resource))) ||
				FAILED(dxgi_resource->GetSharedHandle(&handle)) ||
				!handle)
				return false;

			const HRESULT hr = m_d12_device->OpenSharedHandle(handle, IID_PPV_ARGS(&destination));
			return SUCCEEDED(hr);
		};

		if (!open_shared(m_d11_shared_input.Get(), m_d12_shared_input) ||
			!open_shared(m_d11_shared_output.Get(), m_d12_shared_output))
		{
			LogFailureOnce("D3D11/D3D12 shared texture interop failed.");
			return false;
		}

		auto create_d12_texture = [&](DXGI_FORMAT texture_format, D3D12_RESOURCE_FLAGS flags,
			D3D12_RESOURCE_STATES initial_state, ComPtrD12Resource& out) -> bool
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Width = width;
			desc.Height = height;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = texture_format;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			desc.Flags = flags;

			return SUCCEEDED(m_d12_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
				&desc, initial_state, nullptr, IID_PPV_ARGS(&out)));
		};

		if (!create_d12_texture(DXGI_FORMAT_R16G16B16A16_FLOAT,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_nr_input) ||
			!create_d12_texture(DXGI_FORMAT_R16G16B16A16_FLOAT,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_nr_output))
		{
			LogFailureOnce("failed to create DLSS 5 NR RGBA16F resources.");
			return false;
		}

		m_width = width;
		m_height = height;
		m_backbuffer_format = format;
		return true;
	}

	bool DirectRenderer::CreateComputePipeline()
	{
		const char* shader_to_nr =
			"Texture2D<float4> Source : register(t0);\n"
			"RWTexture2D<float4> Destination : register(u0);\n"
			"[numthreads(16,16,1)]\n"
			"void CSMain(uint3 id : SV_DispatchThreadID)\n"
			"{\n"
			"    uint width, height;\n"
			"    Destination.GetDimensions(width, height);\n"
			"    if (id.x >= width || id.y >= height) return;\n"
			"    Destination[id.xy] = Source[id.xy];\n"
			"}\n";

		const char* shader_from_nr =
			"Texture2D<float4> Source : register(t0);\n"
			"RWTexture2D<unorm float4> Destination : register(u0);\n"
			"[numthreads(16,16,1)]\n"
			"void CSMain(uint3 id : SV_DispatchThreadID)\n"
			"{\n"
			"    uint width, height;\n"
			"    Destination.GetDimensions(width, height);\n"
			"    if (id.x >= width || id.y >= height) return;\n"
			"    Destination[id.xy] = Source[id.xy];\n"
			"}\n";

		Microsoft::WRL::ComPtr<ID3DBlob> blob_to_nr;
		Microsoft::WRL::ComPtr<ID3DBlob> blob_from_nr;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;

		if (FAILED(D3DCompile(shader_to_nr, std::strlen(shader_to_nr), "PCSX2DLSS5ToNR",
				nullptr, nullptr, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				&blob_to_nr, &errors)))
		{
			LogFailureOnce("failed to compile the D3D12 input conversion shader.");
			return false;
		}

		errors.Reset();
		if (FAILED(D3DCompile(shader_from_nr, std::strlen(shader_from_nr), "PCSX2DLSS5FromNR",
				nullptr, nullptr, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				&blob_from_nr, &errors)))
		{
			LogFailureOnce("failed to compile the D3D12 output conversion shader.");
			return false;
		}

		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;

		D3D12_ROOT_PARAMETER parameters[2]{};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[0].DescriptorTable.NumDescriptorRanges = 1;
		parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
		parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable.NumDescriptorRanges = 1;
		parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];

		D3D12_ROOT_SIGNATURE_DESC root_desc{};
		root_desc.NumParameters = 2;
		root_desc.pParameters = parameters;

		Microsoft::WRL::ComPtr<ID3DBlob> serialized_root;
		if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
				&serialized_root, &errors)) ||
			FAILED(m_d12_device->CreateRootSignature(0, serialized_root->GetBufferPointer(),
				serialized_root->GetBufferSize(), IID_PPV_ARGS(&m_root_signature))))
		{
			LogFailureOnce("failed to create the DLSS 5 conversion root signature.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
		pipeline_desc.pRootSignature = m_root_signature.Get();
		pipeline_desc.CS = {blob_to_nr->GetBufferPointer(), blob_to_nr->GetBufferSize()};
		if (FAILED(m_d12_device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&m_convert_to_nr))))
			return false;

		pipeline_desc.CS = {blob_from_nr->GetBufferPointer(), blob_from_nr->GetBufferSize()};
		if (FAILED(m_d12_device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&m_convert_from_nr))))
			return false;

		D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = 4;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_d12_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_descriptor_heap))))
			return false;

		const UINT increment =
			m_d12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		srv.Format = m_backbuffer_format;
		m_d12_device->CreateShaderResourceView(m_d12_shared_input.Get(), &srv, cpu);

		cpu.ptr += increment;
		uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		m_d12_device->CreateUnorderedAccessView(m_nr_input.Get(), nullptr, &uav, cpu);

		cpu.ptr += increment;
		srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		m_d12_device->CreateShaderResourceView(m_nr_output.Get(), &srv, cpu);

		cpu.ptr += increment;
		uav.Format = m_backbuffer_format;
		m_d12_device->CreateUnorderedAccessView(m_d12_shared_output.Get(), nullptr, &uav, cpu);

		return true;
	}

	bool DirectRenderer::BeginD3D12Commands()
	{
		if (FAILED(m_d12_allocator->Reset()))
			return false;

		return SUCCEEDED(m_d12_list->Reset(m_d12_allocator.Get(), nullptr));
	}

	bool DirectRenderer::ExecuteAndWait()
	{
		if (FAILED(m_d12_list->Close()))
			return false;

		ID3D12CommandList* lists[] = {m_d12_list.Get()};
		m_d12_queue->ExecuteCommandLists(1, lists);

		const std::uint64_t value = ++m_d12_fence_value;
		if (FAILED(m_d12_queue->Signal(m_d12_fence.Get(), value)))
			return false;

		if (m_d12_fence->GetCompletedValue() < value)
		{
			HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!event)
				return false;

			const HRESULT hr = m_d12_fence->SetEventOnCompletion(value, event);
			if (SUCCEEDED(hr))
				WaitForSingleObject(event, 20000);
			CloseHandle(event);

			if (FAILED(hr))
				return false;
		}

		return true;
	}

	bool DirectRenderer::LoadNGX(unsigned int width, unsigned int height)
	{
		const std::filesystem::path directory = ExecutableDirectory();
		const std::filesystem::path core_path = directory / L"_nvngx.dll";
		const std::filesystem::path nr_path = directory / L"nvngx_dlssnr.dll";
		const std::filesystem::path shim_path = directory / L"caller" / L"nvngx.dll";

		m_ngx_core = LoadLibraryW(core_path.c_str());
		m_ngx_nr = LoadLibraryW(nr_path.c_str());
		m_caller_shim = LoadLibraryW(shim_path.c_str());

		if (!m_ngx_core || !m_ngx_nr || !m_caller_shim)
		{
			LogFailureOnce("missing _nvngx.dll, nvngx_dlssnr.dll, or caller\\nvngx.dll beside pcsx2-qt.exe.");
			return false;
		}

		g_init_project_id = reinterpret_cast<PFN_InitProjectID>(
			GetProcAddress(m_ngx_core, "NVSDK_NGX_D3D12_Init_ProjectID"));
		g_allocate_parameters = reinterpret_cast<PFN_AllocateParameters>(
			GetProcAddress(m_ngx_core, "NVSDK_NGX_D3D12_AllocateParameters"));
		g_shutdown = reinterpret_cast<PFN_Shutdown>(
			GetProcAddress(m_ngx_core, "NVSDK_NGX_D3D12_Shutdown"));

		g_nr_init = reinterpret_cast<PFN_InitExt>(
			GetProcAddress(m_ngx_nr, "NVSDK_NGX_D3D12_Init_Ext"));
		g_nr_create = reinterpret_cast<PFN_CreateFeature>(
			GetProcAddress(m_ngx_nr, "NVSDK_NGX_D3D12_CreateFeature"));
		g_nr_evaluate = reinterpret_cast<PFN_EvaluateFeature>(
			GetProcAddress(m_ngx_nr, "NVSDK_NGX_D3D12_EvaluateFeature"));
		g_nr_release = reinterpret_cast<PFN_ReleaseFeature>(
			GetProcAddress(m_ngx_nr, "NVSDK_NGX_D3D12_ReleaseFeature"));

		g_shim_init = reinterpret_cast<PFN_ShimInit>(
			GetProcAddress(m_caller_shim, "DLSSNR_CallInit"));
		g_shim_create = reinterpret_cast<PFN_ShimCreate>(
			GetProcAddress(m_caller_shim, "DLSSNR_CallCreate"));
		g_shim_evaluate = reinterpret_cast<PFN_ShimEvaluate>(
			GetProcAddress(m_caller_shim, "DLSSNR_CallEvaluate"));
		g_shim_release = reinterpret_cast<PFN_ShimRelease>(
			GetProcAddress(m_caller_shim, "DLSSNR_CallRelease"));

		if (!g_init_project_id || !g_allocate_parameters || !g_nr_init || !g_nr_create ||
			!g_nr_evaluate || !g_nr_release || !g_shim_init || !g_shim_create ||
			!g_shim_evaluate || !g_shim_release)
		{
			LogFailureOnce("required NGX/DLSSNR/caller-shim exports are missing.");
			return false;
		}

		const std::wstring data_path = directory.wstring();
		const wchar_t* paths[] = {data_path.c_str()};

		NVSDK_NGX_FeatureCommonInfo common{};
		common.PathListInfo.Path = paths;
		common.PathListInfo.Length = 1;
		common.LoggingInfo.LoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;

		bool core_initialized = false;
		for (int api_version = 0x13; api_version <= 0x20; api_version++)
		{
			const NVSDK_NGX_Result result = g_init_project_id(
				"53f803cc-a12f-4d69-90d5-19b7599cad19",
				0, "1.0", data_path.c_str(), m_d12_device.Get(), api_version, nullptr);
			if (result == NGX_SUCCESS)
			{
				core_initialized = true;
				break;
			}
		}

		if (!core_initialized)
		{
			LogFailureOnce("NVSDK_NGX_D3D12_Init_ProjectID failed.");
			return false;
		}

		static constexpr unsigned long long APP_ID = 141959980ULL;
		const NVSDK_NGX_Result nr_init_result = g_shim_init(
			reinterpret_cast<void*>(g_nr_init), APP_ID, data_path.c_str(), m_d12_device.Get(),
			0x15, &common);

		if (nr_init_result != NGX_SUCCESS)
		{
			LogFailureOnce("DLSSNR Init_Ext through caller shim failed.");
			return false;
		}

		const NVSDK_NGX_Result allocate_result = g_allocate_parameters(&m_ngx_params);
		if (allocate_result != NGX_SUCCESS || !m_ngx_params)
		{
			LogFailureOnce("NGX AllocateParameters failed.");
			return false;
		}

		return RecreateFeature(width, height);
	}

	bool DirectRenderer::RecreateFeature(unsigned int width, unsigned int height)
	{
		if (!m_ngx_params)
			return false;

		if (m_ngx_feature)
		{
			g_shim_release(reinterpret_cast<void*>(g_nr_release), m_ngx_feature);
			m_ngx_feature = nullptr;
		}

		m_ngx_params->Reset();
		m_ngx_params->Set("DLSSNR.Width", width);
		m_ngx_params->Set("DLSSNR.Height", height);
		m_ngx_params->Set("DLSSNR.Enabled", 1);
		m_ngx_params->Set("DLSSNR.Reset", 1);
		m_ngx_params->Set("DLSSNR.Style", 1);
		m_ngx_params->Set("DLSSNR.Hint.Render.Preset", 3);
		m_ngx_params->Set("DLSSNR.Intensity", 1.0f);
		m_ngx_params->Set("DLSSNR.LocalToneStrength", 1.0f);
		m_ngx_params->Set("DLSSNR.LocalStructureStrength", 1.0f);
		m_ngx_params->Set("DLSSNR.SkinStructureStrength", -1.0f);
		m_ngx_params->Set("DLSSNR.UseAutoMask", 0);
		m_ngx_params->Set("DLSSNR.UICorrection", 0);
		m_ngx_params->Set("DLSSNR.DepthInverted", 1);
		m_ngx_params->Set("DLSSNR.ScalingRatio", 1.0f);
		m_ngx_params->Set("DLSSNR.MVecScaleX", 1.0f);
		m_ngx_params->Set("DLSSNR.MVecScaleY", 1.0f);
		m_ngx_params->Set("DLSSNR.Color", m_nr_input.Get());
		m_ngx_params->Set("DLSSNR.Output", m_nr_output.Get());
		m_ngx_params->Set("DLSSNR.Backbuffer", m_nr_output.Get());
		m_ngx_params->Set("DLSSNR.ColorSubrectBaseX", 0);
		m_ngx_params->Set("DLSSNR.ColorSubrectBaseY", 0);
		m_ngx_params->Set("DLSSNR.ColorSubrectWidth", width);
		m_ngx_params->Set("DLSSNR.ColorSubrectHeight", height);
		m_ngx_params->Set("DLSSNR.OutputSubrectBaseX", 0);
		m_ngx_params->Set("DLSSNR.OutputSubrectBaseY", 0);
		m_ngx_params->Set("DLSSNR.OutputSubrectWidth", width);
		m_ngx_params->Set("DLSSNR.OutputSubrectHeight", height);

		if (!BeginD3D12Commands())
			return false;

		const NVSDK_NGX_Result create_result = g_shim_create(
			reinterpret_cast<void*>(g_nr_create), m_d12_list.Get(), NR_FEATURE_ID,
			m_ngx_params, &m_ngx_feature);

		if (create_result != NGX_SUCCESS || !m_ngx_feature)
		{
			m_d12_list->Close();
			LogFailureOnce("NGX feature 18 creation failed.");
			return false;
		}

		if (!ExecuteAndWait())
			return false;

		Log("feature 18 created for %ux%u.", width, height);
		return true;
	}

	bool DirectRenderer::EnsureInitialized(ID3D11Device* device, ID3D11DeviceContext* context,
		IDXGISwapChain1* swap_chain, unsigned int width, unsigned int height, DXGI_FORMAT format)
	{
		if (m_failed)
			return false;

		if (!m_d12_device)
		{
			m_host_device = device;
			m_host_context = context;
			m_swap_chain = swap_chain;

			if (!CreateD3D12Companion(device, context))
			{
				m_failed = true;
				return false;
			}
		}

		format = NormalizeSwapFormat(format);
		if (m_width == width && m_height == height && m_backbuffer_format == format &&
			m_initialized)
			return true;

		DestroySizeDependentResources();

		if (!CreateInteropResources(width, height, format) ||
			!CreateComputePipeline() ||
			(!m_ngx_core && !LoadNGX(width, height)) ||
			(m_ngx_core && !RecreateFeature(width, height)))
		{
			m_failed = true;
			return false;
		}

		m_initialized = true;
		m_frame_index = 0;
		Log("direct PCSX2 neural-rendering path initialized.");
		return true;
	}

	bool DirectRenderer::EvaluateFrame(ID3D11Texture2D* backbuffer)
	{
		m_host_context->CopyResource(m_d11_shared_input.Get(), backbuffer);

		if (m_d11_context4 && m_d11_fence && m_d12_shared_fence)
		{
			const std::uint64_t value = ++m_interop_fence_value;
			if (FAILED(m_d11_context4->Signal(m_d11_fence.Get(), value)))
				return false;
			if (FAILED(m_d12_queue->Wait(m_d12_shared_fence.Get(), value)))
				return false;
		}
		else
		{
			m_host_context->Flush();
		}

		if (!BeginD3D12Commands())
			return false;

		ID3D12DescriptorHeap* heaps[] = {m_descriptor_heap.Get()};
		m_d12_list->SetDescriptorHeaps(1, heaps);
		m_d12_list->SetComputeRootSignature(m_root_signature.Get());

		const UINT increment =
			m_d12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const D3D12_GPU_DESCRIPTOR_HANDLE gpu_base = m_descriptor_heap->GetGPUDescriptorHandleForHeapStart();

		D3D12_RESOURCE_BARRIER input_barrier = Transition(
			m_d12_shared_input.Get(), D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		m_d12_list->ResourceBarrier(1, &input_barrier);

		m_d12_list->SetPipelineState(m_convert_to_nr.Get());
		m_d12_list->SetComputeRootDescriptorTable(0, gpu_base);
		m_d12_list->SetComputeRootDescriptorTable(1, {gpu_base.ptr + increment});
		m_d12_list->Dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);

		D3D12_RESOURCE_BARRIER uav_barrier{};
		uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uav_barrier.UAV.pResource = m_nr_input.Get();
		m_d12_list->ResourceBarrier(1, &uav_barrier);

		m_ngx_params->Set("DLSSNR.Reset", m_frame_index == 0 ? 1 : 0);
		m_ngx_params->Set("DLSSNR.Color", m_nr_input.Get());
		m_ngx_params->Set("DLSSNR.Output", m_nr_output.Get());
		m_ngx_params->Set("DLSSNR.Backbuffer", m_nr_output.Get());

		const NVSDK_NGX_Result evaluate_result = g_shim_evaluate(
			reinterpret_cast<void*>(g_nr_evaluate), m_d12_list.Get(), m_ngx_feature,
			m_ngx_params, nullptr);
		if (evaluate_result != NGX_SUCCESS)
		{
			m_d12_list->Close();
			LogFailureOnce("NGX feature 18 evaluation failed.");
			return false;
		}

		D3D12_RESOURCE_BARRIER nr_barrier{};
		nr_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		nr_barrier.UAV.pResource = m_nr_output.Get();
		m_d12_list->ResourceBarrier(1, &nr_barrier);

		m_d12_list->SetPipelineState(m_convert_from_nr.Get());
		m_d12_list->SetComputeRootDescriptorTable(0, {gpu_base.ptr + 2ULL * increment});
		m_d12_list->SetComputeRootDescriptorTable(1, {gpu_base.ptr + 3ULL * increment});
		m_d12_list->Dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);

		D3D12_RESOURCE_BARRIER output_barrier{};
		output_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		output_barrier.UAV.pResource = m_d12_shared_output.Get();
		m_d12_list->ResourceBarrier(1, &output_barrier);

		D3D12_RESOURCE_BARRIER restore_input = Transition(
			m_d12_shared_input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON);
		m_d12_list->ResourceBarrier(1, &restore_input);

		if (!ExecuteAndWait())
			return false;

		m_host_context->CopyResource(backbuffer, m_d11_shared_output.Get());
		m_frame_index++;
		return true;
	}

	void DirectRenderer::Process(ID3D11Device* device, ID3D11DeviceContext* context,
		IDXGISwapChain1* swap_chain)
	{
		if (!device || !context || !swap_chain || m_failed)
			return;

		// DLSS5oneclick's generic no-DLSS path is a complete ReShade/Feeder pipeline.
		// Never run the experimental direct feature-18 path on top of it.
		if (!m_external_path_checked)
		{
			const std::filesystem::path directory = ExecutableDirectory();
			m_external_path_present =
				std::filesystem::exists(directory / L"standalone-dlssnr.addon64") ||
				std::filesystem::exists(directory / L"dlss5-feed.addon64") ||
				(std::filesystem::exists(directory / L"renodx-dlss5.addon64") &&
				 std::filesystem::exists(directory / L"dxgi.dll"));
			m_external_path_checked = true;

			if (m_external_path_present)
				Log("External DLSS5 runtime detected; direct feature-18 path disabled to avoid a double neural pass.");
		}

		if (m_external_path_present)
			return;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
		if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || !backbuffer)
			return;

		D3D11_TEXTURE2D_DESC desc{};
		backbuffer->GetDesc(&desc);

		if (!EnsureInitialized(device, context, swap_chain, desc.Width, desc.Height, desc.Format))
			return;

		if (!EvaluateFrame(backbuffer.Get()))
			m_failed = true;
	}

	void DirectRenderer::DestroyNGX()
	{
		if (m_ngx_feature)
		{
			if (g_shim_release && g_nr_release)
				g_shim_release(reinterpret_cast<void*>(g_nr_release), m_ngx_feature);
			m_ngx_feature = nullptr;
		}

		if (g_shutdown)
			g_shutdown();

		m_ngx_params = nullptr;

		if (m_caller_shim)
		{
			FreeLibrary(m_caller_shim);
			m_caller_shim = nullptr;
		}

		if (m_ngx_nr)
		{
			FreeLibrary(m_ngx_nr);
			m_ngx_nr = nullptr;
		}

		if (m_ngx_core)
		{
			FreeLibrary(m_ngx_core);
			m_ngx_core = nullptr;
		}

		g_init_project_id = nullptr;
		g_allocate_parameters = nullptr;
		g_shutdown = nullptr;
		g_nr_init = nullptr;
		g_nr_create = nullptr;
		g_nr_evaluate = nullptr;
		g_nr_release = nullptr;
		g_shim_init = nullptr;
		g_shim_create = nullptr;
		g_shim_evaluate = nullptr;
		g_shim_release = nullptr;
	}

	void DirectRenderer::Shutdown()
	{
		DestroySizeDependentResources();
		DestroyNGX();

		m_d12_shared_fence.Reset();
		m_d11_fence.Reset();
		m_d11_context4.Reset();
		m_d11_device5.Reset();

		m_d12_fence.Reset();
		m_d12_list.Reset();
		m_d12_allocator.Reset();
		m_d12_queue.Reset();
		m_d12_device.Reset();

		m_host_device = nullptr;
		m_host_context = nullptr;
		m_swap_chain = nullptr;

		m_initialized = false;
		m_failed = false;
		m_logged_failure = false;
		m_external_path_checked = false;
		m_external_path_present = false;
		m_frame_index = 0;
		m_d12_fence_value = 0;
		m_interop_fence_value = 0;
	}
}

#endif
