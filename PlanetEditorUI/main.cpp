// World Creater — 外部控制面板 (C++ / ImGui / DX11 / 液态玻璃)
//
// 角色划分：
//   本程序 = 玩家看的 UI（列表、字段表、预览、按钮）
//   游戏内 PlanetEditor.dll = 接口层（TCP 127.0.0.1:41729），负责反射改星球、重建地形、写盘
//
// 玻璃效果移植自 E:\1SFSPackTools\ui（glass.cpp/h + DWM 亚克力）。

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "glass.h"
#include "net.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

// ---------------- D3D ----------------
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND g_hwnd = nullptr;
static GlassRenderer g_glass;

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_dev->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
static void CleanupRenderTarget() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL lvl;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &lvl, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &lvl, &g_ctx);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

// ---------------- 亚克力 ----------------
struct ACCENT_POLICY { int AccentState; int AccentFlags; unsigned int GradientColor; int AnimationId; };
struct WINDOWCOMPOSITIONATTRIBDATA { int Attrib; void* pvData; size_t cbData; };
typedef BOOL(WINAPI* PFN_SetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static bool EnableAcrylic(HWND hwnd, unsigned int tint_abgr) {
    auto fn = (PFN_SetWindowCompositionAttribute)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!fn) return false;
    ACCENT_POLICY policy{ 4 /*ACCENT_ENABLE_ACRYLICBLURBEHIND*/, 0, tint_abgr, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data{ 19 /*WCA_ACCENT_POLICY*/, &policy, sizeof(policy) };
    return fn(hwnd, &data) != FALSE;
}

// ---------------- 与游戏通信 ----------------
static BridgeClient g_bridge;
static int g_port = 41729;

struct PlanetInfo { std::string code; int radius = 0; std::string gravity; float selAnim = 0.f; };
struct FieldRow { int kind = 0; std::string path; std::string value; char buf[2048]; bool dirty = false; };

static std::vector<PlanetInfo> g_planets;
static std::vector<FieldRow> g_fields;

// 星球形状预览（由 C# 端 SHAPE 命令推送）
static std::vector<float> g_shape;      // 一圈地形高度（米）
static double g_shapeRadius = 0.0;
static int g_shapeCol[3] = { 150, 150, 150 };
static double g_lastShape = 0.0;
static std::vector<float> g_shapeHi;   // 导出用的高精度轮廓
static bool g_wantFine = false;      // 正在等高精度数据
static bool g_readingFine = false;

static bool ExportHeightmapPng(const std::vector<float>& src);

// 界面：0=主界面 1=笔刷编辑（带过渡动画）
static int g_view = 0;
static float g_viewAnim = 0.f;
static int g_samples = 4096;
static float g_rate = 250.f;
static float g_zoomX = 1.f;     // 高度图横向缩放
static float g_zoomY = 1.f;     // 纵向缩放
static float g_panX = 0.5f;     // 视图中心（0..1）
static int g_brushShape = 0;    // 0圆滑 1方头 2尖峰
static int g_paintMode = 0;     // 0=画笔(加减) 1=吸附(拉向光标)
static bool g_confirmClear = false;
static bool g_renameOpen = false;
static char g_renameBuf[64] = "";
static bool g_delPlanetConfirm = false;
static float g_pendingRefresh = -1.f;   // >0 表示倒计时后刷新字段
static float g_viewMinH = 0.f;  // 高度图纵向视野（固定，避免一画就缩放）
static float g_viewMaxH = 0.f;
static bool g_viewRangeInit = false;
static std::string g_selected;          // 当前星球
static std::vector<std::string> g_log;
static std::string g_status = "未连接";
static char g_filter[64] = "";
static std::string g_modSel;          // 当前查看的模块（空 = 全部）
static bool g_paint = false;
static float g_brush = 2000.f;
static int g_pending = 0;               // 还需读取的行数
static int g_pending_kind = 0;          // 1=planets 2=fields
static double g_last_poll = 0.0;

static void LogLine(const std::string& s) {
    g_log.push_back(s);
    if (g_log.size() > 200) g_log.erase(g_log.begin(), g_log.begin() + 100);
}

static void SendCmd(const std::string& s) { g_bridge.Send(s); }

static void RequestPlanets() { g_pending_kind = 1; g_pending = -1; SendCmd("LIST"); }
static void RequestFields(const std::string& code) {
    g_fields.clear();
    g_pending_kind = 2; g_pending = -1;
    SendCmd("FIELDS " + code);
}

static void DoConnect() {
    if (g_bridge.Connect(g_port)) {
        g_status = "已连接 :" + std::to_string(g_port);
        LogLine("connected to 127.0.0.1:" + std::to_string(g_port));
        SendCmd("STATUS");
        RequestPlanets();
    } else {
        g_status = "未连接（先在游戏里进世界）";
        LogLine(g_bridge.LastError());
    }
}

static void HandleLine(const std::string& line) {
    // 多行响应
    if (g_pending > 0) {
        if (g_pending_kind == 1) {
            // code \t radius \t gravity
            size_t a = line.find('\t');
            size_t b = line.find('\t', a == std::string::npos ? 0 : a + 1);
            PlanetInfo p;
            if (a != std::string::npos) {
                p.code = line.substr(0, a);
                if (b != std::string::npos) {
                    p.radius = atoi(line.substr(a + 1, b - a - 1).c_str());
                    p.gravity = line.substr(b + 1);
                }
            }
            if (!p.code.empty()) g_planets.push_back(p);
        } else if (g_pending_kind == 3) {
            float hv = (float)atof(line.c_str());
            if (g_readingFine) g_shapeHi.push_back(hv); else g_shape.push_back(hv);
        } else if (g_pending_kind == 2) {
            // kind \t path \t value
            size_t a = line.find('\t');
            size_t b = line.find('\t', a == std::string::npos ? 0 : a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                FieldRow f;
                f.kind = atoi(line.substr(0, a).c_str());
                f.path = line.substr(a + 1, b - a - 1);
                f.value = line.substr(b + 1);
                strncpy(f.buf, f.value.c_str(), sizeof(f.buf) - 1);   // 公式可能很长
                f.buf[sizeof(f.buf) - 1] = 0;
                g_fields.push_back(f);
            }
        }
        g_pending--;
        if (g_pending == 0) {
            if (g_pending_kind == 1)
                LogLine("planet list: " + std::to_string(g_planets.size()) + " 个");
            else if (g_pending_kind == 2)
                LogLine("fields: " + std::to_string(g_fields.size()) + " 项");
            else if (g_pending_kind == 3 && g_readingFine) {
                g_readingFine = false;
                g_wantFine = false;
                ExportHeightmapPng(g_shapeHi);
            }
            g_pending_kind = 0;
        }
        return;
    }

    if (line.rfind("PONG", 0) == 0 || line.rfind("STATUS", 0) == 0) { LogLine(line); return; }
    if (line.rfind("PLANETS ", 0) == 0) {
        g_planets.clear();
        g_pending = atoi(line.c_str() + 8);
        g_pending_kind = 1;
        if (g_pending == 0) g_pending_kind = 0;
        return;
    }
    if (line.rfind("SHAPE ", 0) == 0) {
        // SHAPE <code> <n> <radius> <r> <g> <b>
        char code[128] = {0};
        int n = 0, r = 150, gg = 150, b = 150;
        double radius = 0.0;
        if (sscanf_s(line.c_str(), "SHAPE %127s %d %lf %d %d %d", code, (unsigned)sizeof(code), &n, &radius, &r, &gg, &b) >= 4) {
            if (g_wantFine) {
                g_shapeHi.clear();
                g_shapeHi.reserve(n > 0 ? n : 0);
                g_readingFine = true;
            } else {
                g_shape.clear();
                g_shape.reserve(n > 0 ? n : 0);
                g_readingFine = false;
                g_lastShape = ImGui::GetTime();
            }
            g_shapeRadius = radius;
            g_shapeCol[0] = r; g_shapeCol[1] = gg; g_shapeCol[2] = b;
            g_pending = n;
            g_pending_kind = 3;
        }
        return;
    }
    if (line.rfind("FIELDS ", 0) == 0) {
        int n = 0;
        sscanf_s(line.c_str() + 7, "%*s %d", &n);
        g_pending = n;
        g_pending_kind = 2;
        if (g_pending == 0) g_pending_kind = 0;
        return;
    }
    if (line.rfind("OK", 0) == 0 || line.rfind("ERR", 0) == 0) { LogLine(line); return; }
    LogLine(line);
}

static void PollBridge() {
    if (!g_bridge.Connected()) return;
    std::vector<std::string> lines = g_bridge.Poll();
    for (auto& l : lines) HandleLine(l);
    if (!g_bridge.Connected()) { g_status = "连接已断开"; LogLine("disconnected"); }
}

// ---------------- ImGui 样式 ----------------
static void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 16.f;
    s.ChildRounding = 12.f;
    s.FrameRounding = 8.f;
    s.GrabRounding = 8.f;
    s.PopupRounding = 10.f;
    s.WindowPadding = ImVec2(14, 14);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ScrollbarRounding = 8.f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0, 0, 0, 0);
    s.ScrollbarSize = 10.f;
    s.ScrollbarRounding = 99.f;
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.65f, 1.f, 0.35f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.65f, 1.f, 0.55f);
    c[ImGuiCol_Header] = ImVec4(0.35f, 0.65f, 1.f, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.65f, 1.f, 0.45f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.97f, 1.f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.65f, 0.70f, 0.78f, 1.f);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------- 贴靠到游戏窗口右半 ----------------
