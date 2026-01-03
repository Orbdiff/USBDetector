#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <atomic>
#include <algorithm>
#include <wrl/client.h>

#include "USB/usbhunt.hpp"
#include "UI/_font.hh"

using Microsoft::WRL::ComPtr;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

ID3D11Device* g_pd3dDevice                     = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext       = nullptr;
IDXGISwapChain* g_pSwapChain                   = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
HWND g_hWnd                                    = nullptr;

USBDetector detector;
std::vector<USBDeviceInfo> currentDevices;
std::atomic<bool> isDetecting(false);
std::thread usbDetectionThread;

void CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> pBackBuffer;
    if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
        return;
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &g_mainRenderTargetView);
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    if (FAILED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    )))
        return false;

    CreateRenderTarget();
    return true;
}

template<typename T>
void SafeRelease(T*& ptr)
{
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

void CleanupRenderTarget()
{
    SafeRelease(g_mainRenderTargetView);
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    SafeRelease(g_pSwapChain);
    SafeRelease(g_pd3dDeviceContext);
    SafeRelease(g_pd3dDevice);
}

void UpdateUSBDevices()
{
    currentDevices = detector.GetDevices();
    isDetecting = false;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            if (FAILED(g_pSwapChain->ResizeBuffers(
                0,
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)),
                DXGI_FORMAT_UNKNOWN,
                0)))
            {
                ::PostQuitMessage(0);
            }
            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain
