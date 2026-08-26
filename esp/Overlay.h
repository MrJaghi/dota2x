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

class Overlay;

class Overlay
{
public:
	HWND overlayWnd = nullptr;
	HWND gameWnd = nullptr;
	int width = 0, height = 0;
	// game client-area origin in SCREEN coordinates (for SendInput math)
	int clientOriginX = 0, clientOriginY = 0;
	bool focused = false;

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	IDXGISwapChain* swapChain = nullptr;
	ID3D11RenderTargetView* renderTargetView = nullptr;

	~Overlay() { Shutdown(); }

	bool Create(const wchar_t* gameWindowTitle)
	{
		Shutdown(); // release any stale state from a previous run.

		gameWnd = FindWindowW(nullptr, gameWindowTitle);
		if (!gameWnd)
			return false;

		RECT rect;
		GetClientRect(gameWnd, &rect);
		POINT topLeft{ rect.left, rect.top };
		ClientToScreen(gameWnd, &topLeft);
		clientOriginX = topLeft.x;
		clientOriginY = topLeft.y;
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;

		const wchar_t* kClassName = L"DragonBurnOverlayClass";
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = kClassName;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		// RegisterClassEx fails with ERROR_CLASS_ALREADY_EXISTS on the
		// second Create() (after a Dota restart); that's fine -- we reuse
		// the existing class.
		RegisterClassExW(&wc);

		// NOTE: WS_EX_TRANSPARENT stays on FOREVER now. The old build used
		// to strip it while the menu was open, which turned this fullscreen
		// topmost window into a wall that ate every mouse event on the
		// screen ("my mouse doesn't even move"). Menu input is handled by
		// InputRouter's low-level hook instead -- the overlay itself never
		// takes input of any kind.
		overlayWnd = CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kClassName, L"DragonBurn ESP", WS_POPUP,
			topLeft.x, topLeft.y, width, height,
			nullptr, nullptr, wc.hInstance, this);

		if (!overlayWnd)
			return false;

		// Stash 'this' in GWLP_USERDATA so WndProc can reach us for WM_ACTIVATE.
		SetWindowLongPtrW(overlayWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		SetLayeredWindowAttributes(overlayWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

		if (!CreateDeviceD3D())
			return false;

		ShowWindow(overlayWnd, SW_SHOW);
		UpdateWindow(overlayWnd);

		m_visible = true;
		m_moved = true;
		m_sizeChanged = false;
		m_lastTopmost = GetTickCount64();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.IniFilename = nullptr; // no imgui.ini churn; the menu saves its own pos
		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(overlayWnd);
		ImGui_ImplDX11_Init(device, context);

		return true;
	}

	// Keep the overlay glued to the game window. This used to call
	// SetWindowPos + ShowWindow + SetWindowLongPtr EVERY frame, which is a
	// heavyweight Win32 round-trip and contributed to the menu-time lag; now
	// it only touches the window when something actually changed.
	void SyncWithGameWindow()
	{
		if (!IsWindow(gameWnd)) { focused = false; return; }

		// Treat our own overlay as "game focused" too -- WS_EX_NOACTIVATE
		// should keep the game in front, but if the overlay does receive
		// activation we still want the ESP + menu to keep running.
		HWND fg = GetForegroundWindow();
		focused = (fg == gameWnd) || (fg == overlayWnd);

		if (!focused) {
			if (m_visible) {
				ShowWindow(overlayWnd, SW_HIDE);
				m_visible = false;
			}
		} else if (!m_visible) {
			ShowWindow(overlayWnd, SW_SHOWNOACTIVATE);
			m_visible = true;
			m_moved = true; // force a SetWindowPos next block
		}

		RECT rect;
		GetClientRect(gameWnd, &rect);
		POINT topLeft{ rect.left, rect.top };
		ClientToScreen(gameWnd, &topLeft);
		int newWidth = rect.right - rect.left;
		int newHeight = rect.bottom - rect.top;

		if (newWidth != width || newHeight != height) {
			width = newWidth;
			height = newHeight;
			m_moved = true;
			m_sizeChanged = true;
		}
		if (topLeft.x != clientOriginX || topLeft.y != clientOriginY) {
			clientOriginX = topLeft.x;
			clientOriginY = topLeft.y;
			m_moved = true;
		}

		// Periodically re-assert topmost (other always-on-top apps can
		// steal the Z slot) -- but at 0.5 Hz, not 60 Hz.
		ULONGLONG now = GetTickCount64();
		bool reTopmost = (now - m_lastTopmost) > 500;
		if ((m_moved || reTopmost) && focused) {
			SetWindowPos(overlayWnd, HWND_TOPMOST,
				clientOriginX, clientOriginY, width, height,
				SWP_NOACTIVATE | SWP_SHOWWINDOW);
			m_moved = false;
			m_lastTopmost = now;
			if (m_sizeChanged) {
				ResizeSwapChain();
				m_sizeChanged = false;
			}
		}
	}

	bool IsGameFocused() const { return focused; }

	// Pump the Windows message queue -- must be called each frame before
	// input/render. Low-level hook callbacks (InputRouter) are also delivered
	// while this thread pumps messages.
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
		if (!context || !swapChain || !renderTargetView) return;
		ImGui::Render();
		const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->OMSetRenderTargets(1, &renderTargetView, nullptr);
		context->ClearRenderTargetView(renderTargetView, clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		swapChain->Present(1, 0);
	}

	void Shutdown()
	{
		if (overlayWnd && IsWindow(overlayWnd)) {
			// If ImGui/DX11 are initialized, give them a chance to detach
			// BEFORE we destroy the window so no callbacks fire into a
			// destroyed HWND.
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			CleanupDeviceD3D();

			DestroyWindow(overlayWnd);
			overlayWnd = nullptr;
		} else {
			CleanupDeviceD3D();
		}
		gameWnd = nullptr;
		width = height = 0;
		clientOriginX = clientOriginY = 0;
		focused = false;
		m_visible = false;
	}

private:
	bool m_visible = false;
	bool m_moved = true;
	bool m_sizeChanged = false;
	ULONGLONG m_lastTopmost = 0;

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// Refuse activation on mouse clicks so Dota 2 keeps keyboard focus.
		// (The overlay is permanently click-through anyway; this is a belt
		// and braces no-activate guard.)
		switch (msg)
		{
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_ACTIVATE:
			if (LOWORD(wParam) != WA_INACTIVE) {
				if (Overlay* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA))) {
					if (IsWindow(self->gameWnd))
						SetForegroundWindow(self->gameWnd);
				}
				return 0;
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}

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
		if (!swapChain || width <= 0 || height <= 0) return;
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