static HWND FindGameWindow() {
    HWND h = FindWindowW(L"UnityWndClass", L"Spaceflight Simulator");
    if (h && IsWindowVisible(h)) return h;
    h = FindWindowW(nullptr, L"Spaceflight Simulator");
    return (h && IsWindowVisible(h)) ? h : nullptr;
}

// 面板占「游戏窗口左边那半屏」：x 从显示器工作区左边到游戏左边，高度对齐游戏
static void DockLeftOfGame() {
    HWND game = FindGameWindow();
    if (!game) {
        // 没有游戏：主屏左半
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, sw / 2, sh, SWP_SHOWWINDOW);
        return;
    }

    RECT gr{};
    GetWindowRect(game, &gr);
    HMONITOR mon = MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    int top = gr.top < mi.rcWork.top ? mi.rcWork.top : gr.top;
    int bottom = gr.bottom > mi.rcWork.bottom ? mi.rcWork.bottom : gr.bottom;
    int h = bottom - top;
    if (h < 300) { top = mi.rcWork.top; h = mi.rcWork.bottom - mi.rcWork.top; }

    int x = mi.rcWork.left;
    int w = gr.left - x;                 // 游戏左边到显示器左边的全部空间
    if (w < 420) {                       // 左边没空间：退回占游戏左半，避免压到游戏
        w = (gr.right - gr.left) / 2;
        x = gr.left;
    }
    SetWindowPos(g_hwnd, HWND_TOP, x, top, w, h, SWP_SHOWWINDOW);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        if (g_dev != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_swap->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            RECT rc; GetClientRect(hWnd, &rc);
            g_glass.Resize(rc.right - rc.left, rc.bottom - rc.top);
        }
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) return 0;   // 客户区 = 整窗（无系统标题栏，保留缩放边框）
        break;
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &p);
        RECT rc; GetClientRect(hWnd, &rc);
        const int b = 6;
        bool left = p.x < b, right = p.x > rc.right - b, top = p.y < b, bottom = p.y > rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
        mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
        mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
        mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------- 界面 ----------------
// 标准标题栏按钮：自绘线条，无底色；关闭键悬停变红
static bool g_customMax = false;
static RECT g_preMaxRect{};

static void ToggleMaximize() {
    if (!g_customMax) {
        GetWindowRect(g_hwnd, &g_preMaxRect);
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        SetWindowPos(g_hwnd, nullptr, mi.rcWork.left, mi.rcWork.top,
                     mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        g_customMax = true;
    } else {
        SetWindowPos(g_hwnd, nullptr, g_preMaxRect.left, g_preMaxRect.top,
                     g_preMaxRect.right - g_preMaxRect.left, g_preMaxRect.bottom - g_preMaxRect.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        g_customMax = false;
    }
}
static float g_capAnim[3] = { 0.f, 0.f, 0.f };

// 标题栏按钮：悬停 = 半透明白底 + 整体放大 + 线条加粗，全部带过渡
static bool CaptionButton(const char* id, int kind) {
    const float w = 46.f, h = 32.f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    bool clicked = ImGui::IsItemClicked(0);

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;
    float target = (hovered || held) ? 1.f : 0.f;
    int idx = (kind < 0) ? 0 : (kind > 2 ? 2 : kind);
    float& a = g_capAnim[idx];
    a += (target - a) * (dt * 16.f);
    if (a < 0.002f) a = 0.f;
    if (a > 0.998f) a = 1.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float scale = 1.f + 0.12f * a;
    ImVec2 c(p.x + w * 0.5f, p.y + h * 0.5f);
    ImVec2 hs(w * 0.5f * scale - 3.f, h * 0.5f * scale - 2.f);

    if (a > 0.01f) {
        dl->AddRectFilled(ImVec2(c.x - hs.x, c.y - hs.y), ImVec2(c.x + hs.x, c.y + hs.y),
                          IM_COL32(255, 255, 255, (int)(44.f * a)), 8.f);
    }

    ImU32 col = IM_COL32(255, 255, 255, (int)(200.f + 55.f * a));
    float th = 1.4f + 1.0f * a;
    float g = 5.0f * (1.f + 0.10f * a);
    if (kind == 0) {
        dl->AddLine(ImVec2(c.x - g - 1.f, c.y), ImVec2(c.x + g + 1.f, c.y), col, th);
    } else if (kind == 1) {
        dl->AddRect(ImVec2(c.x - g, c.y - g), ImVec2(c.x + g, c.y + g), col, 0.f, 0, th);
    } else {
        dl->AddLine(ImVec2(c.x - g, c.y - g), ImVec2(c.x + g, c.y + g), col, th);
        dl->AddLine(ImVec2(c.x - g, c.y + g), ImVec2(c.x + g, c.y - g), col, th);
    }
    return clicked;
}
static void DrawTitleBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##title", ImVec2(0, 32), ImGuiChildFlags_None);
    float w = ImGui::GetWindowWidth();

    float textY = (32.f - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos(ImVec2(16, textY));
    ImGui::TextUnformatted("WORLD  CREATER");
    ImGui::SameLine(190);
    ImGui::SetCursorPosY(textY);
    ImGui::TextDisabled("%s", g_status.c_str());

    // 右侧三个按钮：绝对定位，垂直居中一致
    const float bw = 46.f;
    float bx = w - bw * 3.f;
    ImGui::SetCursorPos(ImVec2(bx, 0.f));
    if (CaptionButton("##min", 0)) ShowWindow(g_hwnd, SW_MINIMIZE);
    ImGui::SetCursorPos(ImVec2(bx + bw, 0.f));
    if (CaptionButton("##max", 1)) ToggleMaximize();
    ImGui::SetCursorPos(ImVec2(bx + bw * 2.f, 0.f));
    if (CaptionButton("##close", 2)) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

    // 拖动（避开按钮区）
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##drag", ImVec2(bx - 4.f > 40.f ? bx - 4.f : 40.f, 32));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        ReleaseCapture();
        SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
// 选中/悬停的圆角高亮（自绘，带缓动）
struct HoverPill {
    ImVec2 fromMin, fromMax, toMin, toMax, curMin, curMax;
    float t = 1.f;
    float alpha = 0.f;
    bool started = false;
};
static HoverPill g_pill;
// 玻璃滚动条：只描边（轨道极淡 + 滑块亮边），需要全屏裁剪区否则会被内容区裁掉
static void DrawGlassScrollbar() {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win->ScrollbarY) return;
    ImRect bb = ImGui::GetWindowScrollbarRect(win, ImGuiAxis_Y);
    ImDrawList* sl = ImGui::GetWindowDrawList();
    sl->PushClipRectFullScreen();

    ImVec2 t0(bb.Min.x + 2.f, bb.Min.y + 4.f), t1(bb.Max.x - 2.f, bb.Max.y - 4.f);
    if (t1.y - t0.y > 30.f) {
        sl->AddRect(t0, t1, IM_COL32(255, 255, 255, 22), 99.f, 0, 1.f);
        float viewH = t1.y - t0.y;
        float smax = ImGui::GetScrollMaxY();
        if (smax > 1.f) {
            float grabH = viewH * (viewH / (viewH + smax));
            if (grabH < 26.f) grabH = 26.f;
            float t = ImGui::GetScrollY() / smax;
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            float y0 = t0.y + (viewH - grabH) * t;
            ImVec2 g0(t0.x, y0), g1(t1.x, y0 + grabH);
            sl->AddRectFilled(g0, g1, IM_COL32(255, 255, 255, 14), 99.f);
            sl->AddRect(g0, g1, IM_COL32(255, 255, 255, 125), 99.f, 0, 1.2f);
        }
    }
    sl->PopClipRect();
}
static void RequestShape();

// ---- 导出高度图 PNG（WIC 编码，带 alpha）----
#include <wincodec.h>
#include <vector>
#include <cmath>
#include <string>

static int   g_expW = 2048;
static int   g_expH = 512;
static float g_expColor[3] = { 1.0f, 1.0f, 1.0f };
static char  g_expMsg[256] = "";

static bool WritePngRGBA(const wchar_t* path, int w, int h, const std::vector<unsigned char>& px) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;

    if (SUCCEEDED(factory->CreateStream(&stream)) && stream &&
        SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && enc &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && frame &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize((UINT)w, (UINT)h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->SetPixelFormat(&fmt))) {
            UINT stride = (UINT)(w * 4);
            if (SUCCEEDED(frame->WritePixels((UINT)h, stride, (UINT)px.size(), (BYTE*)px.data())) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
                ok = true;
            }
        }
    }
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    factory->Release();
    return ok;
}

