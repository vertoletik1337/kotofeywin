#include <windows.h>
#include <d3d11.h>
#include <vector>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "minhook/include/MinHook.h"

// Линковка необходимых библиотек для DirectX и графики
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace Offsets { 
    uintptr_t Base = 0; 
    uintptr_t PlayerMgr_Inst = 0x521E10; 
    uintptr_t Trans_GetPos = 0x5A1B20; 
    uintptr_t EntityList_Off = 0x20; 
    uintptr_t LocalPlayer_Off = 0x10; 
    uintptr_t WeaponCtrl_Off = 0x30; 
    uintptr_t FireCooldown_Off = 0x48; 
}

struct { bool Menu = 1; bool ESP = 1; bool Rapid = 1; } Config;
uintptr_t debugLocalPlayer = 0;
int playersFound = 0;

template <typename T> T SafeRead(uintptr_t addr) { 
    if (addr < 0x10000 || addr > 0x7FFFFFFEFFFF) return (T)0; 
    __try { return *(T*)addr; } 
    __except (EXCEPTION_EXECUTE_HANDLER) { return (T)0; } 
}

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT); 
Present_t oPresent; ID3D11Device* pDev; ID3D11DeviceContext* pCtx; HWND wnd; WNDPROC oWnd;
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT __stdcall WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { 
    if (Config.Menu && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1; 
    return CallWindowProc(oWnd, h, m, w, l); 
}

void DoFeatures() {
    uintptr_t Base = Offsets::Base;
    if (!Base) return;
    auto GetInst = (uintptr_t(*)())(Base + Offsets::PlayerMgr_Inst);
    uintptr_t mgr = GetInst();
    if (!mgr) { debugLocalPlayer = 0; playersFound = 0; return; }
    debugLocalPlayer = SafeRead<uintptr_t>(mgr + Offsets::LocalPlayer_Off);
    if (debugLocalPlayer && Config.Rapid) {
        uintptr_t gunCtrl = SafeRead<uintptr_t>(debugLocalPlayer + Offsets::WeaponCtrl_Off);
        if (gunCtrl) *(float*)(gunCtrl + Offsets::FireCooldown_Off) = 0.0f;
    }
    uintptr_t listPtr = SafeRead<uintptr_t>(mgr + Offsets::EntityList_Off);
    int count = SafeRead<int>(mgr + Offsets::EntityList_Off + 0x8);
    playersFound = (count > 0 && count < 128) ? count : 0;
    for (int i = 0; i < playersFound; i++) {
        uintptr_t player = SafeRead<uintptr_t>(listPtr + (i * 0x8));
        if (!player || player == debugLocalPlayer) continue;
        uintptr_t transform = SafeRead<uintptr_t>(player + 0x98);
        if (!transform) continue;
        float pos[3]; 
        ((void(__stdcall*)(uintptr_t, float*))(Base + Offsets::Trans_GetPos))(transform, pos);
        if (Config.ESP) ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(pos[0], pos[1]), 5.0f, IM_COL32(255, 0, 0, 255));
    }
}

HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!pDev) { 
        sc->GetDevice(__uuidof(ID3D11Device), (void**)&pDev); 
        pDev->GetImmediateContext(&pCtx); 
        DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd); 
        wnd = sd.OutputWindow; 
        ImGui::CreateContext(); 
        ImGui_ImplWin32_Init(wnd); 
        ImGui_ImplDX11_Init(pDev, pCtx); 
        oWnd = (WNDPROC)SetWindowLongPtr(wnd, GWLP_WNDPROC, (LONG_PTR)WndProc); 
    }
    if (GetAsyncKeyState(VK_INSERT) & 1) Config.Menu = !Config.Menu;
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(10, 10), IM_COL32(0, 255, 255, 255), "kotofey.win | Internal Engine");
    if (Config.Menu) { 
        ImGui::Begin("kotofey.win - Debug Panel", &Config.Menu); 
        ImGui::Text("LocalPlayer Addr: 0x%p", (void*)debugLocalPlayer);
        ImGui::Text("Players Found: %d", playersFound);
        ImGui::Checkbox("ESP (Visual)", &Config.ESP); 
        ImGui::Checkbox("RapidFire (Memory)", &Config.Rapid); 
        ImGui::End(); 
    }
    DoFeatures();
    ImGui::Render(); pCtx->OMSetRenderTargets(1, nullptr, nullptr); 
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(sc, sync, flags);
}

DWORD WINAPI Main(LPVOID p) {
    while (!(Offsets::Base = (uintptr_t)GetModuleHandleA("GameAssembly.dll"))) Sleep(100);
    if (MH_Initialize() != MH_OK) return 0;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetConsoleWindow();
    sd.SampleDesc.Count = 1;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Windowed = TRUE;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx))) {
        void** vTable = *(void***)sc;
        if (MH_CreateHook(vTable[8], &hkPresent, (void**)&oPresent) == MH_OK) MH_EnableHook(vTable[8]);
        sc->Release(); dev->Release(); ctx->Release();
    }
    return 0;
}

BOOL WINAPI DllMain(HMODULE m, DWORD r, LPVOID p) { if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, Main, m, 0, 0); return 1; }
