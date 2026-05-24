#include <windows.h>
#include <iostream>
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <vector>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "minhook/include/MinHook.h"

#pragma comment(lib, "d3d11.lib")

#define M_PI 3.14159265358979323846f

// --- МАТЕМАТИЧЕСКИЕ СТРУКТУРЫ ---
struct Vector3 {
    float x, y, z;

    float Distance(const Vector3& v) const {
        return std::sqrt((x - v.x) * (x - v.x) + (y - v.y) * (y - v.y) + (z - v.z) * (z - v.z));
    }
};

struct Vector2 {
    float x, y;
};

// Твоя оригинальная структура контроллера из игры
struct AimController {
    unsigned char padding[0x14];
    Vector3 casx; // Позиция камеры / локального игрока
    Vector2 casy; // Текущие углы обзора (Pitch, Yaw)
};

// Элемент списка врагов для Сайлента
struct Target_t {
    Vector3 pos;
    float screen_dist;
};

// --- НАСТРОЙКИ МЕНЮ ---
namespace Settings {
    bool b_MenuOpen = true;
    bool b_SilentAim = false;
    bool b_AutoWall = false;
    bool b_RapidFire = false;
    float f_SilentFov = 60.0f;
    bool b_EspBoxes = false;
    bool b_EspSkeletons = false;
    bool b_EspChams = false;
    bool b_NoRecoil = false;
    bool b_NoSpread = false;
    bool b_BunnyHop = false;
}

// --- ОРИГИНАЛЬНЫЕ ФУНКЦИИ (ПОД МИНХУК) ---
typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present_t oPresent = nullptr;

typedef void(*AimController_Update_t)(AimController* instance);
AimController_Update_t oAimController_Update = nullptr;

typedef bool(*Raycaster_qmk_t)(Vector3 a, Vector3 b, float c, void* d);
Raycaster_qmk_t oRaycaster_qmk = nullptr;

// Хук на рендер моделей (Для Chams)
typedef void(__stdcall* DrawIndexed_t)(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
DrawIndexed_t oDrawIndexed = nullptr;

// Хук на выстрел оружия (Для Rapid Fire)
typedef void(*FireWeapon_t)(void* weapon_instance);
FireWeapon_t oFireWeapon = nullptr;

// D3D11 Переменные
ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pContext = nullptr;
ID3D11RenderTargetView* mainRenderTargetView = nullptr;
HWND window = nullptr;
WNDPROC oWndProc = nullptr;

// Переменные для логики Сайлента
std::vector<Target_t> valid_targets;

// Функция просчета углов до врага (Aim Angles)
Vector2 CalcAngle(Vector3 src, Vector3 dst) {
    Vector2 angles;
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = std::sqrt(delta.x * delta.x + delta.z * delta.z);

    angles.x = std::atan2(-delta.y, hyp) * (180.0f / M_PI); // Наклон (Pitch)
    angles.y = std::atan2(delta.x, delta.z) * (180.0f / M_PI); // Поворот (Yaw)
    return angles;
}

// Простой WorldToScreen без матриц (для понимания дистанции до центра FOV)
bool SimpleWorldToScreen(Vector3 world_pos, Vector2& screen_pos) {
    // В реальном чите тут используется перемножение матриц (ViewMatrix)
    // Для работы FOV круга симулируем попадание в экран
    ImVec2 size = ImGui::GetIO().DisplaySize;
    screen_pos.x = size.x / 2.0f;
    screen_pos.y = size.y / 2.0f;
    return true;
}

// --- ХУКИ ---

// 1. ХУК НА ОБНОВЛЕНИЕ АИМА (pSilent, No Recoil, No Spread)
void hkAimController_Update(AimController* instance) {
    if (instance) {
        // [No Recoil & No Spread] жестко гасим оси смещения прицела и разброса
        if (Settings::b_NoRecoil || Settings::b_NoSpread) {
            instance->casy.x = 0.0f;
            instance->casy.y = 0.0f;
        }

        // [pSilent Aim]
        if (Settings::b_SilentAim && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            Target_t best_target = { {0.f, 0.f, 0.f}, FLT_MAX };
            ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f);

            // Собираем цели, попавшие в FOV
            for (const auto& target : valid_targets) {
                Vector2 screen_pos;
                if (SimpleWorldToScreen(target.pos, screen_pos)) {
                    float dist = std::sqrt(std::pow(screen_pos.x - center.x, 2) + std::pow(screen_pos.y - center.y, 2));
                    if (dist < Settings::f_SilentFov && dist < best_target.screen_dist) {
                        best_target = target;
                        best_target.screen_dist = dist;
                    }
                }
            }

            // Если цель есть — делаем сайлент-мув
            if (best_target.screen_dist != FLT_MAX) {
                Vector2 silent_angles = CalcAngle(instance->casx, best_target.pos);
                
                // Беккапим углы нашей камеры, чтобы экран не дергался
                Vector2 backup_angles = instance->casy;

                // Записываем углы на врага прямо перед выстрелом игры
                instance->casy = silent_angles;

                // Вызываем оригинал, пуля летит во врага
                oAimController_Update(instance);

                // Возвращаем углы назад, визуально прицел на месте!
                instance->casy = backup_angles;
                return;
            }
        }
    }
    return oAimController_Update(instance);
}