// 地形曲线以下填纯色、以上完全透明
static bool ExportHeightmapPng(const std::vector<float>& src) {
    if (src.size() < 8) { snprintf(g_expMsg, sizeof(g_expMsg), "没有地形数据"); return false; }

    int w = g_expW, h = g_expH;
    if (w < 16) w = 16; if (w > 8192) w = 8192;
    if (h < 16) h = 16; if (h > 8192) h = 8192;

    float mn = src[0], mx = src[0];
    for (float v : src) { if (v < mn) mn = v; if (v > mx) mx = v; }
    if (mx - mn < 1e-3f) mx = mn + 1.0f;

    std::vector<unsigned char> px((size_t)w * h * 4, 0);   // 全透明
    unsigned char R = (unsigned char)(g_expColor[0] * 255.f + 0.5f);
    unsigned char G = (unsigned char)(g_expColor[1] * 255.f + 0.5f);
    unsigned char B = (unsigned char)(g_expColor[2] * 255.f + 0.5f);

    const int n = (int)src.size();
    for (int x = 0; x < w; x++) {
        // 线性插值，避免出现一个个矩形台阶
        double pos = (w > 1) ? (double)x * (n - 1) / (w - 1) : 0.0;
        int i0 = (int)pos;
        if (i0 > n - 1) i0 = n - 1;
        int i1 = (i0 + 1 < n) ? i0 + 1 : n - 1;
        float frac = (float)(pos - i0);
        float v = src[i0] * (1.f - frac) + src[i1] * frac;
        float t = (v - mn) / (mx - mn);
        if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
        int topY = h - 1 - (int)(t * (h - 1));      // 该列地形顶部所在行（0=最上）
        for (int y = topY; y < h; y++) {
            unsigned char* p = &px[((size_t)y * w + x) * 4];
            p[0] = B; p[1] = G; p[2] = R; p[3] = 255;
        }
    }

    CreateDirectoryA("Exports", nullptr);
    char name[128];
    snprintf(name, sizeof(name), "Exports\\heightmap_%s_%dx%d.png",
             g_selected.empty() ? "planet" : g_selected.c_str(), w, h);
    wchar_t wpath[512];
    MultiByteToWideChar(CP_ACP, 0, name, -1, wpath, 512);

    if (WritePngRGBA(wpath, w, h, px)) {
        snprintf(g_expMsg, sizeof(g_expMsg), "已导出 %s", name);
        LogLine(std::string("export ") + name);
        return true;
    }
    snprintf(g_expMsg, sizeof(g_expMsg), "导出失败（写文件出错）");
    return false;
}

// ---- 统一按钮样式：悬停=列表行那种白雾，按下=白底+淡蓝字 ----
static bool GlassButton(const char* label, ImVec2 size = ImVec2(0, 0), bool alignLeft = false, bool danger = false, bool selected = false) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems) return false;

    ImGuiStyle& style = g.Style;
    ImVec2 textSize = ImGui::CalcTextSize(label, NULL, true);
    if (size.x <= 0) size.x = textSize.x + style.FramePadding.x * 2.f + 12.f;
    if (size.y <= 0) size.y = ImGui::GetFrameHeight();

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(bb, style.FramePadding.y);
    ImGuiID id = window->GetID(label);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    const float a = g.Style.Alpha;
    ImU32 bg, fg;
    if (held || selected) {           // 按下 / 已选中：白底 + 淡蓝色字
        bg = IM_COL32(255, 255, 255, (int)((held ? 238 : 214) * a));
        fg = IM_COL32(88, 146, 232, (int)(255 * a));
    } else if (hovered) {             // 悬停：和星球列表行一致的白雾
        bg = IM_COL32(255, 255, 255, (int)(60 * a));
        fg = IM_COL32(242, 246, 252, (int)(255 * a));
    } else {
        bg = IM_COL32(255, 255, 255, (int)(13 * a));
        fg = IM_COL32(225, 231, 240, (int)(255 * a));
    }
    window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, 8.f);
    ImVec2 tp;
    if (alignLeft)
        tp = ImVec2(bb.Min.x + 12.f, bb.Min.y + (size.y - textSize.y) * 0.5f);
    else
        tp = ImVec2(bb.Min.x + (size.x - textSize.x) * 0.5f, bb.Min.y + (size.y - textSize.y) * 0.5f);
    window->DrawList->AddText(tp, fg, label);
    return pressed;
}

// ---- 添加星球：带展开动画的下拉 ----
static float g_npAnim = 0.f;
static bool  g_npOpen = false;
static bool  g_npOverBtn = false;
static bool  g_npOverMenu = false;
static ImVec2 g_npPos = ImVec2(0, 0);
static float g_npWidth = 220.f;

