#include <windows.h>
#include <iostream>
#include <d3d11.h>
#include <dxgi.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "minhook/include/MinHook.h"

#pragma comment(lib, "d3d11.lib")

struct Vector3 {
    float x, y, z;
};

struct Vector2 {
    float x, y;
};

struct AimController {
    unsigned char padding[0x14];
    Vector3 casx;
    Vector2 casy;
};

namespace Settings {
    bool b_MenuOpen = true;
    bool b_SilentAim = false;
    bool b_AutoWall = false;
    bool b_RapidFire = false;
    float f_SilentFov = 30.0f;
    bool b_EspBoxes = false;
    bool b_EspSkeletons = false;
    bool b_EspChams = false;
    bool b_NoRecoil = false;
    bool b_NoSpread = false;
    bool b_BunnyHop = false;
}

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present_t oPresent = nullptr;

typedef void(*AimController_Update_t)(AimController* instance);
AimController_Update_t oAimController_Update = nullptr;

typedef bool(*Raycaster_qmk_t)(Vector3 a, Vector3 b, float c, void* d);
Raycaster_qmk_t oRaycaster_qmk = nullptr;

ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pContext = nullptr;
ID3D11RenderTargetView* mainRenderTargetView = nullptr;
HWND window = nullptr;
WNDPROC oWndProc = nullptr;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (Settings::b_MenuOpen && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void hkAimController_Update(AimController* instance) {
    if (instance) {
        if (Settings::b_NoRecoil || Settings::b_NoSpread) {
            instance->casy.x = 0.0f;
            instance->casy.y = 0.0f;
        }
        if (Settings::b_SilentAim) {
            // Logic
        }
    }
    return oAimController_Update(instance);
}

bool hkRaycaster_qmk(Vector3 a, Vector3 b, float c, void* d) {
    if (Settings::b_AutoWall) {
        return true; 
    }
    return oRaycaster_qmk(a, b, c, d);
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!pDevice) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            window = sd.OutputWindow;
            ID3D11Texture2D* pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
            pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
            pBackBuffer->Release();
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            
            ImGui::StyleColorsDark();
            
            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX11_Init(pDevice, pContext);
        }
        else return oPresent(pSwapChain, SyncInterval, Flags);
    }

    if (GetAsyncKeyState(VK_INSERT) & 1) {
        Settings::b_MenuOpen = !Settings::b_MenuOpen;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (Settings::b_SilentAim) {
        ImGui::GetForegroundDrawList()->AddCircle(
            ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f),
            Settings::f_SilentFov,
            IM_COL32(255, 0, 0, 255),
            100,
            1.5f
        );
    }

    if (Settings::b_MenuOpen) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("AI_Kolbasa", &Settings::b_MenuOpen, ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Rage")) {
                ImGui::Checkbox("Silent Aim", &Settings::b_SilentAim);
                ImGui::SliderFloat("Silent FOV", &Settings::f_SilentFov, 10.0f, 300.0f, "%.1f px");
                ImGui::Checkbox("Auto-Wall", &Settings::b_AutoWall);
                ImGui::Checkbox("Rapid Fire", &Settings::b_RapidFire);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visuals")) {
                ImGui::Checkbox("2D / 3D Boxes", &Settings::b_EspBoxes);
                ImGui::Checkbox("Skeleton ESP", &Settings::b_EspSkeletons);
                ImGui::Checkbox("Chams", &Settings::b_EspChams);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Misc")) {
                ImGui::Checkbox("No Recoil", &Settings::b_NoRecoil);
                ImGui::Checkbox("No Spread", &Settings::b_NoSpread);
                ImGui::Checkbox("BunnyHop", &Settings::b_BunnyHop);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    ImGui::Render();
    pContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    
    return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI CheatThread(LPVOID lpReserved) {
    uintptr_t gameAssembly = 0;
    while (!gameAssembly) {
        gameAssembly = (uintptr_t)GetModuleHandleA("GameAssembly.dll");
        Sleep(100);
    }

    if (MH_Initialize() != MH_OK) {
        return FALSE;
    }

    uintptr_t raycasterTarget = gameAssembly + 0x8470C0; 
    MH_CreateHook((LPVOID)raycasterTarget, &hkRaycaster_qmk, reinterpret_cast<LPVOID*>(&oRaycaster_qmk));
    MH_EnableHook((LPVOID)raycasterTarget);

    uintptr_t aimControllerUpdateTarget = gameAssembly + 0x85A120; 
    MH_CreateHook((LPVOID)aimControllerUpdateTarget, &hkAimController_Update, reinterpret_cast<LPVOID*>(&oAimController_Update));
    MH_EnableHook((LPVOID)aimControllerUpdateTarget);

    HWND dummyWindow = CreateWindowA("BUTTON", "DummyWindow", WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, 100, 100, nullptr, nullptr, nullptr, nullptr);
    if (!dummyWindow) {
        MH_Uninitialize();
        return FALSE;
    }

    DXGI_SWAP_CHAIN_DESC scDesc = { 0 };
    scDesc.BufferCount = 1;                                    
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;     
    scDesc.BufferDesc.Width = 1;                               
    scDesc.BufferDesc.Height = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;      
    scDesc.OutputWindow = dummyWindow;                         
    scDesc.SampleDesc.Count = 1;                               
    scDesc.Windowed = TRUE;                                    

    IDXGISwapChain* fakeSwapChain = nullptr;
    ID3D11Device* fakeDevice = nullptr;
    ID3D11DeviceContext* fakeContext = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, 
        D3D_DRIVER_TYPE_HARDWARE, 
        nullptr, 
        0, 
        &featureLevel, 
        1, 
        D3D11_SDK_VERSION, 
        &scDesc, 
        &fakeSwapChain, 
        &fakeDevice, 
        nullptr, 
        &fakeContext
    );

    if (FAILED(hr)) {
        DestroyWindow(dummyWindow);
        MH_Uninitialize();
        return FALSE;
    }

    void** swapChainVTable = *(void***)fakeSwapChain;
    void* presentAddress = swapChainVTable[8]; 

    if (MH_CreateHook(presentAddress, &hkPresent, reinterpret_cast<LPVOID*>(&oPresent)) == MH_OK) {
        MH_EnableHook(presentAddress);
    }

    fakeSwapChain->Release();
    fakeDevice->Release();
    fakeContext->Release();
    DestroyWindow(dummyWindow);

    return TRUE;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, CheatThread, hMod, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        break;
    }
    return TRUE;
}