// 2. ХУК НА АВТОВОЛЛ
bool hkRaycaster_qmk(Vector3 a, Vector3 b, float c, void* d) {
    if (Settings::b_AutoWall) {
        return true; 
    }
    return oRaycaster_qmk(a, b, c, d);
}

// 3. ХУК НА RAPID FIRE (Бешеная скорострельность)
void hkFireWeapon(void* weapon_instance) {
    oFireWeapon(weapon_instance);
    
    if (Settings::b_RapidFire) {
        // Если зажат ЛКМ, вызываем выстрел повторно без ожидания кулдауна игры
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            oFireWeapon(weapon_instance);
            oFireWeapon(weapon_instance);
        }
    }
}

// 4. ХУК НА CHAMS (D3D11 Заливка текстур)
void __stdcall hkDrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    if (Settings::b_EspChams) {
        ID3D11Buffer* pBuffer = nullptr;
        UINT stride = 0, offset = 0;
        pContext->IAGetVertexBuffers(0, 1, &pBuffer, &stride, &offset);

        if (pBuffer) {
            D3D11_BUFFER_DESC desc;
            pBuffer->GetDesc(&desc);
            
            // Фильтруем индексные буферы игроков (32/24 — стандарт для Unity-моделей рук/тел)
            if (stride == 32 || stride == 24) { 
                // Отключаем проверку глубины (Z-Buffer), чтобы чамсы были видны сквозь стены
                pContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
                
                oDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
                
                pBuffer->Release();
                return;
            }
            pBuffer->Release();
        }
    }
    return oDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
}

// --- ИНПУТ ХЕНДЛЕР ---
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (Settings::b_MenuOpen && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

// --- DIRECTX 11 PRESENT (МЕНЮ + КРУГ FOV) ---
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

            // Цепляем Хук на Chams (DrawIndexed) через VTable контекста
            void** contextVTable = *(void***)pContext;
            MH_CreateHook(contextVTable[12], &hkDrawIndexed, reinterpret_cast<LPVOID*>(&oDrawIndexed));
            MH_EnableHook(contextVTable[12]);
        }
        else return oPresent(pSwapChain, SyncInterval, Flags);
    }

    if (GetAsyncKeyState(VK_INSERT) & 1) {
        Settings::b_MenuOpen = !Settings::b_MenuOpen;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // [Отрисовка FOV кружочка для pSilent]
    if (Settings::b_SilentAim) {
        ImGui::GetForegroundDrawList()->AddCircle(
            ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f),
            Settings::f_SilentFov,
            IM_COL32(255, 0, 0, 255), // Красный цвет круга
            100,                      // Количество граней круга
            1.5f                      // Толщина линии
        );
    }

    // Отрисовка GUI
    if (Settings::b_MenuOpen) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("AI_Kolbasa v2", &Settings::b_MenuOpen, ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Rage")) {
                ImGui::Checkbox("pSilent Aim", &Settings::b_SilentAim);
                ImGui::SliderFloat("Silent FOV", &Settings::f_SilentFov, 10.0f, 400.0f, "%.1f px");
                ImGui::Checkbox("Auto-Wall", &Settings::b_AutoWall);
                ImGui::Checkbox("Rapid Fire", &Settings::b_RapidFire);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visuals")) {
                ImGui::Checkbox("2D / 3D Boxes", &Settings::b_EspBoxes);
                ImGui::Checkbox("Skeleton ESP", &Settings::b_EspSkeletons);
                ImGui::Checkbox("Chams (Wallhack)", &Settings::b_EspChams);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Misc")) {
                ImGui::Checkbox("No Recoil", &Settings::b_NoRecoil);
                ImGui::Checkbox("No Spread (Анти-разброс)", &Settings::b_NoSpread);
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

// --- ИНИЦИАЛИЗАЦИЯ И ХУК ТРЕЙД ---
DWORD WINAPI CheatThread(LPVOID lpReserved) {
    uintptr_t gameAssembly = 0;
    while (!gameAssembly) {
        gameAssembly = (uintptr_t)GetModuleHandleA("GameAssembly.dll");
        Sleep(100);
    }

    if (MH_Initialize() != MH_OK) {
        return FALSE;
    }

    // Хук Автоволла
    uintptr_t raycasterTarget = gameAssembly + 0x8470C0; 
    MH_CreateHook((LPVOID)raycasterTarget, &hkRaycaster_qmk, reinterpret_cast<LPVOID*>(&oRaycaster_qmk));
    MH_EnableHook((LPVOID)raycasterTarget);

    // Хук Аима (Сайлент, НоуРекоил, НоуСпред)
    uintptr_t aimControllerUpdateTarget = gameAssembly + 0x85A120; 
    MH_CreateHook((LPVOID)aimControllerUpdateTarget, &hkAimController_Update, reinterpret_cast<LPVOID*>(&oAimController_Update));
    MH_EnableHook((LPVOID)aimControllerUpdateTarget);

    // Хук на стрельбу (Для Rapid Fire)
    uintptr_t fireWeaponTarget = gameAssembly + 0x95B210; // Адрес метода выстрела из GameAssembly.dll
    MH_CreateHook((LPVOID)fireWeaponTarget, &hkFireWeapon, reinterpret_cast<LPVOID*>(&oFireWeapon));
    MH_EnableHook((LPVOID)fireWeaponTarget);

    // Логика создания фейк-окна для перехвата DX11 Present
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
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &scDesc, &fakeSwapChain, &fakeDevice, nullptr, &fakeContext
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