static void DrawNewPlanetMenu() {
    static const char* tKeys[6]  = { "earth", "rocky", "gas", "ice", "asteroid", "bary" };
    static const char* tNames[6] = { "类地行星", "岩石行星", "气态巨行星", "冰卫星", "小行星", "双星质心" };

    if (g_npAnim <= 0.003f) return;

    const float rowH = 28.f;
    const float h = 6 * rowH + 16.f;
    ImGui::SetNextWindowPos(ImVec2(g_npPos.x, g_npPos.y + (1.f - g_npAnim) * -8.f));
    ImGui::SetNextWindowSize(ImVec2(g_npWidth, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_npAnim);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.12f, 0.94f));
    ImGui::Begin("##npmenu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    g_npOverMenu = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    for (int i = 0; i < 6; i++) {
        if (GlassButton(tNames[i], ImVec2(g_npWidth - 12.f, rowH), true)) {
            SendCmd(std::string("CMD newplanet ") + tKeys[i] + " live");
            LogLine(std::string("newplanet ") + tKeys[i]);
            g_npOpen = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

static void DrawPlanetList() {
    // ---- 固定头部：不随列表滚动 ----
    if (GlassButton("刷新列表", ImVec2(96, 26))) RequestPlanets();
    ImGui::SameLine();
    ImGui::TextDisabled("%d", (int)g_planets.size());
    ImGui::SameLine();
    {
        float bw = ImGui::GetContentRegionAvail().x;
        if (GlassButton("＋ 添加星球", ImVec2(bw, 26))) g_npOpen = !g_npOpen;
        g_npOverBtn = ImGui::IsItemHovered();
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        g_npPos = ImVec2(rmin.x, rmax.y + 4.f);
        g_npWidth = bw;
    }
    // 动画推进
    {
        float target = g_npOpen ? 1.f : 0.f;
        float dt = ImGui::GetIO().DeltaTime; if (dt > 0.1f) dt = 0.1f;
        float spd = 1.f - expf(-18.f * dt);
        g_npAnim += (target - g_npAnim) * spd;
        if (!g_npOpen && g_npAnim < 0.01f) g_npAnim = 0.f;
        if (g_npAnim > 0.995f) g_npAnim = 1.f;
        if (g_npOpen && ImGui::IsMouseClicked(0) && !g_npOverBtn && !g_npOverMenu) g_npOpen = false;
    }
    ImGui::Separator();

    ImGui::BeginChild("##planetlist", ImVec2(240, -338), ImGuiChildFlags_Borders);

    const float rowH = 30.f, gap = 3.f;
    const float padX = 6.f, padY = 4.f, textLeft = 12.f, rounding = 12.f;
    const float step = rowH + gap + padY * 2.f;
    const int n = (int)g_planets.size();
    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;

    ImVec2 start = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;

    // 1) 每行块矩形 + 命中
    std::vector<ImRect> rows(n);
    int hovered = -1;
    bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
    for (int i = 0; i < n; i++) {
        float y = start.y + i * step;
        rows[i] = ImRect(ImVec2(start.x - padX, y - padY),
                         ImVec2(start.x + availW + padX, y + rowH + padY));
        if (windowHovered && ImGui::IsMouseHoveringRect(rows[i].Min, rows[i].Max)) hovered = i;
    }

    // 2) 选中过渡（指数趋近）：圆角块 半透明→不透明，文字 灰白→淡蓝
    for (int i = 0; i < n; i++) {
        float target = (g_planets[i].code == g_selected) ? 1.f : 0.f;
        float a = g_planets[i].selAnim + (target - g_planets[i].selAnim) * (dt * 13.f);
        if (a < 0.003f) a = 0.f;
        if (a > 0.997f) a = 1.f;
        g_planets[i].selAnim = a;
    }

    // 3) 悬停块缓动（smoothstep：慢-快-慢）
    if (hovered >= 0) {
        ImRect tgt = rows[hovered];
        bool changed = !g_pill.started ||
                       tgt.Min.x != g_pill.toMin.x || tgt.Min.y != g_pill.toMin.y ||
                       tgt.Max.x != g_pill.toMax.x || tgt.Max.y != g_pill.toMax.y;
        if (changed) {
            g_pill.fromMin = g_pill.started ? g_pill.curMin : tgt.Min;
            g_pill.fromMax = g_pill.started ? g_pill.curMax : tgt.Max;
            g_pill.toMin = tgt.Min; g_pill.toMax = tgt.Max;
            g_pill.curMin = g_pill.fromMin; g_pill.curMax = g_pill.fromMax;
            g_pill.t = 0.f;
            g_pill.started = true;
        }
        g_pill.alpha += dt * 10.f; if (g_pill.alpha > 1.f) g_pill.alpha = 1.f;
    } else if (g_pill.started) {
        g_pill.alpha -= dt * 10.f;
        if (g_pill.alpha < 0.f) { g_pill.alpha = 0.f; g_pill.started = false; }
    }
    if (g_pill.started && g_pill.t < 1.f) {
        g_pill.t += dt / 0.16f;
        if (g_pill.t > 1.f) g_pill.t = 1.f;
        float e = g_pill.t * g_pill.t * (3.f - 2.f * g_pill.t);
        g_pill.curMin.x = g_pill.fromMin.x + (g_pill.toMin.x - g_pill.fromMin.x) * e;
        g_pill.curMin.y = g_pill.fromMin.y + (g_pill.toMin.y - g_pill.fromMin.y) * e;
        g_pill.curMax.x = g_pill.fromMax.x + (g_pill.toMax.x - g_pill.fromMax.x) * e;
        g_pill.curMax.y = g_pill.fromMax.y + (g_pill.toMax.y - g_pill.fromMax.y) * e;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 4) 背景层：先悬停块，再选中块（选中盖住悬停）
    if (g_pill.started && g_pill.alpha > 0.01f) {
        int a = (int)(30.f * g_pill.alpha);
        dl->AddRectFilled(g_pill.curMin, g_pill.curMax, IM_COL32(255, 255, 255, a), rounding);
    }
    for (int i = 0; i < n; i++) {
        float s = g_planets[i].selAnim;
        if (s <= 0.001f) continue;
        int a = (int)(30.f + (255.f - 30.f) * s);
        dl->AddRectFilled(rows[i].Min, rows[i].Max, IM_COL32(255, 255, 255, a), rounding);
    }

    // 5) 内容层
    for (int i = 0; i < n; i++) {
        PlanetInfo& p = g_planets[i];
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(rows[i].Min.x + padX, rows[i].Min.y + padY));
        ImGui::InvisibleButton("##row", ImVec2(availW > 1.f ? availW : 1.f, rowH));
        bool clicked = ImGui::IsItemClicked(0);
        ImGui::PopID();

        float s = p.selAnim;
        int r = (int)(210.f + (58.f - 210.f) * s);
        int g2 = (int)(218.f + (140.f - 218.f) * s);
        int b = (int)(230.f + (240.f - 230.f) * s);
        ImU32 col = IM_COL32(r, g2, b, 255);
        char label[160];
        snprintf(label, sizeof(label), "%s", p.code.c_str());
        ImVec2 ts = ImGui::CalcTextSize(label);
        float ty = rows[i].Min.y + padY + (rowH - ts.y) * 0.5f;
        dl->AddText(ImVec2(rows[i].Min.x + padX + textLeft, ty), col, label);

        if (clicked) {
            g_selected = p.code;
            g_shape.clear();
            RequestFields(p.code);
            RequestShape();
        }
    }

    ImGui::Dummy(ImVec2(1, n * step));

    DrawGlassScrollbar();

    ImGui::EndChild();
    DrawNewPlanetMenu();
}

// 星球数据模块（和 FieldTree 的 key 前缀一一对应）
static const char* kModules[9] = { "basics", "atmo", "atmoVis", "terrain", "water", "rings", "clouds", "post", "orbit" };
static const char* kModuleLong[9] = { "基础", "大气物理", "大气视觉", "地形", "海洋", "行星环", "云层", "后处理", "轨道" };
static bool g_modOpen[9] = { true, true, true, true, false, false, false, false, false };
static float g_modAnim[9] = { 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f };   // 折叠动画进度
static int g_delModPending = -1;
static const bool  kModuleRemovable[9] = { false, true, true, false, true, true, true, true, false };

static int CountModule(const char* mod) {
    int n = 0;
    std::string prefix = std::string(mod) + ".";
    for (auto& f : g_fields) if (f.path.rfind(prefix, 0) == 0) n++;
    return n;
}

static void RefreshFields() {
    if (g_selected.empty()) return;
    g_fields.clear();
    RequestFields(g_selected);
    RequestShape();
}
static void DrawFields() {
    if (g_selected.empty()) {
        ImGui::TextDisabled("← 左边选一个星球");
        return;
    }
    ImGui::Text("%s", g_selected.c_str());
    ImGui::SameLine();
    if (GlassButton("应用")) SendCmd("CMD apply");
    ImGui::SameLine();
    if (GlassButton("保存")) SendCmd("CMD save");
    ImGui::SameLine();
    if (GlassButton("重读")) { SendCmd("CMD reload"); RequestFields(g_selected); }
    ImGui::SameLine();
    if (GlassButton("改名")) g_renameOpen = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("重命名当前星球（会同步其它星球的 orbit.parent 和文件名）");
    ImGui::SameLine();
    if (GlassButton("删星球")) g_delPlanetConfirm = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("从包和运行时里删除当前星球");
    ImGui::SameLine();
    if (GlassButton("质心")) {
        SendCmd("CMD soifit " + g_selected);
        LogLine("soifit " + g_selected);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("把该星球的引力范围(SOI)撑到覆盖所有子天体轨道，适合双星质心");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##filter", "过滤字段…", g_filter, sizeof(g_filter));


    ImGui::Separator();
    ImGui::BeginChild("##fields", ImVec2(0, 0), ImGuiChildFlags_Borders);

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;
    float winW = ImGui::GetContentRegionAvail().x;
    int shownTotal = 0;

    for (int m = 0; m < 9; m++) {
        int cnt = CountModule(kModules[m]);
        bool forced = (g_filter[0] != 0);

        // ---- 折叠动画进度 ----
        float target = (g_modOpen[m] || forced) ? 1.f : 0.f;
        g_modAnim[m] += (target - g_modAnim[m]) * (dt * 14.f);
        if (g_modAnim[m] < 0.002f) g_modAnim[m] = 0.f;
        if (g_modAnim[m] > 0.998f) g_modAnim[m] = 1.f;
        float anim = g_modAnim[m];

        ImGui::PushID(m);

        // ---- 标题行：左侧区域点击折叠（右侧留给按钮，避免抢点击）----
        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const float btnW = 96.f;
        ImGui::InvisibleButton("##hdr", ImVec2(winW - btnW - 8.f, 26));
        if (ImGui::IsItemClicked(0)) g_modOpen[m] = !g_modOpen[m];
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        {
            // 三角箭头：展开 ▲（0），折叠 ▼（π），随动画连续旋转
            float rot = (1.f - anim) * 3.14159265f;
            ImVec2 c(rowPos.x + 13.f, rowPos.y + 13.f);
            float r = 6.0f;
            ImVec2 pts[3];
            for (int k = 0; k < 3; k++) {
                float a = rot + (-1.5707963f) + k * 2.0943951f;
                pts[k] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
            }
            dl->AddTriangleFilled(pts[0], pts[1], pts[2], IM_COL32(215, 224, 238, 255));
        }
        char hdr[96];
        if (cnt > 0) snprintf(hdr, sizeof(hdr), "%s  (%d)", kModuleLong[m], cnt);
        else snprintf(hdr, sizeof(hdr), "%s  （未启用）", kModuleLong[m]);
        dl->AddText(ImVec2(rowPos.x + 28.f, rowPos.y + (26.f - ImGui::GetTextLineHeight()) * 0.5f),
                    cnt > 0 ? IM_COL32(235, 240, 248, 255) : IM_COL32(140, 150, 165, 255), hdr);

        // ---- 右侧按钮（半透明底，文字居中）----
        {
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + winW - btnW, rowPos.y));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
            if (kModuleRemovable[m] && cnt > 0) {
                if (GlassButton("删除模块", ImVec2(btnW, 26), false, true)) g_delModPending = m;
            } else if (cnt == 0) {
                if (GlassButton("启用", ImVec2(btnW, 26))) {
                    SendCmd("CMD addmod " + g_selected + " " + kModules[m]);
                    g_pendingRefresh = 0.6f;
                    g_modOpen[m] = true;
                }
            }
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + 28.f));
        }

        // ---- 内容：逐行揭示（anim 决定显示多少行）----
        if (anim > 0.002f && cnt > 0) {
            std::string prefix = std::string(kModules[m]) + ".";
            std::vector<FieldRow*> rows;
            for (auto& f : g_fields) {
                if (f.path.rfind(prefix, 0) != 0) continue;
                if (g_filter[0] && f.path.find(g_filter) == std::string::npos) continue;
                rows.push_back(&f);
            }
            int total = (int)rows.size();
            int show = (int)ceilf(total * anim);
            if (show > total) show = total;

            for (int ri = 0; ri < show; ri++) {
                FieldRow* fp = rows[ri];
                ImGui::PushID(fp->path.c_str());
                bool committed = false;
                ImGui::SetCursorPosX(6.f);
                ImGui::AlignTextToFramePadding();
                if (fp->kind == 5) {
                    // 公式单独占一行，下面放整宽的多行编辑器
                    ImGui::TextDisabled("%s", fp->path.c_str());
                    ImGui::SetCursorPosX(8.f);
                    float bw = winW - 28.f;
                    if (bw < 200.f) bw = 200.f;
                    committed = ImGui::InputTextMultiline("##f", fp->buf, sizeof(fp->buf),
                                                          ImVec2(bw, 108.f),
                                                          ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
                } else {
                    ImGui::TextDisabled("%s", fp->path.c_str());
                    ImGui::SameLine();
                    float rx = winW - 215.f;
                    if (rx < 150.f) rx = 150.f;
                    ImGui::SetCursorPosX(rx);
                    ImGui::SetNextItemWidth(200);
                    committed = ImGui::InputText("##v", fp->buf, sizeof(fp->buf), ImGuiInputTextFlags_EnterReturnsTrue);
                    if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;   // 失焦也算提交
                }
                if (committed) {
                    SendCmd("SET " + g_selected + " " + fp->path + " " + std::string(fp->buf));
                    fp->value = fp->buf;
                }
                ImGui::PopID();
            }
            shownTotal += show;
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    if (shownTotal == 0 && g_filter[0]) ImGui::TextDisabled("(没有匹配的字段)");
    DrawGlassScrollbar();
    ImGui::EndChild();

    // ---- 删除确认弹窗 ----
    if (g_delModPending >= 0) ImGui::OpenPopup("##delmod");
    {
        // 弹窗也走半透明风格
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.08f, 0.11f, 0.62f));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.25f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    }
    if (ImGui::BeginPopupModal("##delmod", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const char* modName = (g_delModPending >= 0 && g_delModPending < 9) ? kModuleLong[g_delModPending] : "?";
        const float POP_W = 340.f;
        ImGui::Dummy(ImVec2(POP_W, 0.f));   // 固定内容宽度，方便按钮居中
        ImGui::Spacing();
        ImGui::Text("确定要删除「%s」模块吗？", modName);
        ImGui::Spacing();
        ImGui::TextDisabled("删除后该模块的字段会从星球上移除。");
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            float x = (POP_W - total) * 0.5f;
            if (x < 0.f) x = 0.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
        if (ImGui::Button("确定删除", ImVec2(120, 32))) {
            if (g_delModPending >= 0 && g_delModPending < 9) {
                SendCmd("CMD delmod " + g_selected + " " + kModules[g_delModPending]);
                g_pendingRefresh = 0.6f;
                LogLine(std::string("delmod ") + kModules[g_delModPending]);
            }
            g_delModPending = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 32))) {
            g_delModPending = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}


