#pragma once
#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#pragma comment(lib, "d3d11.lib")

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class Overlay
{
public:
	HWND overlayWnd = nullptr;
	HWND gameWnd = nullptr;
	int width = 0, height = 0;
	bool clickable = false;

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	IDXGISwapChain* swapChain = nullptr;
	ID3D11RenderTargetView* renderTargetView = nullptr;

	bool Create(const wchar_t* gameWindowTitle)
	{
		gameWnd = FindWindowW(nullptr, gameWindowTitle);
		if (!gameWnd)
			return false;

		RECT rect;
		GetClientRect(gameWnd, &rect);
		POINT topLeft{ rect.left, rect.top };
		ClientToScreen(gameWnd, &topLeft);
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;

		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"AzhengOverlayClass";
		RegisterClassExW(&wc);

		overlayWnd = CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			wc.lpszClassName, L"AzhengOverlay", WS_POPUP,
			topLeft.x, topLeft.y, width, height,
			nullptr, nullptr, wc.hInstance, this);

		if (!overlayWnd)
			return false;

		SetLayeredWindowAttributes(overlayWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

		if (!CreateDeviceD3D())
			return false;

		ShowWindow(overlayWnd, SW_SHOW);
		UpdateWindow(overlayWnd);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(overlayWnd);
		ImGui_ImplDX11_Init(device, context);

		return true;
	}

	void SyncWithGameWindow()
	{
		if (!IsWindow(gameWnd)) return;

		RECT rect;
		GetClientRect(gameWnd, &rect);
		POINT topLeft{ rect.left, rect.top };
		ClientToScreen(gameWnd, &topLeft);
		int newWidth = rect.right - rect.left;
		int newHeight = rect.bottom - rect.top;

		SetWindowPos(overlayWnd, HWND_TOPMOST, topLeft.x, topLeft.y, newWidth, newHeight, SWP_NOACTIVATE);

		if (newWidth != width || newHeight != height)
		{
			width = newWidth;
			height = newHeight;
			ResizeSwapChain();
		}
	}

	void SetClickable(bool value)
	{
		if (clickable == value) return;
		clickable = value;

		LONG_PTR exStyle = GetWindowLongPtrW(overlayWnd, GWL_EXSTYLE);
		if (value)
			exStyle &= ~WS_EX_TRANSPARENT;
		else
			exStyle |= WS_EX_TRANSPARENT;
		SetWindowLongPtrW(overlayWnd, GWL_EXSTYLE, exStyle);
	}

	void PumpMessages()
	{
		MSG msg{};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	void BeginFrame()
	{
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void EndFrameAndPresent()
	{
		ImGui::Render();
		const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->OMSetRenderTargets(1, &renderTargetView, nullptr);
		context->ClearRenderTargetView(renderTargetView, clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		swapChain->Present(1, 0);
	}

	void Shutdown()
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		CleanupDeviceD3D();
	}

private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			return true;
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	bool CreateDeviceD3D()
	{
		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 2;
		sd.BufferDesc.Width = width;
		sd.BufferDesc.Height = height;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = overlayWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		UINT createFlags = 0;
		D3D_FEATURE_LEVEL featureLevel;
		const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
			featureLevels, 1, D3D11_SDK_VERSION, &sd,
			&swapChain, &device, &featureLevel, &context);

		if (FAILED(hr))
			return false;

		CreateRenderTarget();
		return true;
	}

	void CreateRenderTarget()
	{
		ID3D11Texture2D* backBuffer = nullptr;
		swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (backBuffer)
		{
			device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
			backBuffer->Release();
		}
	}

	void CleanupRenderTarget()
	{
		if (renderTargetView) { renderTargetView->Release(); renderTargetView = nullptr; }
	}

	void ResizeSwapChain()
	{
		if (!swapChain) return;
		CleanupRenderTarget();
		swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
		CreateRenderTarget();
	}

	void CleanupDeviceD3D()
	{
		CleanupRenderTarget();
		if (swapChain) { swapChain->Release(); swapChain = nullptr; }
		if (context) { context->Release(); context = nullptr; }
		if (device) { device->Release(); device = nullptr; }
	}
};
