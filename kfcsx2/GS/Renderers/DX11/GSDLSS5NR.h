// SPDX-License-Identifier: GPL-3.0+
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

struct NVSDK_NGX_Handle
{
	unsigned int Id;
};

struct NVSDK_NGX_Parameter
{
	virtual void Set(const char* name, unsigned long long value) = 0;
	virtual void Set(const char* name, float value) = 0;
	virtual void Set(const char* name, double value) = 0;
	virtual void Set(const char* name, unsigned int value) = 0;
	virtual void Set(const char* name, int value) = 0;
	virtual void Set(const char* name, ID3D11Resource* value) = 0;
	virtual void Set(const char* name, ID3D12Resource* value) = 0;
	virtual void Set(const char* name, void* value) = 0;

	virtual int Get(const char* name, unsigned long long* value) const = 0;
	virtual int Get(const char* name, float* value) const = 0;
	virtual int Get(const char* name, double* value) const = 0;
	virtual int Get(const char* name, unsigned int* value) const = 0;
	virtual int Get(const char* name, int* value) const = 0;
	virtual int Get(const char* name, ID3D11Resource** value) const = 0;
	virtual int Get(const char* name, ID3D12Resource** value) const = 0;
	virtual int Get(const char* name, void** value) const = 0;

	virtual void Reset() = 0;
};

namespace GSDLSS5NR
{
	class DirectRenderer final
	{
	public:
		DirectRenderer() = default;
		~DirectRenderer();

		DirectRenderer(const DirectRenderer&) = delete;
		DirectRenderer& operator=(const DirectRenderer&) = delete;

		void Process(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swap_chain);
		void Shutdown();

	private:
		using ComPtrD11Texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>;
		using ComPtrD11Device5 = Microsoft::WRL::ComPtr<ID3D11Device5>;
		using ComPtrD11Context4 = Microsoft::WRL::ComPtr<ID3D11DeviceContext4>;
		using ComPtrD11Fence = Microsoft::WRL::ComPtr<ID3D11Fence>;
		using ComPtrD12Device = Microsoft::WRL::ComPtr<ID3D12Device>;
		using ComPtrD12Queue = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
		using ComPtrD12Allocator = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;
		using ComPtrD12List = Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
		using ComPtrD12Fence = Microsoft::WRL::ComPtr<ID3D12Fence>;
		using ComPtrD12Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
		using ComPtrD12Heap = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;
		using ComPtrD12RootSignature = Microsoft::WRL::ComPtr<ID3D12RootSignature>;
		using ComPtrD12PipelineState = Microsoft::WRL::ComPtr<ID3D12PipelineState>;

		bool EnsureInitialized(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swap_chain,
			unsigned int width, unsigned int height, DXGI_FORMAT format);
		bool CreateD3D12Companion(ID3D11Device* device, ID3D11DeviceContext* context);
		bool CreateInteropResources(unsigned int width, unsigned int height, DXGI_FORMAT format);
		bool CreateComputePipeline();
		bool LoadNGX(unsigned int width, unsigned int height);
		bool RecreateFeature(unsigned int width, unsigned int height);
		bool EvaluateFrame(ID3D11Texture2D* backbuffer);
		bool BeginD3D12Commands();
		bool ExecuteAndWait();
		void DestroySizeDependentResources();
		void DestroyNGX();

		void Log(const char* format, ...);
		void LogFailureOnce(const char* message);

		static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
			D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

		ID3D11Device* m_host_device = nullptr;
		ID3D11DeviceContext* m_host_context = nullptr;
		IDXGISwapChain1* m_swap_chain = nullptr;

		ComPtrD11Device5 m_d11_device5;
		ComPtrD11Context4 m_d11_context4;
		ComPtrD11Fence m_d11_fence;

		ComPtrD12Device m_d12_device;
		ComPtrD12Queue m_d12_queue;
		ComPtrD12Allocator m_d12_allocator;
		ComPtrD12List m_d12_list;
		ComPtrD12Fence m_d12_fence;
		ComPtrD12Fence m_d12_shared_fence;

		ComPtrD11Texture m_d11_shared_input;
		ComPtrD11Texture m_d11_shared_output;
		ComPtrD12Resource m_d12_shared_input;
		ComPtrD12Resource m_d12_shared_output;

		ComPtrD12Resource m_nr_input;
		ComPtrD12Resource m_nr_output;
		ComPtrD12Heap m_descriptor_heap;
		ComPtrD12RootSignature m_root_signature;
		ComPtrD12PipelineState m_convert_to_nr;
		ComPtrD12PipelineState m_convert_from_nr;

		HMODULE m_ngx_core = nullptr;
		HMODULE m_ngx_nr = nullptr;
		HMODULE m_caller_shim = nullptr;

		NVSDK_NGX_Parameter* m_ngx_params = nullptr;
		NVSDK_NGX_Handle* m_ngx_feature = nullptr;

		unsigned int m_width = 0;
		unsigned int m_height = 0;
		DXGI_FORMAT m_backbuffer_format = DXGI_FORMAT_UNKNOWN;
		std::uint64_t m_frame_index = 0;
		std::uint64_t m_d12_fence_value = 0;
		std::uint64_t m_interop_fence_value = 0;

		bool m_initialized = false;
		bool m_failed = false;
		bool m_logged_failure = false;
		bool m_external_path_checked = false;
		bool m_external_path_present = false;
	};
}

#endif