static void RequestShape() {
    if (g_selected.empty()) return;
    SendCmd("SHAPE " + g_selected);
}

// ---------------- 滑条 + 数值框 ----------------
static bool SliderWithInput(const char* label, float* v, float lo, float hi,
                            const char* fmt, const char* cmdFmt) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.f);
    if (ImGui::SliderFloat((std::string("##s") + label).c_str(), v, lo, hi, fmt)) changed = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat((std::string("##n") + label).c_str(), v, 0.f, 0.f, fmt)) {
        if (*v < lo) *v = lo;
        if (*v > hi) *v = hi;
        changed = true;
    }
    if (changed) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), cmdFmt, *v);
        SendCmd(cmd);
    }
    return changed;
}

static bool SliderWithInputInt(const char* label, int* v, int lo, int hi, const char* cmdFmt) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.f);
    if (ImGui::SliderInt((std::string("##s") + label).c_str(), v, lo, hi)) changed = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputInt((std::string("##n") + label).c_str(), v)) {
        if (*v < lo) *v = lo;
        if (*v > hi) *v = hi;
        changed = true;
    }
    if (changed) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), cmdFmt, *v);
        SendCmd(cmd);
    }
    return changed;
}

// ---------------- 左侧小预览：剪影 + 剖面 ----------------
static void DrawPreview() {
    const double PI = 3.14159265358979323846;
    ImGui::Separator();
    float W = ImGui::GetContentRegionAvail().x;
    if (W < 60.f) W = 60.f;
    const float H = 132.f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + H), IM_COL32(255, 255, 255, 12), 10.f);

    if (g_shape.size() < 8 || g_shapeRadius <= 0.0) {
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##prevclick0", ImVec2(W, H));
        if (ImGui::IsItemClicked(0)) g_view = 1;
        dl->AddText(ImVec2(p.x + 12.f, p.y + H * 0.5f - 8.f),
                    IM_COL32(150, 160, 175, 255), "选中星球后显示预览");
        return;
    }

    float D = H - 16.f;
    ImVec2 ctr(p.x + 8.f + D * 0.5f, p.y + H * 0.5f);

    float maxH = 0.f, minH = 0.f;
    for (size_t i = 0; i < g_shape.size(); i++) {
        if (g_shape[i] > maxH) maxH = g_shape[i];
        if (g_shape[i] < minH) minH = g_shape[i];
    }
    double maxR = g_shapeRadius + (double)maxH;
    if (maxR <= 0.0) maxR = 1.0;
    float scale = (D * 0.5f) / (float)(maxR * 1.02);
    int n = (int)g_shape.size();

    ImU32 rim = IM_COL32(
        (int)(g_shapeCol[0] * 0.55f + 255 * 0.45f),
        (int)(g_shapeCol[1] * 0.55f + 255 * 0.45f),
        (int)(g_shapeCol[2] * 0.55f + 255 * 0.45f), 255);

    ImDrawListFlags savedFlags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    const int LAYERS = 8;
    for (int layer = 0; layer < LAYERS; layer++) {
        float t = (float)layer / (float)(LAYERS - 1);
        float ring = 1.0f - 0.72f * t;
        float shade = 0.62f + 0.34f * t;
        ImU32 col = IM_COL32(
            (int)(g_shapeCol[0] * shade),
            (int)(g_shapeCol[1] * shade),
            (int)(g_shapeCol[2] * shade), 255);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double a0 = PI * 2.0 * i / n, a1 = PI * 2.0 * j / n;
            float r0 = (float)((g_shapeRadius + g_shape[i]) * scale) * ring;
            float r1 = (float)((g_shapeRadius + g_shape[j]) * scale) * ring;
            ImVec2 q0(ctr.x + (float)cos(a0) * r0, ctr.y - (float)sin(a0) * r0);
            ImVec2 q1(ctr.x + (float)cos(a1) * r1, ctr.y - (float)sin(a1) * r1);
            dl->AddTriangleFilled(ctr, q0, q1, col);
        }
    }
    dl->Flags = savedFlags;

    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double a0 = PI * 2.0 * i / n, a1 = PI * 2.0 * j / n;
        float r0 = (float)((g_shapeRadius + g_shape[i]) * scale);
        float r1 = (float)((g_shapeRadius + g_shape[j]) * scale);
        ImVec2 q0(ctr.x + (float)cos(a0) * r0, ctr.y - (float)sin(a0) * r0);
        ImVec2 q1(ctr.x + (float)cos(a1) * r1, ctr.y - (float)sin(a1) * r1);
        dl->AddLine(q0, q1, rim, 1.2f);
    }

    float px0 = p.x + 8.f + D + 12.f;
    float px1 = p.x + W - 10.f;
    if (px1 - px0 > 40.f) {
        float py0 = p.y + 20.f, py1 = p.y + H - 22.f;
        if (maxH - minH < 1.f) { maxH = minH + 1.f; }
        float y0 = py1 - (0.f - minH) / (maxH - minH) * (py1 - py0);
        dl->AddLine(ImVec2(px0, y0), ImVec2(px1, y0), IM_COL32(255, 255, 255, 40), 1.f);
        ImVec2 prev(0, 0);
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)(n - 1);
            float x = px0 + (px1 - px0) * t;
            float y = py1 - (g_shape[i] - minH) / (maxH - minH) * (py1 - py0);
            ImVec2 cur(x, y);
            if (i > 0) dl->AddLine(prev, cur, IM_COL32(120, 190, 255, 230), 1.4f);
            prev = cur;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f m", maxH);
        dl->AddText(ImVec2(px0, p.y + 4.f), IM_COL32(150, 160, 175, 255), buf);
    }

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##prevclick", ImVec2(W, H));
    if (ImGui::IsItemClicked(0)) g_view = 1;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("点击进入笔刷编辑（高度图里可以直接画）");
}

