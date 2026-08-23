#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

inline Present_t oPresent = nullptr;
inline ResizeBuffers_t oResizeBuffers = nullptr;

void OnPresent(IDXGISwapChain* swapChain);
void OnResizeBuffers();

inline HRESULT __stdcall hkPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
	OnPresent(swapChain);
	return oPresent(swapChain, syncInterval, flags);
}

inline HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* swapChain, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{
	OnResizeBuffers();
	return oResizeBuffers(swapChain, count, w, h, fmt, flags);
}

inline void PatchVTableEntry(void** vtable, int index, void* hook, void** original)
{
	DWORD oldProtect;
	VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
	*original = vtable[index];
	vtable[index] = hook;
	VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

inline bool InstallPresentHook()
{
	WNDCLASSEXA wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "AzhengDummyWndClass";
	RegisterClassExA(&wc);

	HWND dummyWnd = CreateWindowExA(0, wc.lpszClassName, "dummy", WS_OVERLAPPEDWINDOW,
		0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = 100;
	sd.BufferDesc.Height = 100;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = dummyWnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	IDXGISwapChain* swapChain = nullptr;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		levels, 1, D3D11_SDK_VERSION, &sd, &swapChain, &device, &featureLevel, &context);

	if (FAILED(hr))
	{
		DestroyWindow(dummyWnd);
		UnregisterClassA(wc.lpszClassName, wc.hInstance);
		return false;
	}

	void** vtable = *reinterpret_cast<void***>(swapChain);

	PatchVTableEntry(vtable, 8, (void*)hkPresent, (void**)&oPresent);
	PatchVTableEntry(vtable, 13, (void*)hkResizeBuffers, (void**)&oResizeBuffers);

	swapChain->Release();
	context->Release();
	device->Release();
	DestroyWindow(dummyWnd);
	UnregisterClassA(wc.lpszClassName, wc.hInstance);
	return true;
}