(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("USBDetector"), NULL };
    ::RegisterClassEx(&wc);
    g_hWnd = ::CreateWindow(wc.lpszClassName, _T("USBDetector made by Diff"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(g_hWnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(g_hWnd, SW_MAXIMIZE);
    ::UpdateWindow(g_hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImFontConfig CustomFont;
    CustomFont.FontDataOwnedByAtlas = false;
    ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)Custom, static_cast<int>(Custom_len), 17.0f, &CustomFont);
    io.FontDefault = font;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                 = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.25f, 0.28f, 0.35f, 1.00f);
    colors[ImGuiCol_Border]               = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.08f, 0.08f, 0.80f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.50f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.20f, 0.20f, 0.20f, 0.80f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.30f, 0.40f, 0.60f, 0.90f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.40f, 0.50f, 0.70f, 0.90f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.25f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.ScrollbarSize     = 16.0f;
    style.ItemSpacing       = ImVec2(10, 8);
    style.ItemInnerSpacing  = ImVec2(8, 6);
    style.CellPadding       = ImVec2(8, 6);
    style.WindowPadding     = ImVec2(16, 16);
    style.FramePadding      = ImVec2(10, 6);

    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    isDetecting = true;
    usbDetectionThread = std::thread(UpdateUSBDevices);

    static bool showLoadingAnimation = true;
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (isDetecting)
        {
            static double loadingStartTime = 0.0;
            static float fadeOutAlpha = 1.0f;
            const float minLoadingDuration = 1.0f;
            const float fadeOutSpeed = 2.0f;

            ImGuiIO& io = ImGui::GetIO();
            double t = ImGui::GetTime();
            float tf = static_cast<float>(t);

            if (loadingStartTime == 0.0)
                loadingStartTime = t;

            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 size = ImGui::GetWindowSize();
            ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f - 20.0f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            const int particleCount = 100;
            for (int i = 0; i < particleCount; ++i)
            {
                float seed = i * 13.37f;
                float x = pos.x + fmodf(seed * 37.0f + tf * 20.0f, size.x);
                float y = pos.y + fmodf(seed * 53.0f + tf * 15.0f, size.y);
                float alpha = 40.0f + 40.0f * sinf(tf + seed);
                float radius = 1.5f + 0.5f * sinf(tf + seed * 0.5f);
                draw_list->AddCircleFilled(ImVec2(x, y), radius, IM_COL32(100, 150, 200, static_cast<int>(alpha)));
            }

            const float baseRadius = 18.0f;
            float pulse = 0.9f + 0.1f * sinf(tf * 2.5f);
            float radius = baseRadius * pulse;

            ImU32 ringColors[3] = {IM_COL32(169, 169, 169, 220), IM_COL32(100, 150, 200, 200), IM_COL32(100, 150, 200, 200)};

            float ringOffsets[3] = { -4.0f, -2.5f, -1.0f };
            float ringStart[3] = { 0.0f, 1.5f, 3.0f };
            for (int i = 0; i < 3; ++i)
            {
                draw_list->PathArcTo(center, radius + ringOffsets[i], tf * 2.2f + ringStart[i], tf * 2.2f + ringStart[i] + 1.6f, 48);
                draw_list->PathStroke(ringColors[i], false, 3.5f);
            }

            const char* loadingText = "Parsing USB Devices";
            ImVec2 textSize = ImGui::CalcTextSize(loadingText);
            ImVec2 textPos = ImVec2(center.x - textSize.x * 0.5f, center.y + baseRadius + 16.0f);

            float textPulse = 0.85f + 0.15f * sinf(tf * 2.0f);
            int alpha = std::max(120, static_cast<int>(textPulse * 255.0f * fadeOutAlpha));
            draw_list->AddText(textPos, IM_COL32(255, 255, 255, alpha), loadingText);

            if (!isDetecting && (t - loadingStartTime) >= minLoadingDuration)
            {
                fadeOutAlpha -= io.DeltaTime * fadeOutSpeed;
                if (fadeOutAlpha <= 0.0f)
                {
                    fadeOutAlpha = 0.0f;
                    showLoadingAnimation = false;
                    loadingStartTime = 0.0;
                }
            }
        } else {
            if (ImGui::BeginTable("USBDevicesTable", 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Device Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
                ImGui::TableSetupColumn("Vendor Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, 1);
                ImGui::TableSetupColumn("VID/PID", ImGuiTableColumnFlags_WidthStretch, 0.0f, 2);
                ImGui::TableSetupColumn("Last Connect Time", ImGuiTableColumnFlags_WidthStretch, 0.0f, 3);
                ImGui::TableSetupColumn("Last Removal Time", ImGuiTableColumnFlags_WidthStretch, 0.0f, 4);
                ImGui::TableSetupColumn("Capabilities", ImGuiTableColumnFlags_WidthStretch, 0.0f, 5);
                ImGui::TableSetupColumn("Connected", ImGuiTableColumnFlags_WidthStretch, 0.0f, 6);
                ImGui::TableHeadersRow();

                ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
                if (specs && specs->SpecsDirty)
                {
                    specs->SpecsDirty = false;
                    int column = specs->Specs[0].ColumnUserID;
                    bool ascending = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    std::sort(currentDevices.begin(), currentDevices.end(), [&](const USBDeviceInfo& a, const USBDeviceInfo& b) {
                        switch (column)
                        {
                        case 0: return ascending ? (a.DeviceName < b.DeviceName)           : (a.DeviceName > b.DeviceName);
                        case 1: return ascending ? (a.VendorName < b.VendorName)           : (a.VendorName > b.VendorName);
                        case 2: return ascending ? (a.vidpid < b.vidpid)                   : (a.vidpid > b.vidpid);
                        case 3: return ascending ? (a.connectTime < b.connectTime)         : (a.connectTime > b.connectTime);
                        case 4: return ascending ? (a.lastRemovalTime < b.lastRemovalTime) : (a.lastRemovalTime > b.lastRemovalTime);
                        case 5: return ascending ? (a.capabilities < b.capabilities)       : (a.capabilities > b.capabilities);
                        case 6: return ascending ? (a.isConnected < b.isConnected)         : (a.isConnected > b.isConnected);
                        default: return false;
                        }
                        });
                }

                for (size_t i = 0; i < currentDevices.size(); ++i)
                {
                    const auto& dev = currentDevices[i];

                    ImGui::PushID(static_cast<int>(i));

                    ImGui::TableNextRow();

                    bool isMassStorage = dev.DeviceName.find("Mass Storage") != std::string::npos || dev.VendorName.find("Mass Storage") != std::string::npos;

                    ImGui::TableSetColumnIndex(0);
                    if (isMassStorage) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    }
                    ImGui::Text("%s", dev.DeviceName.c_str());
                    if (isMassStorage) {
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (isMassStorage) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    }
                    ImGui::Text("%s", dev.VendorName.c_str());
                    if (isMassStorage) {
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(2);
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.7f, 1.0f));
                        ImGui::Text("%s", dev.vidpid.c_str());
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(3);
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                        ImGui::Text("%s", dev.connectTime.c_str());
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(4);
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                        ImGui::Text("%s", dev.lastRemovalTime.c_str());
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(5); ImGui::Text("%s", dev.capabilities.c_str());

                    ImGui::TableSetColumnIndex(6);
                    {
                        if (dev.isConnected) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                        }
                        else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                        }
                        ImGui::Text("%s", dev.isConnected ? "Yes" : "No");
                        ImGui::PopStyleColor();
                    }

                    //POPUP MENU
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                    ImGui::InvisibleButton("##row", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight()));
                    ImGui::PopStyleVar();

                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        ImGui::OpenPopup("popup_table_path");
                    }

                    if (ImGui::BeginPopup("popup_table_path"))
                    {
                        if (ImGui::Selectable("Search USB"))
                        {
                            std::string searchQuery = "https://www.google.com/search?q=" + dev.DeviceName + "+" + dev.VendorName;
                            ShellExecuteW(NULL, L"open", std::wstring(searchQuery.begin(), searchQuery.end()).c_str(), NULL, NULL, SW_SHOWNORMAL);
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        ImGui::End();
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    if (usbDetectionThread.joinable()) usbDetectionThread.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(g_hWnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}