// ---------------- 大高度图（可缩放 / 可画） ----------------
static void DrawBigHeightMap(float W, float H) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(W, H);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + H), IM_COL32(255, 255, 255, 10), 12.f);
    dl->PushClipRect(p, ImVec2(p.x + W, p.y + H), true);

    if (g_shape.size() < 8) {
        dl->AddText(ImVec2(p.x + 16, p.y + H * 0.5f - 8), IM_COL32(150, 160, 175, 255),
                    "选中星球后这里显示全周地形高度");
        dl->PopClipRect();
        ImGui::Dummy(sz);
        return;
    }

    float dataMin = 0.f, dataMax = 0.f;
    for (size_t i = 0; i < g_shape.size(); i++) {
        if (g_shape[i] < dataMin) dataMin = g_shape[i];
        if (g_shape[i] > dataMax) dataMax = g_shape[i];
    }
    if (!g_viewRangeInit) {
        float span = dataMax - dataMin;
        if (span < 1.f) span = 1.f;
        g_viewMinH = dataMin - span * 0.12f;
        g_viewMaxH = dataMax + span * 0.30f;
        g_viewRangeInit = true;
    } else if (dataMax > g_viewMaxH || dataMin < g_viewMinH) {
        float span = dataMax - dataMin;
        if (span < 1.f) span = 1.f;
        g_viewMinH = dataMin - span * 0.12f;
        g_viewMaxH = dataMax + span * 0.30f;
    }

    float vh = (g_viewMaxH - g_viewMinH) / g_zoomY;
    float vmid = (g_viewMaxH + g_viewMinH) * 0.5f;
    float minH = vmid - vh * 0.5f;
    float maxH = vmid + vh * 0.5f;

    ImVec2 q0(p.x + 10.f, p.y + 10.f), q1(p.x + W - 10.f, p.y + H - 10.f);

    int n = (int)g_shape.size();
    float vis = 1.f / g_zoomX;
    if (vis > 1.f) vis = 1.f;
    float lo = g_panX - vis * 0.5f, hi = g_panX + vis * 0.5f;
    if (lo < 0.f) { lo = 0.f; hi = vis; }
    if (hi > 1.f) { hi = 1.f; lo = 1.f - vis; }
    g_panX = (lo + hi) * 0.5f;
    auto xOf = [&](float t) { return q0.x + (q1.x - q0.x) * ((t - lo) / (hi - lo)); };
    auto yOf = [&](float h) {
        float t = (h - minH) / (maxH - minH);
        if (t < -0.4f) t = -0.4f;
        if (t > 1.4f) t = 1.4f;
        return q1.y - t * (q1.y - q0.y);
    };

    const ImU32 fillCol = IM_COL32(58, 116, 186, 255);
    for (int i = 0; i < n - 1; i++) {
        float t0 = (float)i / (float)(n - 1);
        float t1 = (float)(i + 1) / (float)(n - 1);
        if (t1 < lo || t0 > hi) continue;
        float xa = xOf(t0);
        float xb = xOf(t1) + 0.9f;
        ImVec2 a(xa, yOf(g_shape[i]));
        ImVec2 b(xb, yOf(g_shape[i + 1]));
        dl->AddQuadFilled(a, b, ImVec2(b.x, q1.y), ImVec2(a.x, q1.y), fillCol);
    }
    for (int i = 0; i <= 4; i++) {
        float y = q0.y + (q1.y - q0.y) * i / 4.f;
        dl->AddLine(ImVec2(q0.x, y), ImVec2(q1.x, y), IM_COL32(255, 255, 255, 26), 1.f);
    }
    for (int i = 1; i < 8; i++) {
        float x = q0.x + (q1.x - q0.x) * i / 8.f;
        dl->AddLine(ImVec2(x, q0.y), ImVec2(x, q1.y), IM_COL32(255, 255, 255, 20), 1.f);
    }
    float yZero = yOf(0.f);
    dl->AddLine(ImVec2(q0.x, yZero), ImVec2(q1.x, yZero), IM_COL32(255, 255, 255, 90), 1.f);
    ImVec2 prev(0, 0);
    bool hasPrev = false;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);
        if (t < lo - 0.002f || t > hi + 0.002f) { hasPrev = false; continue; }
        ImVec2 cur(xOf(t), yOf(g_shape[i]));
        if (hasPrev) dl->AddLine(prev, cur, IM_COL32(140, 200, 255, 255), 1.7f);
        prev = cur;
        hasPrev = true;
    }

    dl->AddText(ImVec2(q0.x + 6, q0.y + 2), IM_COL32(160, 172, 188, 255), "高度 (m)");
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f m", g_viewMaxH);
        dl->AddText(ImVec2(q0.x + 6, q0.y + 20), IM_COL32(120, 132, 150, 220), buf);
        snprintf(buf, sizeof(buf), "%.0f m", g_viewMinH);
        dl->AddText(ImVec2(q0.x + 6, q1.y - 18), IM_COL32(120, 132, 150, 220), buf);
    }
    dl->AddText(ImVec2(q0.x + 6, q1.y - 38), IM_COL32(150, 162, 178, 220),
                "滚轮缩放 / Ctrl+滚轮纵向 / 中键平移 / 左键画 / 右键削");

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##canvas", sz);
    bool hovered = ImGui::IsItemHovered();
    bool lmb = ImGui::IsMouseDown(0) && hovered;
    bool rmb = ImGui::IsMouseDown(1) && hovered;

    ImVec2 mp = ImGui::GetMousePos();
    bool inside = mp.x >= q0.x && mp.x <= q1.x && mp.y >= q0.y && mp.y <= q1.y;
    float angle = 0.f, targetH = 0.f;
    if (inside) {
        float tx = (mp.x - q0.x) / (q1.x - q0.x);
        if (tx < 0.f) tx = 0.f;
        if (tx > 1.f) tx = 1.f;
        angle = (lo + tx * (hi - lo)) * 6.2831853f;
        targetH = minH + (1.f - (mp.y - q0.y) / (q1.y - q0.y)) * (maxH - minH);
    }

    if (inside) {
        double RR = g_shapeRadius > 1.0 ? g_shapeRadius : 1.0;
        float halfAng = (float)(g_brush / RR);
        float pxPerTurn = (q1.x - q0.x) / (hi - lo);
        float radPx = halfAng * pxPerTurn;
        if (radPx < 10.f) radPx = 10.f;
        if (radPx > 400.f) radPx = 400.f;
        dl->AddCircleFilled(mp, radPx, IM_COL32(255, 255, 255, 16), 48);
        dl->AddCircle(mp, radPx, IM_COL32(0, 0, 0, 110), 48, 2.4f);
        dl->AddCircle(mp, radPx, IM_COL32(255, 255, 255, 200), 48, 1.4f);
        if (g_brushShape == 1) {
            float szo = radPx * 0.50f;
            dl->AddRect(ImVec2(mp.x - szo, mp.y - szo), ImVec2(mp.x + szo, mp.y + szo),
                        IM_COL32(255, 255, 255, 130), 0.f, 0, 1.2f);
        } else if (g_brushShape == 2) {
            float szo = radPx * 0.55f;
            dl->AddTriangle(ImVec2(mp.x, mp.y - szo), ImVec2(mp.x - szo, mp.y + szo),
                            ImVec2(mp.x + szo, mp.y + szo), IM_COL32(255, 255, 255, 130), 1.2f);
        }
        {
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "%.0f m", g_brush);
            dl->AddText(ImVec2(mp.x + radPx + 6.f, mp.y - radPx - 4.f), IM_COL32(230, 238, 250, 220), lbl);
        }
        float bx0 = xOf((angle - halfAng) / 6.2831853f);
        float bx1 = xOf((angle + halfAng) / 6.2831853f);
        if (bx0 < q0.x) bx0 = q0.x;
        if (bx1 > q1.x) bx1 = q1.x;
        if (bx1 > bx0) {
            dl->AddRectFilled(ImVec2(bx0, q0.y), ImVec2(bx1, q1.y), IM_COL32(255, 255, 255, 22));
            dl->AddLine(ImVec2(bx0, q0.y), ImVec2(bx0, q1.y), IM_COL32(255, 255, 255, 90), 1.f);
            dl->AddLine(ImVec2(bx1, q0.y), ImVec2(bx1, q1.y), IM_COL32(255, 255, 255, 90), 1.f);
        }
        if (g_paintMode == 1) {
            float yT = yOf(targetH);
            dl->AddLine(ImVec2(bx0, yT), ImVec2(bx1, yT), IM_COL32(255, 205, 80, 235), 1.6f);
        }
    }

    if (inside && (lmb || rmb)) {
        static double lastSend = 0.0;
        double now = ImGui::GetTime();
        bool moved = ImGui::GetIO().MouseDelta.x != 0.f || ImGui::GetIO().MouseDelta.y != 0.f;
        if (now - lastSend > 0.033 && !g_selected.empty()) {
            lastSend = now;
            char cmd[160];
            if (g_paintMode == 0) {
                if (moved) {
                    float delta = g_rate * 0.033f * (rmb ? -1.f : 1.f);
                    snprintf(cmd, sizeof(cmd), "PAINTAT %s %.6f %.2f", g_selected.c_str(), angle, (double)delta);
                    SendCmd(cmd);
                    SendCmd("SHAPE " + g_selected);
                }
            } else {
                double tgt = rmb ? 0.0 : (double)targetH;
                snprintf(cmd, sizeof(cmd), "PAINTTO %s %.6f %.2f 0.45", g_selected.c_str(), angle, tgt);
                SendCmd(cmd);
                SendCmd("SHAPE " + g_selected);
            }
        }
    }

    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f && ImGui::GetIO().KeyCtrl) {
            g_zoomY *= (wheel > 0.f ? 1.25f : 0.8f);
            if (g_zoomY < 1.f) g_zoomY = 1.f;
            if (g_zoomY > 16.f) g_zoomY = 16.f;
        } else if (wheel != 0.f) {
            float oldVis = 1.f / g_zoomX;
            float mx = (mp.x - q0.x) / (q1.x - q0.x);
            if (mx < 0.f) mx = 0.f;
            if (mx > 1.f) mx = 1.f;
            float anchorT = lo + mx * oldVis;
            g_zoomX *= (wheel > 0.f ? 1.25f : 0.8f);
            if (g_zoomX < 1.f) g_zoomX = 1.f;
            if (g_zoomX > 64.f) g_zoomX = 64.f;
            float newVis = 1.f / g_zoomX;
            g_panX = (anchorT - mx * newVis) + newVis * 0.5f;
            if (g_panX < newVis * 0.5f) g_panX = newVis * 0.5f;
            if (g_panX > 1.f - newVis * 0.5f) g_panX = 1.f - newVis * 0.5f;
        }
    }
    if (hovered && ImGui::IsMouseDragging(2)) {
        float dx = ImGui::GetIO().MouseDelta.x / (q1.x - q0.x);
        g_panX -= dx * (1.f / g_zoomX);
    }

    dl->PopClipRect();
}

