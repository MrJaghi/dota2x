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
		// second Create() (after a Dota restart); that's fine — we reuse
		// the existing class.
		RegisterClassExW(&wc);

		overlayWnd = CreateWindowExW(
			// WS_EX_NOACTIVATE is critical: it keeps the overlay from stealing
			// keyboard focus when the user clicks on ImGui menu items, so
			// Dota 2 stays the foreground window and our "is game focused"
			// heuristic doesn't incorrectly hide the overlay mid-click.
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

		// Hide the overlay entirely when Dota 2 is NOT the foreground window
		// (Alt+Tabbed out, browser focused, etc.). We still keep the frame loop
		// alive so we instantly reappear on switch-back.
		HWND fg = GetForegroundWindow();
		// Treat our own overlay as "game focused" too -- when the user clicks
		// on ImGui menu items the overlay may briefly own the foreground;
		// the game is still the target and we must not hide/close the menu.
		bool gameFocused = (fg == gameWnd) || (fg == overlayWnd);

		// The menu forces the overlay clickable; if the game isn't focused, also
		// force-transparent so the overlay can never steal clicks on other apps.
		if (gameFocused) {
			if (IsWindowVisible(overlayWnd) == FALSE)
				ShowWindow(overlayWnd, SW_SHOWNOACTIVATE);
			// Only enforce clickable state when focused (no clicks stolen outside)
			if (clickable) SetLayeredClickable(true);
			else           SetLayeredClickable(false);
		} else {
			// Game is in background: hide overlay completely so it doesn't sit on
			// top of Chrome/Desktop/Discord, and force it non-clickable just in case.
			ShowWindow(overlayWnd, SW_HIDE);
		}

		RECT rect;
		GetClientRect(gameWnd, &rect);
		POINT topLeft{ rect.left, rect.top };
		ClientToScreen(gameWnd, &topLeft);
		int newWidth = rect.right - rect.left;
		int newHeight = rect.bottom - rect.top;

		SetWindowPos(overlayWnd, HWND_TOPMOST, topLeft.x, topLeft.y, newWidth, newHeight,
			SWP_NOACTIVATE | (gameFocused ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

		if (newWidth != width || newHeight != height)
		{
			width = newWidth;
			height = newHeight;
			ResizeSwapChain();
		}
	}

	// Returns true while the overlay is visible (game focused).
	// We treat both the game window AND our own overlay window as "focused"
	// because WS_EX_NOACTIVATE should keep the game in front, but if the
	// overlay does receive activation (e.g. the user alt-tabs to it during
	// menu use), we still want the ESP + menu to keep running.
	bool IsGameFocused() const
	{
		if (!IsWindow(gameWnd)) return false;
		HWND fg = GetForegroundWindow();
		return (fg == gameWnd) || (fg == overlayWnd);
	}

	void SetClickable(bool value)
	{
		if (clickable == value) return;
		clickable = value;
		SetLayeredClickable(value);
	}

	// Pump the Windows message queue -- must be called each frame before input/render.
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
		clickable = false;
	}

private:
	void SetLayeredClickable(bool value)
	{
		if (!overlayWnd) return;
		LONG_PTR exStyle = GetWindowLongPtrW(overlayWnd, GWL_EXSTYLE);
		if (value)
			exStyle &= ~WS_EX_TRANSPARENT;
		else
			exStyle |= WS_EX_TRANSPARENT;
		SetWindowLongPtrW(overlayWnd, GWL_EXSTYLE, exStyle);
	}

private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// Refuse activation on mouse clicks so Dota 2 keeps keyboard focus
		// while the user interacts with ImGui.  Without this, clicking on
		// the menu makes the overlay the foreground window and our "is game
		// focused" check (plus Dota's input) would misbehave.
		switch (msg)
		{
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_ACTIVATE:
			if (LOWORD(wParam) != WA_INACTIVE) {
				// If we somehow did get activated (e.g. via Alt), immediately
				// yield focus back to the game window.
				if (Overlay* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA))) {
					if (IsWindow(self->gameWnd))
						SetForegroundWindow(self->gameWnd);
				}
				return 0;
			}
			break;
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