// ---------------- 笔刷编辑界面 ----------------
static void DrawBrushScreen(float W, float H) {
    if (ImGui::Button("< 返回", ImVec2(88, 30))) g_view = 0;
    ImGui::SameLine();
    ImGui::Text("%s   笔刷编辑", g_selected.empty() ? "(未选星球)" : g_selected.c_str());

    ImGui::Spacing();
    DrawBigHeightMap(W, H * 0.40f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    SliderWithInput("笔刷半径", &g_brush, 10.f, 20000.f, "%.0f m", "CMD brush %.0f");
    ImGui::Spacing();
    SliderWithInputInt("采样点", &g_samples, 256, 8192, "CMD samples %d");
    ImGui::Spacing();
    SliderWithInput("强度", &g_rate, 10.f, 2000.f, "%.0f m/s", "CMD rate %.0f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (GlassButton(g_paint ? "绘制中（点此关闭）" : "开启笔刷", ImVec2(190, 34), false, false, g_paint)) {
        SendCmd("CMD paint");
        g_paint = !g_paint;
    }
    ImGui::SameLine();
    if (GlassButton("撤销一笔", ImVec2(110, 34))) SendCmd("CMD undo " + g_selected);
    ImGui::SameLine();
    if (GlassButton("重做", ImVec2(80, 34))) SendCmd("CMD redo " + g_selected);

    ImGui::Spacing();
    ImGui::TextUnformatted("笔刷形状");
    {
        const char* shapeNames[3] = { "圆滑", "方头", "尖峰" };
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            if (GlassButton(shapeNames[i], ImVec2(88, 30), false, false, g_brushShape == i)) {
                g_brushShape = i;
                char cmd[48];
                snprintf(cmd, sizeof(cmd), "CMD shape %d", i);
                SendCmd(cmd);
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("绘制方式");
    {
        const char* modeNames[2] = { "画笔（沿轨迹加减）", "吸附（拉向光标）" };
        for (int i = 0; i < 2; i++) {
            if (i) ImGui::SameLine();
            if (GlassButton(modeNames[i], ImVec2(178, 30), false, false, g_paintMode == i)) g_paintMode = i;
        }
        ImGui::SameLine();
        if (GlassButton("重置缩放", ImVec2(104, 30))) { g_zoomX = 1.f; g_zoomY = 1.f; g_panX = 0.5f; }
        ImGui::SameLine();
        if (GlassButton("适应视图", ImVec2(104, 30))) { g_viewRangeInit = false; }
        ImGui::SameLine();
        if (g_confirmClear) {
            if (GlassButton("确认清除？", ImVec2(118, 30), false, true)) { SendCmd("CMD clear " + g_selected); g_confirmClear = false; }
            ImGui::SameLine();
            if (GlassButton("取消", ImVec2(64, 30))) g_confirmClear = false;
        } else {
            if (GlassButton("还原原始地形", ImVec2(118, 30))) g_confirmClear = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("导出高度图（PNG）");
    ImGui::TextUnformatted("宽");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##expw", &g_expW, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("高");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##exph", &g_expH, 0, 0);
    if (g_expW < 16) g_expW = 16;  if (g_expW > 8192) g_expW = 8192;
    if (g_expH < 16) g_expH = 16;  if (g_expH > 8192) g_expH = 8192;
    ImGui::SameLine();
    ImGui::TextUnformatted("填充色");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::ColorEdit3("##expcol", g_expColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    if (GlassButton("导出 PNG", ImVec2(110, 28))) {
        g_wantFine = true;
        SendCmd("CMD shapefine " + g_selected + " 4096");
        snprintf(g_expMsg, sizeof(g_expMsg), "正在获取高精度轮廓…");
    }
    if (g_expMsg[0]) ImGui::TextDisabled("%s", g_expMsg);
    ImGui::TextDisabled("地形以下填纯色，以上为全透明（游戏按有无像素判定地形）");


    ImGui::Spacing();
    ImGui::TextDisabled("在游戏窗口里：左键按住 = 抬高，右键按住 = 压低");
    ImGui::TextDisabled("采样点 = 笔刷曲线精度（越大越细腻）");
    ImGui::TextDisabled("Ctrl+Z 撤销 / Ctrl+Y 重做 / Ctrl+S 保存");
}

// ---------------- 左侧笔刷功能区 ----------------
static void DrawBrushPanel() {
    ImGui::Separator();
    ImGui::TextUnformatted("笔刷");

    ImGui::PushStyleColor(ImGuiCol_Button,
        g_paint ? ImVec4(0.35f, 0.65f, 1.f, 0.55f) : ImVec4(1, 1, 1, 0.08f));
    if (ImGui::Button(g_paint ? "绘制中" : "开启笔刷", ImVec2(104, 28))) {
        SendCmd("CMD paint");
        g_paint = !g_paint;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("撤销一笔", ImVec2(96, 28))) SendCmd("CMD undo " + g_selected);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 74.f);
    if (ImGui::SliderFloat("##brush", &g_brush, 10.f, 20000.f, "%.0f m")) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "CMD brush %.0f", g_brush);
        SendCmd(cmd);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(66.f);
    if (ImGui::InputFloat("##brushnum", &g_brush, 0.f, 0.f, "%.0f")) {
        if (g_brush < 10.f) g_brush = 10.f;
        if (g_brush > 20000.f) g_brush = 20000.f;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "CMD brush %.0f", g_brush);
        SendCmd(cmd);
    }

    ImGui::TextDisabled("游戏窗口：左键抬高 / 右键压低");
    ImGui::TextDisabled("点上面的高度图可在这里直接画");
}

// 改名 / 删除星球 的弹窗（半透明风格，无标题栏，按钮居中）
static void DrawPlanetDialogs() {
    // ---- 改名 ----
    if (g_renameOpen) {
        ImGui::OpenPopup("##rename");
        strncpy(g_renameBuf, g_selected.c_str(), sizeof(g_renameBuf) - 1);
        g_renameBuf[sizeof(g_renameBuf) - 1] = 0;
        g_renameOpen = false;
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.08f, 0.11f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    if (ImGui::BeginPopupModal("##rename", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const float PW = 340.f;
        ImGui::Dummy(ImVec2(PW, 0.f));
        ImGui::Spacing();
        ImGui::Text("把「%s」改名为：", g_selected.c_str());
        ImGui::Spacing();
        ImGui::SetNextItemWidth(PW - 8.f);
        ImGui::InputText("##newname", g_renameBuf, sizeof(g_renameBuf));
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (PW - total) * 0.5f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
        if (ImGui::Button("确定", ImVec2(120, 32))) {
            std::string nn(g_renameBuf);
            while (!nn.empty() && (nn.back() == ' ' || nn.back() == '\t')) nn.pop_back();
            if (!nn.empty()) {
                SendCmd("CMD rename " + g_selected + " " + nn);
                LogLine("rename " + g_selected + " -> " + nn);
                g_selected = nn;
                g_pendingRefresh = 0.6f;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 32))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // ---- 删除星球 ----
    if (g_delPlanetConfirm) {
        ImGui::OpenPopup("##delplanet");
        g_delPlanetConfirm = false;
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.08f, 0.11f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    if (ImGui::BeginPopupModal("##delplanet", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const float PW = 360.f;
        ImGui::Dummy(ImVec2(PW, 0.f));
        ImGui::Spacing();
        ImGui::Text("确定要删除「%s」吗？", g_selected.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("星球文件会从包里删掉，运行时也会移除。");
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (PW - total) * 0.5f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.3f, 0.26f, 0.55f));
        if (ImGui::Button("确定删除", ImVec2(120, 32))) {
            SendCmd("CMD delplanet " + g_selected + " live");
            LogLine("delplanet " + g_selected);
            g_pendingRefresh = 0.6f;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 32))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static void DrawLog() {
    ImGui::BeginChild("##log", ImVec2(0, 110), ImGuiChildFlags_Borders);
    int from = (int)g_log.size() > 6 ? (int)g_log.size() - 6 : 0;
    for (int i = from; i < (int)g_log.size(); i++) ImGui::TextDisabled("%s", g_log[i].c_str());
    ImGui::EndChild();
}

static void BuildUI() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    // 背景完全交给 DWM 亚克力，不再铺深色底

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    DrawTitleBar();
    ImGui::Separator();

    // 主界面 / 笔刷编辑 的过渡
    float vdt = ImGui::GetIO().DeltaTime;
    if (vdt > 0.1f) vdt = 0.1f;
    float vtarget = (g_view == 1) ? 1.f : 0.f;
    g_viewAnim += (vtarget - g_viewAnim) * (vdt * 9.f);
    if (g_viewAnim < 0.002f) g_viewAnim = 0.f;
    if (g_viewAnim > 0.998f) g_viewAnim = 1.f;
    float ve = g_viewAnim * g_viewAnim * (3.f - 2.f * g_viewAnim);
    ImVec2 contentTop = ImGui::GetCursorScreenPos();

    float h = ImGui::GetContentRegionAvail().y;
    if (g_view == 0) {
    ImGui::BeginChild("##left", ImVec2(240, h - 118), ImGuiChildFlags_None);
    DrawPlanetList();
    DrawPreview();
    DrawBrushPanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, h - 118), ImGuiChildFlags_None);
    DrawFields();
    ImGui::EndChild();

    DrawLog();
    DrawPlanetDialogs();
    }

    if (g_view == 1) {
        ImGui::SetCursorScreenPos(ImVec2(contentTop.x, contentTop.y + (1.f - ve) * 26.f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ve);
        ImGui::BeginChild("##brushview", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        DrawBrushScreen(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::End();
}

// ---------------- 主循环 ----------------
int main(int, char**) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{ sizeof(wc), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr,
                   L"PlanetEditorUI", nullptr };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"World Creater",
        WS_OVERLAPPEDWINDOW & ~WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 780,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd || !CreateDeviceD3D(g_hwnd)) { CleanupDeviceD3D(); return 1; }

    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    {
        INT pref = 2;   // DWMWCP_ROUND
        DwmSetWindowAttribute(g_hwnd, 33, &pref, sizeof(pref));
    }
    RECT rc; GetClientRect(g_hwnd, &rc);
    g_glass.Init(g_dev, g_ctx, rc.right - rc.left, rc.bottom - rc.top);
    g_glass.SetMode(GLASS_ACRYLIC);
    EnableAcrylic(g_hwnd, 0x6E18120E);   // AABBGGRR：#0E1218 @43%

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    DockLeftOfGame();         // 默认占游戏左边的半屏

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ApplyStyle();
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 19.f))
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 19.f);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    DoConnect();

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        PollBridge();
        if (g_pendingRefresh > 0.f) {
            g_pendingRefresh -= ImGui::GetIO().DeltaTime;
            if (g_pendingRefresh <= 0.f) { g_pendingRefresh = -1.f; RefreshFields(); }
        }
        // 全局热键：Ctrl+Z 撤销 / Ctrl+Y 重做 / Ctrl+S 保存
        if (g_bridge.Connected()) {
            bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { SendCmd("CMD undo " + g_selected); LogLine("hotkey: undo"); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) { SendCmd("CMD redo " + g_selected); LogLine("hotkey: redo"); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { SendCmd("CMD save"); LogLine("hotkey: save"); }
        }
        {
            static double lastShapeReq = 0.0;
            if (!g_selected.empty() && g_bridge.Connected() && ImGui::GetTime() - lastShapeReq > 1.0) {
                lastShapeReq = ImGui::GetTime();
                RequestShape();
            }
        }
        // 断线自动重连（去掉了"连接"按钮，靠这个兜底）
        {
            static double lastTry = 0.0;
            if (!g_bridge.Connected() && ImGui::GetTime() - lastTry > 2.0) {
                lastTry = ImGui::GetTime();
                if (g_bridge.Connect(g_port)) {
                    g_status = "已连接 :" + std::to_string(g_port);
                    LogLine("reconnected");
                    SendCmd("STATUS");
                    RequestPlanets();
                } else {
                    g_status = "等待游戏端 (127.0.0.1:" + std::to_string(g_port) + ")";
                }
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        BuildUI();
        ImGui::Render();

        const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);

        // 背景已经由 DWM 亚克力模糊好了，这里不再叠玻璃面板；
        // 只在最底层铺一层很薄的深色染色，保证文字对比度（在 BuildUI 里用背景 DrawList 画）

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_glass.Shutdown();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    return 0;
}
