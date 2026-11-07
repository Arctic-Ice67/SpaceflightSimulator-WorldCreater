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
#include <cstdlib>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

// ---------------- 中英切换 ----------------
#include <unordered_map>
#include <fstream>
static bool g_english = false;

struct TrPair { const char* cn; const char* en; };
static const TrPair kTr[] = {
    { "红色为地标范围 改角度实时更新", "Red arc = landmark range, live" },
    { "启用预览", "Enable preview" },
    { "预览已开 点此关闭", "Preview on - click to stop" },
    { "打开后回游戏按 M 进地图视角 当前星球每个地标的范围都会画成红弧", "Turn on, then press M in game: every landmark of this planet is drawn as a red arc" },
    { "游戏里已经画出来了", "already drawn in game" },
    { "现在游戏里没有红线", "nothing drawn in game" },
    { "游戏内预览已开（按 M 进地图视角）", "in-game preview ON (press M for map)" },
    { "游戏内预览已关", "in-game preview OFF" },
    { "游戏地图视角里已同步显示", "also shown on the game map view" },
    { "点上面的按钮 游戏地图视角里才会画", "press the button above to draw it on the game map" },
    { "＋ 新地标", "+ Landmark" },
    { "加了一个地标", "landmark added" },
    { "在当前星球末尾加一个地标 默认 0-10 度", "Append a landmark to this planet (default 0-10 deg)" },
    { "删掉后记得点保存", "save after deleting" },
    { "删除", "Delete" },
    { "删掉地标 ", "deleted landmark " },
    { "地图视角也会标红", "Also drawn on the map view" },
    { "游戏中点选", "Pick in game" }, { "停止点选", "Stop picking" }, { "标记", "Landmark" },
    { "点击游戏星球表面添加地标", "Click the planet surface in game to add a landmark" },
    { "已添加地标", "Landmark added" },
    { "数值", "Value" }, { "最小", "Min" }, { "最大", "Max" },
    { "步长", "Step" }, { "滑块范围", "Slider range" },
    { "拖滑块改值 或点笔输入精确值", "Drag slider or click the pen for exact value" },
    { "  色板", "  palette" },
    { " 个", "" },
    { " 项", "" },
    { "%s   笔刷编辑", "%s   Brush editor" },
    { "%s  （未启用）", "%s  (not enabled)" },
    { "(未选星球)", "(no planet)" },
    { "(没有匹配的字段)", "(no matching fields)" },
    { "< 返回", "< Back" },
    { "Ctrl+Z 撤销 / Ctrl+Y 重做 / Ctrl+S 保存", "Ctrl+Z undo / Ctrl+Y redo / Ctrl+S save" },
    { "← 左边选一个星球", "<- Select a planet on the left" },
    { "不透明  1", "Opaque  1" },
    { "中", "CN" },
    { "云层", "Clouds" },
    { "从包和运行时里删除当前星球", "Remove this planet from pack and runtime" },
    { "作者", "Author" },
    { "作者 / 版本 / 描述，以及备份和导出 zip", "Author / version / description, backups and zip export" },
    { "保存", "Save" },
    { "保存信息", "Save info" },
    { "保存当前为模板…", "Save current as template..." },
    { "共 %d 项", "%d items" },
    { "关闭", "Close" },
    { "关闭  -1", "Off  -1" },
    { "关闭  0", "Off  0" },
    { "切换到中文", "Switch to Chinese" },
    { "删星球", "Delete" },
    { "删除后该模块的字段会从星球上移除。", "Its fields will be removed from this planet." },
    { "删除模块", "Remove module" },
    { "刷新列表", "Refresh" },
    { "包信息", "Pack info" },
    { "卫星  1", "Moon  1" },
    { "卫星  5000", "Moon  5000" },
    { "厚大气  3", "Thick air  3" },
    { "取消", "Cancel" },
    { "后处理", "Post processing" },
    { "启用", "Enable" },
    { "吸附（拉向光标）", "Pull toward cursor" },
    { "圆滑", "Smooth" },
    { "土卫六  500", "Titan  500" },
    { "土星  3800000", "Saturn  3800000" },
    { "土星  6600000", "Saturn  6600000" },
    { "在游戏窗口里：左键按住 = 抬高，右键按住 = 压低", "In game window: hold left = raise, right = lower" },
    { "游戏窗口里也能刷：左键抬高 右键压低", "also works in game: left up, right down" },
    { "地图地标", "Landmarks" },
    { "地形", "Terrain" },
    { "地形以下填纯色，以上为全透明（游戏按有无像素判定地形）", "Opaque below terrain, transparent above" },
    { "地形公式（", "Terrain formula (" },
    { "地形公式（当前难度）", "Terrain formula (current difficulty)" },
    { "地标 ", "Landmark " },
    { "地球  0.1", "Earth  0.1" },
    { "地球  10", "Earth  10" },
    { "地球  1200", "Earth  1200" },
    { "地球  2", "Earth  2" },
    { "地球  25000", "Earth  25000" },
    { "地球  36000", "Earth  36000" },
    { "地球  40845", "Earth  40845" },
    { "地球  5000", "Earth  5000" },
    { "基础", "Basics" },
    { "填充色", "Fill color" },
    { "备份", "Backups" },
    { "大云层  80000", "Large clouds  80000" },
    { "大星球  10000", "Large planet  10000" },
    { "大气物理", "Atmosphere physics" },
    { "大气视觉", "Atmosphere visuals" },
    { "太阳/土卫六  13", "Sun/Titan  13" },
    { "宽", "Width" },
    { "宽环  9000000", "Wide rings  9000000" },
    { "导出 PNG", "Export PNG" },
    { "导出 zip", "Export zip" },
    { "导出失败（写文件出错）", "Export failed (write error)" },
    { "导出高度图（PNG）", "Export heightmap (PNG)" },
    { "小云层  20000", "Small clouds  20000" },
    { "小卫星  2000", "Small moon  2000" },
    { "小行星  0.5", "Asteroid  0.5" },
    { "小行星  1000", "Asteroid  1000" },
    { "尖峰", "Peak" },
    { "岩石行星", "Rocky" },
    { "巨行星  15", "Gas giant  15" },
    { "巨行星  80000", "Gas giant  80000" },
    { "已导出 %s", "Exported %s" },
    { "已添加地标", "Landmark added" },
    { "已连接 :", "Connected :" },
    { "应用", "Apply" },
    { "开启笔刷", "Enable brush" },
    { "强度", "Rate" },
    { "当前难度", "Current" },
    { "恢复最近", "Restore latest" },
    { "慢  1", "Slow  1" },
    { "打快照", "Snapshot" },
    { "把「%s」改名为：", "Rename %s to:" },
    { "把该星球的引力范围(SOI)撑到覆盖所有子天体轨道，适合双星质心", "Expand SOI to cover all child orbits" },
    { "描述", "Description" },
    { "撤销一笔", "Undo" },
    { "改名", "Rename" },
    { "方头", "Flat" },
    { "星球文件会从包里删掉，运行时也会移除。", "Planet file and runtime object will be removed." },
    { "普通  1", "Normal  1" },
    { "木星  0.65", "Jupiter  0.65" },
    { "未连接", "Disconnected" },
    { "未连接（先在游戏里进世界）", "Not connected (enter a world in game first)" },
    { "正在等待游戏里点击…", "Waiting for a click in game..." },
    { "正在获取高精度轮廓…", "Fetching high-res profile..." },
    { "气态巨行星", "Gas giant" },
    { "没有地形数据", "No terrain data" },
    { "海洋", "Oceans" },
    { "游戏中点选", "Pick in game" },
    { "游戏窗口：左键抬高 / 右键压低", "Game window: left raise / right lower" },
    { "滚轮滚动", "Scroll" },
    { "滚轮滚动 · 共 %d 项", "Scroll for more - %d items" },
    { "滚轮缩放 / Ctrl+滚轮纵向 / 中键平移 / 左键画 / 右键削", "Wheel zoom / Ctrl+wheel vertical / middle drag pan / left raise / right lower" },
    { "火星  15000", "Mars  15000" },
    { "火星  3", "Mars  3" },
    { "火星  9", "Mars  9" },
    { "点一下恢复这个快照", "Click to restore this snapshot" },
    { "点上面的高度图可在这里直接画", "Draw directly on the height map above" },
    { "点击游戏星球表面添加地标", "Click the planet surface in game to add a landmark" },
    { "点击进入笔刷编辑（高度图里可以直接画）", "Open brush editor (draw directly on the height map)" },
    { "点色块打开调色盘", "Click swatch to open color picker" },
    { "版本", "Version" },
    { "画笔（沿轨迹加减）", "Brush (raise/lower)" },
    { "确定", "OK" },
    { "确定删除", "Delete" },
    { "确定要删除「%s」吗？", "Delete %s?" },
    { "确定要删除「%s」模块吗？", "Remove module %s?" },
    { "确认清除？", "Confirm clear?" },
    { "笔刷", "Brush" },
    { "笔刷半径", "Brush radius" },
    { "笔刷形状", "Shape" },
    { "等待游戏端 (127.0.0.1:", "Waiting for game (127.0.0.1:" },
    { "类地行星", "Terrestrial" },
    { "紧凑  2000000", "Compact  2000000" },
    { "绘制中", "Painting" },
    { "绘制中（点此关闭）", "Painting (click to stop)" },
    { "绘制方式", "Mode" },
    { "色板", "Palette" },
    { "薄云  12000", "Thin clouds  12000" },
    { "行星环", "Rings" },
    { "质心", "Barycenter" },
    { "贴地  0", "Ground  0" },
    { "轨道", "Orbit" },
    { "过滤字段…", "Filter fields..." },
    { "还原原始地形", "Restore original terrain" },
    { "还没有快照（每次保存星球会自动留上一版）", "No snapshots yet (each save keeps the previous version)" },
    { "连接", "Connect" },
    { "连接已断开", "Connection lost" },
    { "适应视图", "Fit view" },
    { "选中星球后显示预览", "Select a planet to see preview" },
    { "读取地形…", "Loading terrain…" },
    { "极坐标编辑", "Polar edit" },
    { "坐标预览", "Coords" },
    { "坐标预览 开", "Coords on" },
    { "复位视图", "Reset view" },
    { "滚轮放大 中键平移", "wheel zoom, middle drag" },
    { "坐标预览已开", "coords overlay on" },
    { "坐标预览已关", "coords overlay off" },
    { "左侧已经画出来了", "drawn on the left" },
    { "左侧只画地形", "terrain only on the left" },
    { "左侧预览滚轮能放大到 16 倍 拖红点就改角度", "zoom the left view up to 16x, drag the red dots to change angles" },
    { "打开后左侧星球预览会画出每个地标的范围 端点能直接拖", "Turn on: the left planet view draws every landmark range, drag the red dots" },
    { "选中星球后这里显示全周地形高度", "Full terrain profile appears after selecting a planet" },
    { "采样点", "Samples" },
    { "采样点 = 笔刷曲线精度（越大越细腻）", "Samples = brush curve resolution" },
    { "重做", "Redo" },
    { "重命名当前星球（会同步其它星球的 orbit.parent 和文件名）", "Rename planet (also updates orbit.parent and file name)" },
    { "重置缩放", "Reset zoom" },
    { "重读", "Reload" },
    { "重读包", "Reload pack" },
    { "静止  0", "Static  0" },
    { "预设", "Presets" },
    { "高", "Height" },
    { "高度 (m)", "Height (m)" },
    { "默认  1000", "Default  1000" },
    { "默认  4", "Default  4" },
    { "默认  5000", "Default  5000" },
    { "）", ")" },
    { "＋ 添加星球", "+ Add planet" },
};
static std::unordered_map<std::string, const char*>& TrMap() {
    static std::unordered_map<std::string, const char*> m;
    static bool built = false;
    if (!built) {
        for (int i = 0; i < (int)(sizeof(kTr) / sizeof(kTr[0])); i++) m[kTr[i].cn] = kTr[i].en;
        built = true;
    }
    return m;
}
// 中文字符串在英文模式下换成英文，没收录就原样返回
static const char* TR(const char* cn) {
    if (!g_english || !cn) return cn;
    auto& m = TrMap();
    auto it = m.find(cn);
    return it == m.end() ? cn : it->second;
}
static std::string LangFile() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string f(path);
    size_t p = f.find_last_of("\\/");
    return (p == std::string::npos ? std::string("") : f.substr(0, p + 1)) + "WorldCreater_lang.txt";
}
static void LoadLang() {
    std::ifstream f(LangFile());
    char c = 0;
    if (f) { f >> c; }
    g_english = (c == 'e' || c == 'E');
}
// 按钮宽度按文字算，英文比中文长也不会被截
static float TextBtnW(const char* t, float minW) {
    float w = ImGui::CalcTextSize(t).x + 20.f;
    return w > minW ? w : minW;
}
static void SaveLang() {
    std::ofstream f(LangFile());
    if (f) f << (g_english ? 'e' : 'c');
}
// 新建星球模板名跟着语言走
static std::string TplLabel(const std::string& key, const std::string& cn) {
    if (!g_english) return cn;
    static const char* keys[] = { "earth", "rocky", "gas", "ice", "asteroid", "bary" };
    static const char* ens[] = { "Terrestrial", "Rocky", "Gas giant", "Icy moon", "Asteroid", "Barycenter" };
    for (int i = 0; i < 6; i++) if (key == keys[i]) return ens[i];
    return cn;
}

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

static unsigned int g_accentTint = 0x6E18120E;   // AABBGGRR：#0E1218 @43%

static bool EnableAcrylic(HWND hwnd, unsigned int tint_abgr) {
    auto fn = (PFN_SetWindowCompositionAttribute)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!fn) return false;
    g_accentTint = tint_abgr;
    ACCENT_POLICY policy{ 4 /*ACCENT_ENABLE_ACRYLICBLURBEHIND*/, 0, tint_abgr, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data{ 19 /*WCA_ACCENT_POLICY*/, &policy, sizeof(policy) };
    return fn(hwnd, &data) != FALSE;
}

/// <summary>拖动/缩放窗口时把模糊临时换成纯色，窗口跟随鼠标才不拖影。</summary>
static bool g_movingLook = false;   // 记住当前状态，没变就不重复调 DWM（也防止漏还原）
static void SetMovingLook(bool moving) {
    if (g_movingLook == moving) return;
    auto fn = (PFN_SetWindowCompositionAttribute)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!fn || !g_hwnd) return;
    ACCENT_POLICY policy{ moving ? 1 : 4, 0, g_accentTint, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data{ 19 /*WCA_ACCENT_POLICY*/, &policy, sizeof(policy) };
    if (fn(g_hwnd, &data)) g_movingLook = moving;
}

// ---------------- 与游戏通信 ----------------
static BridgeClient g_bridge;
static int g_port = 41729;

// ---------------- 退出/崩溃日志（窗口莫名消失时看这个文件） ----------------
#include <cstdarg>
static FILE* g_dbg = nullptr;
static void DbgOpen() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string f(path);
    size_t p = f.find_last_of("\\/");
    f = (p == std::string::npos ? std::string("") : f.substr(0, p + 1)) + "WorldCreater_debug.log";
    g_dbg = fopen(f.c_str(), "w");
}
static void Dbg(const char* fmt, ...) {
    if (!g_dbg) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_dbg, fmt, ap);
    va_end(ap);
    fputc('\n', g_dbg);
    fflush(g_dbg);
}
static LONG WINAPI DbgCrash(EXCEPTION_POINTERS* ep) {
    Dbg("CRASH code=0x%08X addr=%p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

struct PlanetInfo { std::string code; int radius = 0; std::string gravity; float selAnim = 0.f; };
struct FieldRow { int kind = 0; std::string path; std::string value; char buf[2048]; bool dirty = false; };

static std::vector<PlanetInfo> g_planets;
static std::vector<FieldRow> g_fields;

// 星球形状预览（由 C# 端 SHAPE 命令推送）
static std::vector<float> g_shape;      // 一圈地形高度（米），只放"已收完"的整圈
static double g_shapeRadius = 0.0;
static int g_shapeCol[3] = { 150, 150, 150 };
static double g_lastShape = 0.0;
static std::vector<float> g_shapeHi;   // 导出用的高精度轮廓
static bool g_wantFine = false;      // 正在等高精度数据
static bool g_readingFine = false;

// SHAPE 是逐行推过来的，4096 行会跨好几帧。先收进缓冲，收满再一次性替换，
// 否则中间那几帧会画出半圈轮廓（看起来就是边缘在抽动）。
static std::vector<float> g_shapeIn;
static double g_radiusIn = 0.0;
static int g_colIn[3] = { 150, 150, 150 };
static float g_hRef = 0.f;              // 画轮廓用的参考起伏，带迟滞，避免采样网格每帧都变

static bool ExportHeightmapPng(const std::vector<float>& src);

// 界面：0=主界面 1=极坐标编辑（带过渡动画）
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
static std::string g_status = TR("未连接");
static char g_filter[64] = "";
static std::string g_modSel;          // 当前查看的模块（空 = 全部）
static bool g_paint = false;
static bool g_preview = false;          // 左侧星球预览里的坐标（地标范围）叠加开关
// 星球预览的视图状态（缩放 / 平移 / 正在拖的地标）
static float g_pvZoom = 1.f;      // 1..16 倍
static float g_pvPanX = 0.f;      // 相对视图中心的像素偏移
static float g_pvPanY = 0.f;
static int   g_pvDragLm = -1;     // 正在拖的地标序号，-1 = 没在拖
static int   g_pvDragEnd = 0;     // 0 = 起始角，1 = 结束角
static double g_pvLastSet = 0.0;

static void ResetPreviewView() { g_pvZoom = 1.f; g_pvPanX = 0.f; g_pvPanY = 0.f; }
static float g_brush = 2000.f;
static std::string g_ratePlanet;        // 已经按这颗星球自动配过强度
static bool  g_rateTouched = false;     // 用户手动改过强度就不再自动覆盖
static float AutoRateFor(double radius) {
    double want = radius * 0.01;
    if (want < 300.0) want = 300.0;
    if (want > 5000.0) want = 5000.0;
    return (float)want;
}
static int g_pending = 0;               // 还需读取的行数
static int g_pending_kind = 0;          // 1=planets 2=fields
static double g_last_poll = 0.0;
static float g_fieldsScroll = 0.f;      // 字段区滚动位置，刷新后还原
static bool  g_restoreScroll = false;   // 有待还原的滚动位置

// 模板 / 包信息 / 备份
static std::vector<std::string> g_templates;   // 每行 key \t 名字 \t 来源
static std::vector<std::string> g_textures;    // 游戏里已加载的贴图名
static std::vector<std::string> g_backups;     // 备份时间戳，新的在前
static char g_authorBuf[128] = "";
static char g_verBuf[64] = "";
static char g_descBuf[256] = "";
static bool g_packInfoOpen = false;

static void LogLine(const std::string& s) {
    g_log.push_back(s);
    if (g_log.size() > 200) g_log.erase(g_log.begin(), g_log.begin() + 100);
}

static void SendCmd(const std::string& s) {
    Dbg("> %s", s.c_str());
    g_bridge.Send(s);
}

// 笔刷开关：显式告诉游戏要开还是要关，避免和游戏里的 P 键互相打架
static double g_lastPaintCmd = -10.0;
static void SetPaint(bool on) {
    g_paint = on;
    g_lastPaintCmd = ImGui::GetTime();
    SendCmd(std::string("CMD paint ") + (on ? "on" : "off"));
}

static void RequestPlanets() { g_pending_kind = 1; g_pending = -1; SendCmd("LIST"); }
static void RequestFields(const std::string& code) {
    // 同一颗星球刷新（改模块/删模块/颜色）时把滚动位置留住
    if (code == g_selected) g_restoreScroll = true;
    else { g_restoreScroll = false; g_fieldsScroll = 0.f; }
    g_fields.clear();
    g_pending_kind = 2; g_pending = -1;
    SendCmd("FIELDS " + code);
}

static void DoConnect() {
    if (g_bridge.Connect(g_port)) {
        g_status = TR("已连接 :") + std::to_string(g_port);
        LogLine("connected to 127.0.0.1:" + std::to_string(g_port));
        SendCmd("STATUS");
        SendCmd("CMD templates");
        SendCmd("CMD textures");
        RequestPlanets();
    } else {
        g_status = TR("未连接（先在游戏里进世界）");
        LogLine(g_bridge.LastError());
    }
}

static void HandleLine(const std::string& line) {
    // 多行响应
    // 正在收 SHAPE/FIELDS 这类多行数据时，用户的其它操作也会回 OK/ERR；
    // 这些是控制行，不能当数据行吞掉（否则既污染数据，又错过地标增删后的刷新）
    bool ctrl = line.rfind("OK ", 0) == 0 || line.rfind("ERR ", 0) == 0 || line == "PONG";
    if (g_pending > 0 && !ctrl) {
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
            if (g_readingFine) g_shapeHi.push_back(hv); else g_shapeIn.push_back(hv);
        } else if (g_pending_kind == 4) {
            // 模板行：key \t 名字 \t 来源
            g_templates.push_back(line);
        } else if (g_pending_kind == 5) {
            // 元数据行：author=xxx
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "author") { strncpy(g_authorBuf, v.c_str(), sizeof(g_authorBuf) - 1); g_authorBuf[sizeof(g_authorBuf) - 1] = 0; }
                else if (k == "version") { strncpy(g_verBuf, v.c_str(), sizeof(g_verBuf) - 1); g_verBuf[sizeof(g_verBuf) - 1] = 0; }
                else if (k == "description") { strncpy(g_descBuf, v.c_str(), sizeof(g_descBuf) - 1); g_descBuf[sizeof(g_descBuf) - 1] = 0; }
            }
        } else if (g_pending_kind == 6) {
            g_backups.push_back(line);
        } else if (g_pending_kind == 7) {
            g_textures.push_back(line);
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
                LogLine("planet list: " + std::to_string(g_planets.size()) + TR(" 个"));
            else if (g_pending_kind == 2)
                LogLine("fields: " + std::to_string(g_fields.size()) + TR(" 项"));
            else if (g_pending_kind == 3 && g_readingFine) {
                g_readingFine = false;
                g_wantFine = false;
                ExportHeightmapPng(g_shapeHi);
            }
            else if (g_pending_kind == 3 && g_shapeIn.size() >= 8) {
                // 整圈到齐，原子替换（含半径与配色，避免半径先变、点还没到）
                g_shape.swap(g_shapeIn);
                g_shapeRadius = g_radiusIn;
                g_shapeCol[0] = g_colIn[0]; g_shapeCol[1] = g_colIn[1]; g_shapeCol[2] = g_colIn[2];
                g_lastShape = ImGui::GetTime();
                g_hRef = 0.f;      // 换了一颗星球/换了一圈数据，采样尺度重新起算
            }
            g_pending_kind = 0;
        }
        return;
    }

    if (line.rfind("PONG", 0) == 0) { LogLine(line); return; }
    if (line.rfind("STATUS", 0) == 0) {
        LogLine(line);
        size_t k = line.find(" paint=");
        if (k == std::string::npos) k = line.find("paint=");
        // 游戏里的 P 键也能开关笔刷，这里只做兜底同步；刚点过按钮的 1.5 秒内以面板为准
        if (k != std::string::npos && ImGui::GetTime() - g_lastPaintCmd > 1.5) {
            const char* v = line.c_str() + line.find('=', k) + 1;
            g_paint = (atoi(v) != 0);
        }
        return;
    }
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
                // 收进缓冲，先别动 g_shape：整圈到齐了再换，画面就不会抖
                g_shapeIn.clear();
                g_shapeIn.reserve(n > 0 ? n : 0);
                g_radiusIn = radius;
                g_colIn[0] = r; g_colIn[1] = gg; g_colIn[2] = b;
                g_readingFine = false;
            }
            // 默认强度跟着星球半径走，否则在几百公里半径的星球上刷一下根本看不出来
            if (!g_rateTouched && std::string(code) != g_ratePlanet) {
                g_ratePlanet = code;
                g_rate = AutoRateFor(radius);
                char rc[48];
                snprintf(rc, sizeof(rc), "CMD rate %.0f", g_rate);
                SendCmd(rc);
            }
            g_pending = n;
            g_pending_kind = 3;
        }
        return;
    }
    if (line.rfind("TEMPLATES ", 0) == 0) {
        g_templates.clear();
        g_pending = atoi(line.c_str() + 10);
        g_pending_kind = 4;
        if (g_pending == 0) g_pending_kind = 0;
        return;
    }
    if (line.rfind("META ", 0) == 0) {
        g_pending = atoi(line.c_str() + 5);
        g_pending_kind = 5;
        if (g_pending == 0) g_pending_kind = 0;
        return;
    }
    if (line.rfind("TEXTURES ", 0) == 0) {
        g_textures.clear();
        g_pending = atoi(line.c_str() + 9);
        g_pending_kind = 7;
        if (g_pending == 0) g_pending_kind = 0;
        return;
    }
    if (line.rfind("BACKUPS ", 0) == 0) {
        g_backups.clear();
        g_pending = atoi(line.c_str() + 8);
        g_pending_kind = 6;
        if (g_pending == 0) g_pending_kind = 0;
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
    if (line.rfind("OK dellandmark", 0) == 0 || line.rfind("OK addlandmark", 0) == 0) {
        // 增删成功后立刻重拉字段表：模组已经让字段缓存失效，这里再读就是最新的
        g_pendingRefresh = 0.05f;
        LogLine(line);
        return;
    }
    if (line.rfind("OK", 0) == 0 || line.rfind("ERR", 0) == 0) {
        // 笔刷回执写进日志，方便核对「点的位置」和「真正刷到的角度」
        if (line.rfind("OK paint", 0) == 0 || line.rfind("ERR", 0) == 0) Dbg("< %s", line.c_str());
        LogLine(line);
        return;
    }
    LogLine(line);
}

static void PollBridge() {
    if (!g_bridge.Connected()) return;
    std::vector<std::string> lines = g_bridge.Poll();
    for (auto& l : lines) HandleLine(l);
    if (!g_bridge.Connected()) { g_status = TR("连接已断开"); LogLine("disconnected"); }
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
    case WM_ENTERSIZEMOVE:
        // 开始拖窗：亚克力模糊每帧重算，换成纯色后跟随鼠标才不拖影
        SetMovingLook(true);
        break;
    case WM_EXITSIZEMOVE:
        SetMovingLook(false);
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
// ---------------- 标题栏拖动 ----------------
// 以前是 ReleaseCapture + SendMessage(WM_NCLBUTTONDOWN, HTCAPTION) 交给系统拖：
// 系统进的是 modal 拖动循环，左键松开那一下被它吃掉，ImGui 以为鼠标还按着，
// 拖动条一直 active —— 结果点一下上方栏，整个窗口就黏在鼠标上，点哪儿都在拖窗，
// 所有按钮都按不动（缩小、放大后都一样，只能再点一下才脱开）。
// 现在自己搬窗口：按下记起点，每帧按光标位移 SetWindowPos，松开即结束。
static bool  g_titleDrag = false;
static POINT g_titleDragFrom{};
static RECT  g_titleDragRect{};

static void BeginTitleDrag() {
    GetCursorPos(&g_titleDragFrom);
    if (g_customMax) {
        // 最大化时拖标题栏 = 先还原，光标按比例落回标题栏，跟系统窗口的手感一致
        RECT wr{}; GetWindowRect(g_hwnd, &wr);
        float rel = (wr.right > wr.left)
            ? (float)(g_titleDragFrom.x - wr.left) / (float)(wr.right - wr.left) : 0.5f;
        if (rel < 0.f) rel = 0.f;
        if (rel > 1.f) rel = 1.f;
        ToggleMaximize();
        GetWindowRect(g_hwnd, &wr);
        int nx = g_titleDragFrom.x - (int)(rel * (float)(wr.right - wr.left));
        int ny = g_titleDragFrom.y - 16;
        SetWindowPos(g_hwnd, nullptr, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetCursorPos(&g_titleDragFrom);
    }
    GetWindowRect(g_hwnd, &g_titleDragRect);
    g_titleDrag = true;
    SetMovingLook(true);      // 拖动时亚克力换纯色，避免糊成一片
}

static void UpdateTitleDrag() {
    if (!g_titleDrag) return;
    // 用物理键状态收尾：窗口拖到屏幕外或已经失焦时也能正常结束
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        g_titleDrag = false;
        SetMovingLook(false);
        return;
    }
    POINT cur{};
    GetCursorPos(&cur);
    int dx = cur.x - g_titleDragFrom.x;
    int dy = cur.y - g_titleDragFrom.y;
    if (dx == 0 && dy == 0) return;
    SetWindowPos(g_hwnd, nullptr, g_titleDragRect.left + dx, g_titleDragRect.top + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    ImGui::BeginChild("##title", ImVec2(0, 32), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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

    // 拖动（避开按钮区）：按下只记录起点，真正的移动在 UpdateTitleDrag 里做
    ImGui::SetCursorPos(ImVec2(0, 0));
    float dragW = bx - 4.f;
    if (dragW < 40.f) dragW = 40.f;
    ImGui::InvisibleButton("##drag", ImVec2(dragW, 32));
    bool dragHovered = ImGui::IsItemHovered();
    if (dragHovered && ImGui::IsMouseDoubleClicked(0)) {
        ToggleMaximize();
    } else if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        BeginTitleDrag();
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
    if (src.size() < 8) { snprintf(g_expMsg, sizeof(g_expMsg), TR("没有地形数据")); return false; }

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
        snprintf(g_expMsg, sizeof(g_expMsg), TR("已导出 %s"), name);
        LogLine(std::string("export ") + name);
        return true;
    }
    snprintf(g_expMsg, sizeof(g_expMsg), TR("导出失败（写文件出错）"));
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
    // 模板由 C# 端给，这里只负责显示
    static const char* fallback[3][3] = {
        { "earth", TR("类地行星"), "Earth" },
        { "rocky", TR("岩石行星"), "Mars" },
        { "gas", TR("气态巨行星"), "Jupiter" }
    };
    struct TplRow { std::string key, name; };
    std::vector<TplRow> rows;
    for (auto& t : g_templates) {
        size_t a = t.find('\t');
        size_t b = t.find('\t', a == std::string::npos ? 0 : a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        rows.push_back({ t.substr(0, a), t.substr(a + 1, b - a - 1) });
    }
    if (rows.empty()) {
        for (int i = 0; i < 3; i++) rows.push_back({ fallback[i][0], fallback[i][1] });
    }

    if (g_npAnim <= 0.003f) return;

    const float rowH = 28.f;
    // 模板多了就滚动，最多占屏幕 55%
    float listH = (float)rows.size() * rowH;
    float maxList = ImGui::GetIO().DisplaySize.y * 0.55f;
    if (listH > maxList) listH = maxList;
    const float h = listH + 18.f;
    // 宽度按最长的那一项算，别把中文截掉
    float menuW = g_npWidth;
    for (int i = 0; i < (int)rows.size(); i++) {
        float tw = ImGui::CalcTextSize(rows[i].name.c_str()).x + 40.f;
        if (tw > menuW) menuW = tw;
    }
    {
        float tw = ImGui::CalcTextSize(TR("保存当前为模板…")).x + 40.f;
        if (tw > menuW) menuW = tw;
    }
    if (menuW > 360.f) menuW = 360.f;
    float menuX = g_npPos.x;
    float vw = ImGui::GetIO().DisplaySize.x;
    if (menuX + menuW > vw - 8.f) menuX = vw - menuW - 8.f;
    if (menuX < 8.f) menuX = 8.f;
    ImGui::SetNextWindowPos(ImVec2(menuX, g_npPos.y + (1.f - g_npAnim) * -8.f));
    ImGui::SetNextWindowSize(ImVec2(menuW, h));
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
    ImGui::BeginChild("##tpllist", ImVec2(menuW - 12.f, listH), ImGuiChildFlags_None);
    for (int i = 0; i < (int)rows.size(); i++) {
        std::string tplName = TplLabel(rows[i].key, rows[i].name);
        if (GlassButton(tplName.c_str(), ImVec2(menuW - 24.f, rowH - 2.f), true)) {
            SendCmd(std::string("CMD newplanet ") + rows[i].key + " live");
            LogLine(std::string("newplanet ") + rows[i].key);
            g_npOpen = false;
        }
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

static void DrawPlanetList() {
    // ---- 固定头部：不随列表滚动 ----
    if (GlassButton(TR("刷新列表"), ImVec2(96, 26))) RequestPlanets();
    ImGui::SameLine();
    ImGui::TextDisabled("%d", (int)g_planets.size());
    ImGui::SameLine();
    {
        float bw = ImGui::GetContentRegionAvail().x;
        if (GlassButton(TR("＋ 添加星球"), ImVec2(bw, 26))) {
            g_npOpen = !g_npOpen;
            if (g_npOpen) SendCmd("CMD templates");
        }
        g_npOverBtn = ImGui::IsItemHovered();
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        g_npPos = ImVec2(rmin.x, rmax.y + 4.f);
        g_npWidth = bw;
    }
    // 包级操作：包信息（备份和导出都在里面）
    {
        float full = ImGui::GetContentRegionAvail().x;
        float infoW = full - 46.f;
        if (infoW < 80.f) infoW = 80.f;
        if (GlassButton(TR("包信息"), ImVec2(infoW, 24))) g_packInfoOpen = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("作者 / 版本 / 描述，以及备份和导出 zip"));
        ImGui::SameLine();
        // 点一下切中/英
        const char* langLabel = g_english ? "中" : "EN";
        if (GlassButton(langLabel, ImVec2(40, 24))) {
            g_english = !g_english; SaveLang();
            if (g_bridge.Connected()) g_status = std::string(TR("已连接 :")) + std::to_string(g_port);
            else g_status = std::string(TR("等待游戏端 (127.0.0.1:")) + std::to_string(g_port) + ")";
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_english ? "切换到中文" : "Switch to English");
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

    // 列表高度 = 本栏剩余 - 预览/笔刷固定占位。窗口被压矮时不能让它算成负数塌成一条，
    // 给个下限，超出的部分交给外层 child 的滚动条。
    {
        float availH = ImGui::GetContentRegionAvail().y;
        float listH = availH - 338.f;
        if (listH < 150.f) listH = 150.f;
        ImGui::BeginChild("##planetlist", ImVec2(240, listH), ImGuiChildFlags_Borders);
    }

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

    // 所有行都是绝对坐标画完的；这里把光标放到真实内容末尾即可
    // 原来 Dummy(n * step) 会在最后一行之后再叠加一整份高度，导致能滚到空白区
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + n * step));
    ImGui::Dummy(ImVec2(1, 1));

    DrawGlassScrollbar();

    ImGui::EndChild();
    DrawNewPlanetMenu();
}

// 星球数据模块（和 FieldTree 的 key 前缀一一对应）
static const int kModuleCount = 10;
static const char* kModules[10] = { "basics", "atmo", "atmoVis", "terrain", "water", "rings", "clouds", "post", "orbit", "landmarks" };
static const char* kModuleLong[10] = { "基础", "大气物理", "大气视觉", "地形", "海洋", "行星环", "云层", "后处理", "轨道", "地图地标" };
static bool g_modOpen[10] = { true, true, true, true, false, false, false, false, false, true };
static float g_modAnim[10] = { 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };   // 折叠动画进度
static int g_delModPending = -1;
static const bool  kModuleRemovable[10] = { false, true, true, false, true, true, true, true, false, false };

// 地标字段是 landmarks[0].name 这种，前缀不带点
static std::string ModulePrefix(const char* mod) {
    if (std::string(mod) == "landmarks") return "landmarks";
    return std::string(mod) + ".";
}

static int CountModule(const char* mod) {
    int n = 0;
    std::string prefix = ModulePrefix(mod);
    for (auto& f : g_fields) if (f.path.rfind(prefix, 0) == 0) n++;
    return n;
}

static void RefreshFields() {
    if (g_selected.empty()) return;
    g_restoreScroll = true;
    RequestFields(g_selected);
    RequestShape();
}
// ---------------- 字段中文名（只影响显示，发给游戏的仍是原路径） ----------------
struct CnName { const char* en; const char* cn; };
static const CnName kCnNames[] = {
    // 基础
    { "name", "名称" }, { "radius", "半径" }, { "gravity", "重力" },
    { "timewarpHeight", "时间跃升高度" }, { "velocityArrowsHeight", "速度箭头高度" },
    { "mapColor", "地图颜色" }, { "significant", "主要天体" }, { "rotateCamera", "相机随天体旋转" },
    { "mapResolution", "地图分辨率" }, { "loadDistance", "加载距离" },
    // 地形
    { "collider", "碰撞体" }, { "verticeSize", "顶点大小" },
    { "terrainFormula", "地形公式" }, { "terrainFormulaDifficulties", "地形公式（按难度）" },
    { "textureFormula", "贴图公式" }, { "flatZones", "平坦区域" },
    { "flatZonesDifficulties", "平坦区域（按难度）" }, { "rocks", "岩石" },
    { "rockType", "岩石类型" }, { "rockDensity", "岩石密度" }, { "powerCurve", "数量曲线" },
    { "maxAngle", "最大角度" }, { "minSize", "最小尺寸" }, { "maxSize", "最大尺寸" },
    { "TERRAIN_TEXTURE_DATA", "地表纹理" },
    { "planetTexture", "行星贴图" }, { "planetTextureCutout", "贴图透明度门槛" },
    { "planetTextureRotation", "贴图旋转" }, { "planetTextureDontDistort", "贴图不变形" },
    { "surfaceTexture_A", "表面纹理 A" }, { "surfaceTextureSize_A", "表面纹理 A 尺寸" },
    { "surfaceTexture_B", "表面纹理 B" }, { "surfaceTextureSize_B", "表面纹理 B 尺寸" },
    { "terrainTexture_C", "地形纹理 C" }, { "terrainTextureSize_C", "地形纹理 C 尺寸" },
    { "surfaceLayerSize", "表面层厚度" }, { "minFade", "淡出起点" }, { "maxFade", "淡出终点" },
    { "shadowIntensity", "阴影强度" }, { "shadowHeight", "阴影高度" },
    // 海洋
    { "oceanMaskTexture", "海洋遮罩" }, { "lowerTerrain", "压低地形" }, { "oceanDepth", "海洋深度" },
    { "sand", "沙地颜色" }, { "floor", "海底颜色" }, { "shallow", "浅水颜色" }, { "deep", "深水颜色" },
    { "maskGradient_Water", "水体遮罩渐变" }, { "maskGradient_Terrain", "地形遮罩渐变" },
    { "waterGradientWidthMultiplier", "水体渐变宽度" }, { "sandGradientWidthMultiplier", "沙地渐变宽度" },
    { "floorGradientWidthMultiplier", "海底渐变宽度" }, { "shoreNoiseSize", "岸线噪声尺寸" },
    { "sandNoiseSize", "沙地噪声尺寸" }, { "wavesSize", "波浪尺寸" },
    { "opacity_Surface", "水面不透明度" }, { "opacity_Far", "远处不透明度" },
    { "opacity_FullDarkness", "全黑不透明度" }, { "surfaceVisibilityDistance", "水面可见距离" },
    { "fullDarknessDepth", "全黑深度" }, { "fullDarknessVisibilityDistance", "全黑可见距离" },
    { "must", "必须值" }, { "cannot", "禁止值" }, { "global", "全局值" },
    // 大气 / 云 / 雾
    { "height", "高度" }, { "density", "表面密度" }, { "curve", "衰减曲线" },
    { "parachuteMultiplier", "降落伞倍率" }, { "upperAtmosphere", "高层大气比例" },
    { "shockwaveIntensity", "激波强度" }, { "minHeatingVelocityMultiplier", "最小加热速度倍率" },
    { "hasOxygen", "含氧" }, { "heightDifficultyScale", "高度缩放（按难度）" },
    { "curveScale", "曲线缩放（按难度）" },
    { "GRADIENT", "渐变" }, { "CLOUDS", "云层" }, { "FOG", "雾" },
    { "texture", "贴图" }, { "startHeight", "起始高度" }, { "width", "宽度" },
    { "alpha", "不透明度" }, { "velocity", "移动速度" }, { "positionZ", "前后位置" },
    { "distance", "距离" }, { "color", "颜色" }, { "keys", "关键帧" },
    { "cloudsTexture", "云层贴图" }, { "cloudTextureCutout", "云层透明度门槛" },
    { "fadeZoneHeight", "淡出高度" }, { "sharpenAlpha", "锐化透明" },
    // 行星环
    { "ringsTexture", "行星环贴图" }, { "startRadius", "内半径" }, { "endRadius", "外半径" },
    // 后处理
    { "starIntensity", "星空亮度" }, { "hueShift", "色相偏移" }, { "saturation", "饱和度" },
    { "contrast", "对比度" }, { "red", "红" }, { "green", "绿" }, { "blue", "蓝" },
    // 轨道
    { "parent", "父级天体" }, { "semiMajorAxis", "半长轴" }, { "eccentricity", "离心率" },
    { "inclination", "倾角" }, { "longitudeOfAscendingNode", "升交点经度" },
    { "argumentOfPeriapsis", "近点幅角" }, { "trueAnomaly", "真近点角" },
    { "direction", "公转方向" }, { "multiplierSOI", "引力范围倍率" },
    { "smaDifficultyScale", "半长轴缩放（按难度）" }, { "soiDifficultyScale", "引力范围缩放（按难度）" },
    { "orbitalPeriod", "公转周期" },
    // 地标 / 成就
    { "startAngle", "起始角度" }, { "endAngle", "结束角度" },
    { "Landed", "已着陆" }, { "Takeoff", "已起飞" }, { "Atmosphere", "已进入大气" },
    { "Orbit", "已入轨" }, { "Crash", "已坠毁" },
    // 颜色分量 / 难度
    { "r", "红" }, { "g", "绿" }, { "b", "蓝" }, { "a", "透明度" },
    { "Easy", "简单" }, { "Normal", "普通" }, { "Hard", "困难" }, { "Realistic", "真实" },
    { "base", "当前难度" },
};

// ---------------- 参数预设（原版数值，和网站可选的一致） ----------------
// 原版参数预设（数值取自游戏 1.6 自带行星，和网站可选的一致）
struct PlanetPreset {
    const char* cn; const char* en;
    double radius, gravity, atmoH, atmoD;
    bool hasAtmo, hasRings;
    float cr, cg, cb;
    const char* tex;
};
static const PlanetPreset kPlanets[] = {
    { "太阳", "Sun", 34817000, 247, 0, 0, false, false, 1.00f, 0.85f, 0.20f, "Sun" },
    { "水星", "Mercury", 120000, 3.27, 0, 0, false, false, 0.50f, 0.45f, 0.40f, "Mercury" },
    { "金星", "Venus", 300000, 8.87, 250000, 0.465, true, false, 0.85f, 0.70f, 0.40f, "Venus" },
    { "地球", "Earth", 314970, 9.8, 40000, 0.005, true, false, 0.25f, 0.50f, 0.90f, "Earth_WithOceans" },
    { "火星", "Mars", 169500, 3.61, 11000, 0.019, true, false, 0.75f, 0.35f, 0.20f, "Mars" },
    { "木星", "Jupiter", 3500000, 24.8, 200000, 0.5, true, false, 0.80f, 0.65f, 0.45f, "Jupiter" },
    { "土星", "Saturn", 2911600, 10.44, 200000, 0.5, true, true, 0.85f, 0.75f, 0.55f, "Saturn" },
    { "天王星", "Uranus", 1268100, 8.69, 200000, 0.5, true, false, 0.55f, 0.80f, 0.85f, "Uranus" },
    { "海王星", "Neptune", 1238200, 11.15, 200000, 0.5, true, false, 0.30f, 0.50f, 0.85f, "Neptune" },
    { "冥王星", "Pluto", 59400, 0.62, 0, 0, false, false, 0.60f, 0.50f, 0.45f, "Pluto" },
    { "谷神星", "Ceres", 23475, 0.284, 0, 0, false, false, 0.55f, 0.50f, 0.45f, "Ceres" },
    { "月球", "Moon", 86500, 1.42, 0, 0, false, false, 0.60f, 0.60f, 0.60f, "Moon" },
    { "火卫一", "Phobos", 1000, 0.087, 0, 0, false, false, 0.55f, 0.50f, 0.45f, "Phobos" },
    { "火卫二", "Deimos", 600, 0.054, 0, 0, false, false, 0.55f, 0.50f, 0.45f, "Deimos" },
    { "木卫一", "Io", 91000, 1.796, 0, 0, false, false, 0.85f, 0.80f, 0.40f, "Io" },
    { "木卫二", "Europa", 78000, 1.314, 0, 0, false, false, 0.80f, 0.75f, 0.65f, "Europa" },
    { "木卫三", "Ganymede", 132000, 1.428, 0, 0, false, false, 0.65f, 0.60f, 0.55f, "Ganymede" },
    { "木卫四", "Callisto", 120000, 1.235, 0, 0, false, false, 0.50f, 0.48f, 0.45f, "Callisto" },
    { "土卫六", "Titan", 128700, 1.35, 600000, 0.16, true, false, 0.80f, 0.65f, 0.40f, "Titan" },
    { "土卫二", "Enceladus", 12600, 0.113, 0, 0, false, false, 0.90f, 0.90f, 0.92f, "Enceladus" },
    { "土卫一", "Mimas", 9910, 0.064, 0, 0, false, false, 0.70f, 0.70f, 0.70f, "Mimas" },
    { "土卫三", "Tethys", 26550, 0.146, 0, 0, false, false, 0.75f, 0.75f, 0.75f, "Tethys" },
    { "土卫四", "Dione", 28070, 0.212, 0, 0, false, false, 0.70f, 0.70f, 0.70f, "Dione" },
    { "土卫五", "Rhea", 38175, 0.26, 0, 0, false, false, 0.70f, 0.70f, 0.70f, "Rhea" },
    { "土卫八", "Iapetus", 36750, 0.223, 0, 0, false, false, 0.50f, 0.40f, 0.30f, "Iapetus" },
    { "土卫十五", "Pan", 705, 0.031, 0, 0, false, false, 0.60f, 0.55f, 0.50f, "Pan" },
    { "天卫五", "Miranda", 11790, 0.076, 0, 0, false, false, 0.60f, 0.60f, 0.60f, "Miranda" },
    { "天卫一", "Ariel", 28945, 0.246, 0, 0, false, false, 0.65f, 0.65f, 0.65f, "Ariel" },
    { "天卫二", "Umbriel", 29235, 0.195, 0, 0, false, false, 0.50f, 0.50f, 0.50f, "Umbriel" },
    { "天卫三", "Titania", 39400, 0.371, 0, 0, false, false, 0.60f, 0.55f, 0.55f, "Titania" },
    { "天卫四", "Oberon", 38070, 0.358, 0, 0, false, false, 0.55f, 0.50f, 0.50f, "Oberon" },
    { "天卫二十七", "Puck", 4050, 0.054, 0, 0, false, false, 0.55f, 0.55f, 0.55f, "Puck" },
    { "海卫一", "Triton", 67670, 0.779, 0, 0, false, false, 0.80f, 0.80f, 0.85f, "Triton" },
    { "海卫八", "Proteus", 10450, 0.054, 0, 0, false, false, 0.50f, 0.50f, 0.50f, "Proteus" },
    { "海卫三", "Naiad", 1650, 0.054, 0, 0, false, false, 0.50f, 0.50f, 0.50f, "Naiad" },
    { "冥卫三", "Hydra", 1900, 0.005, 0, 0, false, false, 0.55f, 0.50f, 0.50f, "Hydra" },
    { "冥卫二", "Nix", 2490, 0.002, 0, 0, false, false, 0.55f, 0.50f, 0.50f, "Nix" },
    { "冥卫一", "Charon", 30300, 0.288, 0, 0, false, false, 0.50f, 0.45f, 0.40f, "Charon" },
    { "木卫十四", "Thebe", 2465, 0.019, 0, 0, false, false, 0.55f, 0.50f, 0.45f, "Thebe" },
    { "近地小行星", "Near_Earth_Asteroid", 100, 0.034, 0, 0, false, false, 0.50f, 0.45f, 0.40f, "Asteroid" },
};

struct ParamPreset { std::string label; std::string value; bool isColor = false; float cr = 0, cg = 0, cb = 0; };
// 行星名跟着语言走
static const char* PlanetName(const PlanetPreset& p) { return g_english ? p.en : p.cn; }
// 新建星球模板名跟着语言走（定义在前面）


static std::string NumStr(double v) {
    char b[64];
    if (v == (double)(long long)v) snprintf(b, sizeof(b), "%lld", (long long)v);
    else snprintf(b, sizeof(b), "%.10g", v);
    return b;
}
static bool EndsWith(const std::string& s, const char* tail) {
    size_t n = strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}
static void AddP(std::vector<ParamPreset>& out, const std::string& label, const std::string& value) {
    ParamPreset p; p.label = label; p.value = value; out.push_back(p);
}
// 贴图类预设：内置名 + 游戏里实际加载到的
static void AddTextures(std::vector<ParamPreset>& out, const char* const* names, int n) {
    for (int i = 0; i < n; i++) AddP(out, names[i], names[i]);
    for (int i = 0; i < (int)g_textures.size(); i++) {
        bool dup = false;
        for (auto& p : out) if (p.value == g_textures[i]) { dup = true; break; }
        if (!dup) AddP(out, g_textures[i], g_textures[i]);
    }
}

static const char* kDetailTex[] = { "Snow", "Sand", "Ice02", "DryGround", "Dark_Dust", "DarkDust02",
                                    "HardRocks02", "Dots02", "Circles02", "Blured02", "Neutral", "None" };
static const char* kAtmoTex[] = { "Atmo_Earth", "Atmo_Mars", "Atmo_Venus", "Atmo_Jupiter", "Atmo_Saturn",
                                  "Atmo_Uranus", "Atmo_Neptune", "Atmo_Titan", "Atmo_Europa", "Atmo_Triton",
                                  "Atmo_Enceladus", "Atmo_Pluto", "Atmo_Sun", "None" };
static const char* kCloudTex[] = { "Earth_Clouds", "Earth_Clouds_Front", "Venus_Clouds_Front", "None" };
static const char* kOceanTex[] = { "Earth_OceanMask_V2", "Titan_LakeMask", "None" };
static const char* kRingTex[] = { "Saturn_Rings", "None" };

// 按字段给出预设值；没有就返回空
static std::vector<ParamPreset> PresetsFor(const std::string& path) {
    std::vector<ParamPreset> out;

    if (path == "basics.gravity") {
        for (auto& p : kPlanets) AddP(out, std::string(PlanetName(p)) + "  " + NumStr(p.gravity), NumStr(p.gravity));
        return out;
    }
    if (path == "basics.radius") {
        for (auto& p : kPlanets) AddP(out, std::string(PlanetName(p)) + "  " + NumStr(p.radius), NumStr(p.radius));
        return out;
    }
    if (path == "basics.timewarpHeight") {
        AddP(out, TR("地球  25000"), "25000");
        AddP(out, TR("火星  15000"), "15000");
        AddP(out, TR("巨行星  80000"), "80000");
        AddP(out, TR("卫星  5000"), "5000");
        AddP(out, TR("小卫星  2000"), "2000");
        AddP(out, TR("小行星  1000"), "1000");
        return out;
    }
    if (path == "basics.velocityArrowsHeight") {
        AddP(out, TR("默认  5000"), "5000");
        AddP(out, TR("大星球  10000"), "10000");
        return out;
    }
    if (EndsWith(path, "terrain.verticeSize")) {
        AddP(out, TR("默认  4"), "4");
        AddP(out, TR("火星  3"), "3");
        AddP(out, TR("地球  2"), "2");
        AddP(out, TR("卫星  1"), "1");
        AddP(out, TR("小行星  0.5"), "0.5");
        return out;
    }
    if (EndsWith(path, "terrain.TERRAIN_TEXTURE_DATA.planetTexture")) {
        for (auto& p : kPlanets) AddP(out, std::string(PlanetName(p)) + "  " + p.tex, p.tex);
        AddTextures(out, kDetailTex, 0);
        for (int i = 0; i < (int)g_textures.size(); i++) {
            bool dup = false;
            for (auto& q : out) if (q.value == g_textures[i]) { dup = true; break; }
            if (!dup) AddP(out, g_textures[i], g_textures[i]);
        }
        return out;
    }
    if (EndsWith(path, "surfaceTexture_A") || EndsWith(path, "surfaceTexture_B") ||
        EndsWith(path, "terrainTexture_C")) {
        AddTextures(out, kDetailTex, (int)(sizeof(kDetailTex) / sizeof(kDetailTex[0])));
        return out;
    }
    if (EndsWith(path, "oceanMaskTexture")) {
        AddTextures(out, kOceanTex, (int)(sizeof(kOceanTex) / sizeof(kOceanTex[0])));
        return out;
    }
    if (EndsWith(path, "ringsTexture")) {
        AddTextures(out, kRingTex, (int)(sizeof(kRingTex) / sizeof(kRingTex[0])));
        return out;
    }
    if (EndsWith(path, "GRADIENT.texture")) {
        AddTextures(out, kAtmoTex, (int)(sizeof(kAtmoTex) / sizeof(kAtmoTex[0])));
        return out;
    }
    if (EndsWith(path, "CLOUDS.texture") || EndsWith(path, "cloudsTexture")) {
        AddTextures(out, kCloudTex, (int)(sizeof(kCloudTex) / sizeof(kCloudTex[0])));
        return out;
    }
    if (EndsWith(path, "water.oceanDepth")) {
        AddP(out, TR("地球  5000"), "5000");
        AddP(out, TR("默认  1000"), "1000");
        AddP(out, TR("土卫六  500"), "500");
        return out;
    }
    if (path == "atmo.height" || path == "atmoVis.GRADIENT.height") {
        for (auto& p : kPlanets) {
            if (!p.hasAtmo) continue;
            double v = (path == "atmo.height") ? p.atmoH : p.atmoH * 1.2;
            AddP(out, std::string(PlanetName(p)) + "  " + NumStr(v), NumStr(v));
        }
        return out;
    }
    if (path == "atmo.density") {
        for (auto& p : kPlanets) {
            if (!p.hasAtmo) continue;
            AddP(out, std::string(PlanetName(p)) + "  " + NumStr(p.atmoD), NumStr(p.atmoD));
        }
        return out;
    }
    if (path == "atmo.curve") {
        AddP(out, TR("地球  10"), "10");
        AddP(out, TR("火星  9"), "9");
        AddP(out, TR("巨行星  15"), "15");
        AddP(out, TR("太阳/土卫六  13"), "13");
        return out;
    }
    if (path == "atmo.parachuteMultiplier") {
        AddP(out, TR("普通  1"), "1");
        AddP(out, TR("厚大气  3"), "3");
        return out;
    }
    if (EndsWith(path, "CLOUDS.startHeight")) {
        AddP(out, TR("地球  1200"), "1200");
        AddP(out, TR("贴地  0"), "0");
        AddP(out, TR("关闭  -1"), "-1");
        return out;
    }
    if (EndsWith(path, "CLOUDS.alpha")) {
        AddP(out, TR("地球  0.1"), "0.1");
        AddP(out, TR("木星  0.65"), "0.65");
        AddP(out, TR("不透明  1"), "1");
        AddP(out, TR("关闭  0"), "0");
        return out;
    }
    if (EndsWith(path, "CLOUDS.velocity")) {
        AddP(out, TR("地球  2"), "2");
        AddP(out, TR("慢  1"), "1");
        AddP(out, TR("静止  0"), "0");
        return out;
    }
    if (EndsWith(path, "CLOUDS.width")) {
        AddP(out, TR("地球  40845"), "40845");
        AddP(out, TR("小云层  20000"), "20000");
        AddP(out, TR("大云层  80000"), "80000");
        return out;
    }
    if (EndsWith(path, "CLOUDS.height")) {
        AddP(out, TR("地球  36000"), "36000");
        AddP(out, TR("薄云  12000"), "12000");
        return out;
    }
    if (EndsWith(path, "rings.startRadius")) {
        AddP(out, TR("土星  3800000"), "3800000");
        AddP(out, TR("紧凑  2000000"), "2000000");
        return out;
    }
    if (EndsWith(path, "rings.endRadius")) {
        AddP(out, TR("土星  6600000"), "6600000");
        AddP(out, TR("宽环  9000000"), "9000000");
        return out;
    }
    if (path.find(".mapColor.") != std::string::npos) {
        for (auto& p : kPlanets) {
            ParamPreset q;
            q.label = std::string(PlanetName(p)) + TR("  色板");
            q.isColor = true; q.cr = p.cr; q.cg = p.cg; q.cb = p.cb;
            out.push_back(q);
        }
        return out;
    }
    return out;
}

static std::string CnOf(const std::string& key) {
    // 英文模式直接显示原始字段名
    if (g_english) return key;
    for (int i = 0; i < (int)(sizeof(kCnNames) / sizeof(kCnNames[0])); i++) {
        if (key == kCnNames[i].en) return kCnNames[i].cn;
    }
    return key;   // 没收录就原样显示
}

// 把 terrain.TERRAIN_TEXTURE_DATA.shadowIntensity 变成「阴影强度」
static std::string PrettyField(const std::string& path) {
    if (path == "terrain.formula.base") return TR("地形公式（当前难度）");
    if (path.rfind("terrain.formula.", 0) == 0) return TR("地形公式（") + CnOf(path.substr(16)) + "）";
    // 地标是 landmarks[0].name 这种
    if (path.rfind("landmarks[", 0) == 0) {
        size_t br = path.find(']');
        int idx = atoi(path.substr(10, br - 10).c_str()) + 1;
        std::string rest = (br != std::string::npos && br + 2 < path.size()) ? path.substr(br + 2) : "";
        return TR("地标 ") + std::to_string(idx) + (rest.empty() ? "" : " · " + CnOf(rest));
    }
    // 去掉模块前缀，模块名已经显示在分组标题上
    static const char* mods[] = { "basics.", "atmo.", "atmoVis.", "terrain.", "water.",
                                  "rings.", "clouds.", "post.", "orbit." };
    std::string p = path;
    for (int i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); i++) {
        std::string m(mods[i]);
        if (p.rfind(m, 0) == 0) { p = p.substr(m.size()); break; }
    }
    if (p.empty()) return path;

    std::string out;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t dot = p.find('.', pos);
        std::string seg = (dot == std::string::npos) ? p.substr(pos) : p.substr(pos, dot - pos);
        if (!seg.empty()) {
            std::string base = seg, idx;
            size_t br = seg.find('[');
            if (br != std::string::npos) {
                base = seg.substr(0, br);
                size_t br2 = seg.find(']', br);
                if (br2 != std::string::npos) idx = seg.substr(br + 1, br2 - br - 1);
            }
            std::string cn = base.empty() ? "" : CnOf(base);
            if (!idx.empty()) cn += (cn.empty() ? "" : " ") + std::to_string(atoi(idx.c_str()) + 1);
            if (!cn.empty()) {
                if (!out.empty()) out += " · ";
                out += cn;
            }
        }
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return out.empty() ? path : out;
}

// 按路径找字段行
static FieldRow* FindFieldRow(const std::string& path) {
    for (auto& f : g_fields) if (f.path == path) return &f;
    return nullptr;
}

// 颜色四个分量一起写回游戏
static void ApplyColor(const std::string& base, const float c[4]) {
    const char* comp[4] = { ".r", ".g", ".b", ".a" };
    for (int i = 0; i < 4; i++) {
        std::string path = base + comp[i];
        std::string v = NumStr(c[i]);
        SendCmd("SET " + g_selected + " " + path + " " + v);
        FieldRow* fr = FindFieldRow(path);
        if (fr) {
            strncpy(fr->buf, v.c_str(), sizeof(fr->buf) - 1);
            fr->buf[sizeof(fr->buf) - 1] = 0;
            fr->value = v;
        }
    }
    LogLine("color " + base + " = " + NumStr(c[0]) + " " + NumStr(c[1]) + " " + NumStr(c[2]) + " " + NumStr(c[3]));
}


// ---------------- 数值滑块 ----------------
struct SliderRange { double lo = 0, hi = 1, step = 0; bool custom = false; };
static std::unordered_map<std::string, SliderRange> g_sliderRanges;
static char g_numVal[64] = "", g_numLo[32] = "", g_numHi[32] = "", g_numStep[32] = "";
static std::string g_numPath;

// 不知道范围时按当前值上下各一倍给个大概；常用有界字段直接给固定范围
static SliderRange DefaultRange(const std::string& path, double v) {
    SliderRange r;
    if (path.find("alpha") != std::string::npos ||
        path.find("opacity_") != std::string::npos ||
        path.find("Color.") != std::string::npos ||
        path.find("color.") != std::string::npos ||
        path.find("mapColor") != std::string::npos ||
        path.find("saturation") != std::string::npos) {
        r.lo = 0.0; r.hi = 1.0;
    } else if (path.find("Angle") != std::string::npos ||
               path.find("inclination") != std::string::npos ||
               path.find("argumentOfPeriapsis") != std::string::npos) {
        r.lo = -180.0; r.hi = 360.0;
    } else if (path.find("eccentricity") != std::string::npos) {
        r.lo = 0.0; r.hi = 2.0;
    } else {
        double a = fabs(v);
        if (a < 1e-9) { r.lo = -1.0; r.hi = 1.0; }
        else { r.lo = v - a; r.hi = v + a; }
        if (r.lo > 0.0 && v > 0.0) r.lo = 0.0;
        if (r.hi < 1.0) r.hi = 1.0;
    }
    if (r.hi <= r.lo) r.hi = r.lo + 1.0;
    return r;
}

// ImGui 的 SliderFloat 断言 p_min >= -FLT_MAX/2 && p_max <= FLT_MAX/2。
// 游戏里有些字段用 float.MaxValue(3.40282347e+38) 表示无限，再乘 2 就溢出成 inf；
// NaN 也会让比较为 false 直接触发断言。这里统一兜住，异常值退回普通输入框。
static const double kSafeMax = 1.0e30;   // 远小于 FLT_MAX/2，留足余量

static bool FiniteNum(double v) {
    return std::isfinite(v) && v > -kSafeMax && v < kSafeMax;
}
static double ClampNum(double v, double lo, double hi) {
    if (!std::isfinite(v)) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
// 返回 false = 这个字段别用滑块，改用文本输入
static bool SanitizeRange(const SliderRange& in, double v, SliderRange& out, float& sv) {
    out = in;
    out.lo = ClampNum(out.lo, -kSafeMax, kSafeMax);
    out.hi = ClampNum(out.hi, -kSafeMax, kSafeMax);
    out.step = ClampNum(out.step, 0.0, kSafeMax);
    if (!FiniteNum(v)) return false;
    // 当前值超范围就撑开，全程 double 运算，撑完仍必须在安全区内
    double pad = fabs(v) + 1.0;
    if (v < out.lo) out.lo = v - pad;
    if (v > out.hi) out.hi = v + pad;
    if (!FiniteNum(out.lo)) out.lo = -kSafeMax;
    if (!FiniteNum(out.hi)) out.hi = kSafeMax;
    if (out.hi <= out.lo) out.hi = out.lo + 1.0;
    if (!FiniteNum(out.hi)) { out.lo = -1.0; out.hi = 1.0; }
    sv = (float)v;
    return true;
}
// 字段标签按可用宽度裁成省略号，英文长名也不会压到右边控件
static std::string Ellipsize(const std::string& src, float maxW) {
    if (maxW <= 0.f || ImGui::CalcTextSize(src.c_str()).x <= maxW) return src;
    const float dotsW = ImGui::CalcTextSize("...").x;
    std::string out;
    size_t i = 0;
    while (i < src.size()) {
        unsigned char c = (unsigned char)src[i];
        size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : 4;
        if (i + len > src.size()) len = 1;
        std::string ch = src.substr(i, len);
        float w = ImGui::CalcTextSize(out.c_str()).x + ImGui::CalcTextSize(ch.c_str()).x + dotsW;
        if (w > maxW) break;
        out += ch;
        i += len;
    }
    return out + "...";
}

// 用画笔画一支笔（矢量线条，等价于 svg 图标）
static bool PenButton(const char* id) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(26, 22));
    bool clicked = ImGui::IsItemClicked(0);
    bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImDrawList* dl0 = ImGui::GetWindowDrawList();
        dl0->AddRectFilled(p, ImVec2(p.x + 26, p.y + 22), IM_COL32(255, 255, 255, 28), 5.f);
    }
    ImU32 c = hov ? IM_COL32(255, 255, 255, 240) : IM_COL32(190, 205, 225, 195);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 笔身两条平行线 + 两端封口
    dl->AddLine(ImVec2(p.x + 6, p.y + 16), ImVec2(p.x + 18, p.y + 4), c, 2.f);
    dl->AddLine(ImVec2(p.x + 10, p.y + 20), ImVec2(p.x + 21, p.y + 9), c, 2.f);
    dl->AddLine(ImVec2(p.x + 6, p.y + 16), ImVec2(p.x + 10, p.y + 20), c, 2.f);
    dl->AddLine(ImVec2(p.x + 18, p.y + 4), ImVec2(p.x + 21, p.y + 9), c, 2.f);
    // 笔尖
    dl->AddTriangleFilled(ImVec2(p.x + 3, p.y + 22), ImVec2(p.x + 10, p.y + 20),
                          ImVec2(p.x + 6, p.y + 16), c);
    return clicked;
}

// 地图地标：范围在左侧星球预览里看，这里只管开关、增删和数值
static void DrawLandmarkDisc(float winW) {
    if (GlassButton(g_preview ? TR("坐标预览 开") : TR("坐标预览"), ImVec2(0, 30), false, false, g_preview)) {
        g_preview = !g_preview;
        g_pvDragLm = -1;
        LogLine(g_preview ? TR("坐标预览已开") : TR("坐标预览已关"));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(TR("打开后左侧星球预览会画出每个地标的范围 端点能直接拖"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_preview ? TR("左侧已经画出来了") : TR("左侧只画地形"));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("左侧预览滚轮能放大到 16 倍 拖红点就改角度"));

    ImGui::Spacing();
    if (GlassButton(TR("＋ 新地标"), ImVec2(0, 26))) {
        SendCmd("CMD addlandmark");
        LogLine(TR("加了一个地标"));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("在当前星球末尾加一个地标 默认 0-10 度"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("删掉后记得点保存"));

    const float rowH = 22.f;
    const float delW = 46.f;
    for (int i = 0; i < 64; i++) {
        std::string base = "landmarks[" + std::to_string(i) + "]";
        FieldRow* fn = FindFieldRow(base + ".name");
        FieldRow* fs = FindFieldRow(base + ".startAngle");
        FieldRow* fe = FindFieldRow(base + ".endAngle");
        if (!fn && !fs && !fe) continue;

        ImGui::PushID(1000 + i);
        ImGui::BeginGroup();
        char sbuf[64];
        double sv = fs ? atof(fs->buf) : 0.0;
        double ev = fe ? atof(fe->buf) : 0.0;
        snprintf(sbuf, sizeof(sbuf), "%d", i + 1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", sbuf);
        ImGui::SameLine(34.f);
        ImGui::TextUnformatted((fn && fn->buf[0]) ? fn->buf : "-");
        ImGui::SameLine(34.f + 130.f);
        snprintf(sbuf, sizeof(sbuf), "%.1f - %.1f", sv, ev);
        ImGui::TextDisabled("%s", sbuf);
        ImGui::EndGroup();
        ImGui::SameLine(winW - delW - 10.f);
        if (GlassButton(TR("删除"), ImVec2(delW, rowH), false, true)) {
            SendCmd("CMD dellandmark " + std::to_string(i));
            LogLine(TR("删掉地标 ") + std::to_string(i + 1));
        }
        ImGui::PopID();
    }
}

static void DrawFields() {
    if (g_selected.empty()) {
        ImGui::TextDisabled(TR("← 左边选一个星球"));
        return;
    }
    ImGui::Text("%s", g_selected.c_str());
    ImGui::SameLine();
    if (GlassButton(TR("应用"))) SendCmd("CMD apply");
    ImGui::SameLine();
    if (GlassButton(TR("保存"))) SendCmd("CMD save");
    ImGui::SameLine();
    if (GlassButton(TR("重读"))) { SendCmd("CMD reload"); RequestFields(g_selected); }
    ImGui::SameLine();
    if (GlassButton(TR("改名"))) g_renameOpen = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("重命名当前星球（会同步其它星球的 orbit.parent 和文件名）"));
    ImGui::SameLine();
    if (GlassButton(TR("删星球"))) g_delPlanetConfirm = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("从包和运行时里删除当前星球"));
    ImGui::SameLine();
    if (GlassButton(TR("质心"))) {
        SendCmd("CMD soifit " + g_selected);
        LogLine("soifit " + g_selected);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("把该星球的引力范围(SOI)撑到覆盖所有子天体轨道，适合双星质心"));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##filter", TR("过滤字段…"), g_filter, sizeof(g_filter));


    ImGui::Separator();
    ImGui::BeginChild("##fields", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // 字段清空那一帧内容高度会塌成 0，滚动被 ImGui 夹到顶部；数据回来后还原
    if (g_restoreScroll && !g_fields.empty()) {
        ImGui::SetScrollY(g_fieldsScroll);
        g_restoreScroll = false;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;
    float winW = ImGui::GetContentRegionAvail().x;
    int shownTotal = 0;

    for (int m = 0; m < kModuleCount; m++) {
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
        const float btnW = TextBtnW(TR("删除模块"), 96.f);
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
        if (cnt > 0) snprintf(hdr, sizeof(hdr), "%s  (%d)", TR(kModuleLong[m]), cnt);
        else snprintf(hdr, sizeof(hdr), TR("%s  （未启用）"), TR(kModuleLong[m]));
        dl->AddText(ImVec2(rowPos.x + 28.f, rowPos.y + (26.f - ImGui::GetTextLineHeight()) * 0.5f),
                    cnt > 0 ? IM_COL32(235, 240, 248, 255) : IM_COL32(140, 150, 165, 255), hdr);

        // ---- 右侧按钮（半透明底，文字居中）----
        {
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + winW - btnW, rowPos.y));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
            if (m == kModuleCount - 1) {
                // 地标模块没有「模块」的概念：一个地标都没有时也要能启用
                if (cnt == 0) {
                    if (GlassButton(TR("启用"), ImVec2(btnW, 26))) {
                        SendCmd("CMD addlandmark");
                        g_modOpen[m] = true;
                        LogLine(TR("加了一个地标"));
                    }
                } else {
                    ImGui::Dummy(ImVec2(btnW, 26));
                }
            } else if (kModuleRemovable[m] && cnt > 0) {
                if (GlassButton(TR("删除模块"), ImVec2(btnW, 26), false, true)) g_delModPending = m;
            } else if (cnt == 0) {
                if (GlassButton(TR("启用"), ImVec2(btnW, 26))) {
                    SendCmd("CMD addmod " + g_selected + " " + kModules[m]);
                    g_pendingRefresh = 0.6f;
                    g_modOpen[m] = true;
                }
            }
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + 28.f));
        }

        // ---- 内容：逐行揭示（anim 决定显示多少行）----
        // 地标模块即使一个地标都没有也要画出增删区，否则加不了第一个
        if (m == kModuleCount - 1 && anim > 0.002f) DrawLandmarkDisc(winW);
        if (anim > 0.002f && cnt > 0) {
            std::string prefix = ModulePrefix(kModules[m]);
            std::vector<FieldRow*> rows;
            for (auto& f : g_fields) {
                if (f.path.rfind(prefix, 0) != 0) continue;
                if (g_filter[0] && f.path.find(g_filter) == std::string::npos &&
                    PrettyField(f.path).find(g_filter) == std::string::npos) continue;
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
                std::string pretty = PrettyField(fp->path);

                // 颜色：r/g/b/a 四行合并成一行，带色块和调色盘
                if (fp->kind != 5) {
                    std::string cb;
                    if (EndsWith(fp->path, ".r")) {
                        std::string b0 = fp->path.substr(0, fp->path.size() - 2);
                        if (FindFieldRow(b0 + ".g")) cb = b0;
                    }
                    if (!cb.empty()) {
                        FieldRow* fg = FindFieldRow(cb + ".g");
                        FieldRow* fb = FindFieldRow(cb + ".b");
                        FieldRow* fa = FindFieldRow(cb + ".a");
                        float col[4] = { (float)atof(fp->buf),
                                         fg ? (float)atof(fg->buf) : 0.f,
                                         fb ? (float)atof(fb->buf) : 0.f,
                                         fa ? (float)atof(fa->buf) : 1.f };
                        ImGui::TextDisabled("%s", PrettyField(cb).c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cb.c_str());

                        // 原版色板
                        std::vector<ParamPreset> cps = PresetsFor(fp->path);
                        if (!cps.empty()) {
                            ImGui::SameLine();
                            float pbx = winW - 330.f;
                            if (pbx < 150.f) pbx = 150.f;
                            ImGui::SetCursorPosX(pbx);
                            if (ImGui::Button(TR("色板"), ImVec2(TextBtnW(TR("色板"), 46.f), 0))) ImGui::OpenPopup("##cp");
                            if (ImGui::BeginPopup("##cp")) {
                                float lmax = ImGui::GetIO().DisplaySize.y * 0.45f;
                                float lneed = (float)cps.size() * ImGui::GetTextLineHeightWithSpacing() + 6.f;
                                ImGui::BeginChild("##cps", ImVec2(200.f, lneed > lmax ? lmax : lneed), ImGuiChildFlags_None);
                                for (int ci = 0; ci < (int)cps.size(); ci++) {
                                    if (!cps[ci].isColor) continue;
                                    ImGui::ColorButton("##dot", ImVec4(cps[ci].cr, cps[ci].cg, cps[ci].cb, 1.f),
                                                       ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
                                    ImGui::SameLine();
                                    if (ImGui::Selectable(cps[ci].label.c_str())) {
                                        float nc[4] = { cps[ci].cr, cps[ci].cg, cps[ci].cb, 1.f };
                                        ApplyColor(cb, nc);
                                        ImGui::CloseCurrentPopup();
                                        break;
                                    }
                                }
                                ImGui::EndChild();
                                ImGui::EndPopup();
                            }
                        }

                        // 色块 + 调色盘
                        ImGui::SameLine();
                        float sx = winW - 250.f;
                        if (sx < 150.f) sx = 150.f;
                        ImGui::SetCursorPosX(sx);
                        ImGui::SetNextItemWidth(200.f);
                        if (ImGui::ColorEdit4("##col", col,
                                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                            // 拖的时候别把游戏刷爆，250ms 发一次
                            static double last = 0.0;
                            double now = ImGui::GetTime();
                            if (now - last > 0.25) {
                                last = now;
                                ApplyColor(cb, col);
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("点色块打开调色盘"));
                        ImGui::SameLine();
                        ImGui::TextDisabled("%.2f %.2f %.2f", col[0], col[1], col[2]);
                        ImGui::PopID();
                        continue;
                    }
                    // 已被 .r 那行合并显示，跳过 g/b/a
                    if (EndsWith(fp->path, ".g") || EndsWith(fp->path, ".b") || EndsWith(fp->path, ".a")) {
                        std::string b0 = fp->path.substr(0, fp->path.size() - 2);
                        if (FindFieldRow(b0 + ".r")) { ImGui::PopID(); continue; }
                    }
                }

                if (fp->kind == 5) {
                    // 公式单独占一行，下面放整宽的多行编辑器
                    ImGui::TextDisabled("%s", pretty.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fp->path.c_str());
                    ImGui::SetCursorPosX(8.f);
                    float bw = winW - 28.f;
                    if (bw < 200.f) bw = 200.f;
                    committed = ImGui::InputTextMultiline("##f", fp->buf, sizeof(fp->buf),
                                                          ImVec2(bw, 108.f),
                                                          ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
                } else {
                    bool isNum = (fp->kind == 0 || fp->kind == 1);
                    std::vector<ParamPreset> presets = PresetsFor(fp->path);
                    // 右对齐布局：从右往左排，所有 x 由实际控件宽度算出来。
                    // 中文「预设」约 48px，英文 Presets 约 66px；写死间距会互相压。
                    const float rightPad = 14.f, gapF = 8.f, penW = 26.f, valW = 68.f;
                    float presetW = presets.empty() ? 0.f : TextBtnW(TR("预设"), 46.f);
                    float ctrlW = isNum ? 130.f : 200.f;
                    float tailW = isNum ? (penW + gapF + valW) : 0.f;
                    auto fixedWidth = [&](float w) {
                        return rightPad + w + tailW + (presetW > 0.f ? gapF + presetW : 0.f) + gapF;
                    };
                    const float labelMin = 96.f;
                    float fixedW = fixedWidth(ctrlW);
                    if (fixedW + labelMin > winW) {
                        ctrlW -= (fixedW + labelMin - winW);
                        if (ctrlW < 70.f) ctrlW = 70.f;
                        fixedW = fixedWidth(ctrlW);
                    }
                    float ctrlX = winW - rightPad - ctrlW - tailW;
                    float valX = ctrlX + ctrlW + gapF;
                    float penX = valX + valW + gapF;
                    float presetX = ctrlX - (presetW > 0.f ? gapF + presetW : 0.f);
                    float labelMax = presetX - gapF - ImGui::GetCursorPosX();
                    if (labelMax < 30.f) labelMax = 30.f;
                    std::string label = Ellipsize(pretty, labelMax);
                    ImGui::TextDisabled("%s", label.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", pretty.c_str(), fp->path.c_str());
                    if (!presets.empty()) {
                        ImGui::SameLine();
                        ImGui::SetCursorPosX(presetX);
                        if (ImGui::Button(TR("预设"), ImVec2(presetW, 0))) ImGui::OpenPopup("##pp");
                        if (ImGui::BeginPopup("##pp")) {
                            ImGui::TextDisabled("%s", fp->path.c_str());
                            ImGui::Separator();
                            // 预设可能几十条，限制高度可滚动
                            float listMax = ImGui::GetIO().DisplaySize.y * 0.45f;
                            float listNeed = (float)presets.size() * ImGui::GetTextLineHeightWithSpacing() + 6.f;
                            ImGui::BeginChild("##pscroll", ImVec2(240.f, listNeed > listMax ? listMax : listNeed), ImGuiChildFlags_None);
                            for (int pi = 0; pi < (int)presets.size(); pi++) {
                                if (!ImGui::Selectable(presets[pi].label.c_str())) continue;
                                if (presets[pi].isColor) {
                                    // 颜色是四个分量，一起写
                                    std::string base = fp->path.substr(0, fp->path.rfind('.'));
                                    SendCmd("SET " + g_selected + " " + base + ".r " + NumStr(presets[pi].cr));
                                    SendCmd("SET " + g_selected + " " + base + ".g " + NumStr(presets[pi].cg));
                                    SendCmd("SET " + g_selected + " " + base + ".b " + NumStr(presets[pi].cb));
                                    SendCmd("SET " + g_selected + " " + base + ".a 1");
                                    RequestFields(g_selected);
                                } else {
                                    SendCmd("SET " + g_selected + " " + fp->path + " " + presets[pi].value);
                                    strncpy(fp->buf, presets[pi].value.c_str(), sizeof(fp->buf) - 1);
                                    fp->buf[sizeof(fp->buf) - 1] = 0;
                                    fp->value = presets[pi].value;
                                    LogLine("preset " + fp->path + " = " + presets[pi].value);
                                }
                                ImGui::CloseCurrentPopup();
                                break;
                            }
                            ImGui::EndChild();
                            if (listNeed > listMax)
                                ImGui::TextDisabled(TR("滚轮滚动 · 共 %d 项"), (int)presets.size());
                            ImGui::EndPopup();
                        }
                    }
                    if (isNum) {
                        double cur = atof(fp->buf);
                        auto rit = g_sliderRanges.find(fp->path);
                        SliderRange base = (rit == g_sliderRanges.end()) ? DefaultRange(fp->path, cur) : rit->second;
                        float sv = 0.f;
                        SliderRange sr;
                        bool useSlider = SanitizeRange(base, cur, sr, sv);
                        if (useSlider) {
                        g_sliderRanges[fp->path] = sr;

                        ImGui::SameLine();
                        ImGui::SetCursorPosX(ctrlX);
                        ImGui::SetNextItemWidth(ctrlW);
                        bool sChanged = ImGui::SliderFloat("##sl", &sv, (float)sr.lo, (float)sr.hi, "%.4g");
                        if (sChanged && sr.step > 0.0) {
                            // 用户设了步长就按步长吸附
                            double k = floor((sv - sr.lo) / sr.step + 0.5);
                            sv = (float)(sr.lo + k * sr.step);
                        }
                        if (sChanged) {
                            static double lastSend = 0.0;
                            double now = ImGui::GetTime();
                            if (now - lastSend > 0.20) {
                                lastSend = now;
                                std::string v = NumStr(sv);
                                SendCmd("SET " + g_selected + " " + fp->path + " " + v);
                                strncpy(fp->buf, v.c_str(), sizeof(fp->buf) - 1);
                                fp->buf[sizeof(fp->buf) - 1] = 0;
                                fp->value = v;
                            }
                        }
                        ImGui::SameLine();
                        ImGui::SetCursorPosX(valX);
                        ImGui::TextDisabled("%.4g", sv);

                        // 笔图标：输入精确值和滑块范围
                        ImGui::SameLine();
                        ImGui::SetCursorPosX(penX);
                        if (PenButton("##pen")) {
                            g_numPath = fp->path;
                            snprintf(g_numVal, sizeof(g_numVal), "%s", fp->buf);
                            snprintf(g_numLo, sizeof(g_numLo), "%.6g", sr.lo);
                            snprintf(g_numHi, sizeof(g_numHi), "%.6g", sr.hi);
                            snprintf(g_numStep, sizeof(g_numStep), "%.6g", sr.step);
                            ImGui::OpenPopup("##num");
                        }
                        if (ImGui::BeginPopup("##num")) {
                            ImGui::TextDisabled("%s", fp->path.c_str());
                            ImGui::Separator();
                            ImGui::SetNextItemWidth(180.f);
                            ImGui::InputText(TR("数值"), g_numVal, sizeof(g_numVal));
                            ImGui::TextDisabled("%s", TR("滑块范围"));
                            ImGui::SetNextItemWidth(180.f);
                            ImGui::InputText(TR("最小"), g_numLo, sizeof(g_numLo));
                            ImGui::SetNextItemWidth(180.f);
                            ImGui::InputText(TR("最大"), g_numHi, sizeof(g_numHi));
                            ImGui::SetNextItemWidth(180.f);
                            ImGui::InputText(TR("步长"), g_numStep, sizeof(g_numStep));
                            ImGui::TextDisabled("%s", TR("拖滑块改值 或点笔输入精确值"));
                            if (ImGui::Button(TR("应用"), ImVec2(90, 26))) {
                                std::string v(g_numVal);
                                SendCmd("SET " + g_selected + " " + g_numPath + " " + v);
                                FieldRow* fr = FindFieldRow(g_numPath);
                                if (fr) {
                                    strncpy(fr->buf, v.c_str(), sizeof(fr->buf) - 1);
                                    fr->buf[sizeof(fr->buf) - 1] = 0;
                                    fr->value = v;
                                }
                                SliderRange nr;
                                nr.lo = ClampNum(atof(g_numLo), -kSafeMax, kSafeMax);
                                nr.hi = ClampNum(atof(g_numHi), -kSafeMax, kSafeMax);
                                nr.step = ClampNum(atof(g_numStep), 0.0, kSafeMax);
                                nr.custom = true;
                                if (nr.hi <= nr.lo) nr.hi = nr.lo + 1.0;
                                g_sliderRanges[g_numPath] = nr;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(TR("取消"), ImVec2(90, 26))) ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                        continue;
                        }   // useSlider
                    }
                    // 非数值字段，或数值大到滑块表达不了（SOI / NaN / inf），用文本输入
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ctrlX);
                    ImGui::SetNextItemWidth(ctrlW);
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

    if (shownTotal == 0 && g_filter[0]) ImGui::TextDisabled(TR("(没有匹配的字段)"));
    if (!g_fields.empty()) g_fieldsScroll = ImGui::GetScrollY();
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
        const char* modName = (g_delModPending >= 0 && g_delModPending < kModuleCount) ? TR(kModuleLong[g_delModPending]) : "?";
        const float POP_W = 340.f;
        ImGui::Dummy(ImVec2(POP_W, 0.f));   // 固定内容宽度，方便按钮居中
        ImGui::Spacing();
        ImGui::Text(TR("确定要删除「%s」模块吗？"), modName);
        ImGui::Spacing();
        ImGui::TextDisabled(TR("删除后该模块的字段会从星球上移除。"));
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            float x = (POP_W - total) * 0.5f;
            if (x < 0.f) x = 0.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
        if (ImGui::Button(TR("确定删除"), ImVec2(120, 32))) {
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
        if (ImGui::Button(TR("取消"), ImVec2(120, 32))) {
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

// ---------------- 左侧「星球预览」：极坐标地形圆盘，可放大 / 可拖地标 ----------------

// 拖地标时把新角度写回字段并发给游戏
static void SetLandmarkAngle(int idx, int end, double deg) {
    while (deg < 0.0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    std::string path = "landmarks[" + std::to_string(idx) + "]." + (end == 0 ? "startAngle" : "endAngle");
    char v[32];
    snprintf(v, sizeof(v), "%.1f", deg);
    SendCmd("SET " + g_selected + " " + path + " " + v);
    FieldRow* fr = FindFieldRow(path);
    if (fr) { snprintf(fr->buf, sizeof(fr->buf), "%s", v); fr->value = v; }
}

static void DrawPreview() {
    const double PI = 3.14159265358979323846;
    ImGui::Separator();

    float W = ImGui::GetContentRegionAvail().x;
    if (W < 80.f) W = 80.f;

    // 星球预览和极坐标编辑是两个功能，各走各的入口
    {
        float sp = ImGui::GetStyle().ItemSpacing.x;
        float bw = (W - sp * 2.f) / 3.f;
        if (bw < 40.f) bw = 40.f;
        if (GlassButton(TR("极坐标编辑"), ImVec2(bw, 26))) { g_view = 1; g_pvDragLm = -1; }
        ImGui::SameLine();
        if (GlassButton(g_preview ? TR("坐标预览 开") : TR("坐标预览"), ImVec2(bw, 26), false, false, g_preview)) {
            g_preview = !g_preview;
            g_pvDragLm = -1;
            LogLine(g_preview ? TR("坐标预览已开") : TR("坐标预览已关"));
        }
        ImGui::SameLine();
        if (GlassButton(TR("复位视图"), ImVec2(bw, 26))) ResetPreviewView();
    }

    // 画布高度跟着本栏剩余空间走：窗口压矮时不再固定 240 顶着日志区，被裁掉半个圆
    float H = 240.f;
    {
        float availY = ImGui::GetContentRegionAvail().y - 8.f;   // 底下还留一点给笔刷面板
        if (availY > 90.f && availY < H) H = availY;
    }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + H), IM_COL32(255, 255, 255, 12), 10.f);

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##pvcanvas", ImVec2(W, H));
    bool hovered = ImGui::IsItemHovered();
    // 鼠标在预览里时把滚轮吃掉，否则放大时外层字段区会跟着滚，图就跳
    if (hovered) ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
    ImVec2 mp = ImGui::GetMousePos();
    double now = ImGui::GetTime();

    if (g_shape.size() < 8 || g_shapeRadius <= 0.0) {
        dl->AddText(ImVec2(p.x + 12.f, p.y + H * 0.5f - 8.f), IM_COL32(150, 160, 175, 255),
                    g_selected.empty() ? TR("选中星球后显示预览") : TR("读取地形…"));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + H));
        return;
    }

    int n = (int)g_shape.size();
    float maxH = 0.f, minH = 0.f;
    for (int i = 0; i < n; i++) {
        if (g_shape[i] > maxH) maxH = g_shape[i];
        if (g_shape[i] < minH) minH = g_shape[i];
    }
    double maxR = g_shapeRadius + (double)maxH;
    if (maxR <= 0.0) maxR = 1.0;

    float pad = 12.f;
    float half = (W < H ? W : H) * 0.5f - pad;
    if (half < 20.f) half = 20.f;
    float baseScale = half / (float)(maxR * 1.02);
    float scale = baseScale * g_pvZoom;

    ImVec2 cc(p.x + W * 0.5f, p.y + H * 0.5f);
    ImVec2 ctr(cc.x + g_pvPanX, cc.y + g_pvPanY);

    // 滚轮围绕鼠标缩放
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            float wx = (mp.x - ctr.x) / scale;
            float wy = (mp.y - ctr.y) / scale;
            float factor = powf(1.12f, wheel);
            g_pvZoom *= factor;
            if (g_pvZoom < 1.f) g_pvZoom = 1.f;
            if (g_pvZoom > 16.f) g_pvZoom = 16.f;
            scale = baseScale * g_pvZoom;
            g_pvPanX = (mp.x - cc.x) - wx * scale;
            g_pvPanY = (mp.y - cc.y) - wy * scale;
            ctr = ImVec2(cc.x + g_pvPanX, cc.y + g_pvPanY);
        }
    }
    // 中键平移
    if (hovered && ImGui::IsMouseDragging(2)) {
        g_pvPanX += ImGui::GetIO().MouseDelta.x;
        g_pvPanY += ImGui::GetIO().MouseDelta.y;
        ctr = ImVec2(cc.x + g_pvPanX, cc.y + g_pvPanY);
    }
    {
        float lim = (float)maxR * scale * 0.9f;
        if (g_pvPanX > lim) g_pvPanX = lim;
        if (g_pvPanX < -lim) g_pvPanX = -lim;
        if (g_pvPanY > lim) g_pvPanY = lim;
        if (g_pvPanY < -lim) g_pvPanY = -lim;
    }

    dl->PushClipRect(p, ImVec2(p.x + W, p.y + H), true);

    // 角度 → 屏幕点；两套：按地形表面、按固定半径
    auto ptAt = [&](double deg, float r) {
        double a = deg * PI / 180.0;
        return ImVec2(ctr.x + (float)cos(a) * r, ctr.y - (float)sin(a) * r);
    };
    auto surfR = [&](double deg) {
        double a = deg * PI / 180.0;
        while (a < 0) a += PI * 2.0;
        while (a >= PI * 2.0) a -= PI * 2.0;
        double t = a / (PI * 2.0) * n;
        int i0 = (int)t % n;
        int i1 = (i0 + 1) % n;
        double f = t - (double)(int)t;
        double h = g_shape[i0] * (1.0 - f) + g_shape[i1] * f;   // 两个采样点之间线性插值，别出现台阶
        return (float)((g_shapeRadius + h) * scale);
    };

    // 轮廓与扇形共用的角度步长：屏幕上约 2.2 像素一段。
    // 4096 个采样点直接画，在缩小的圆盘上每段只有零点几像素，1.2px 的线互相重叠、
    // 抗锯齿边缘反复叠加，就成了一片灰白网纹（摩尔纹）。按像素密度抽稀即可，放大后会自动恢复细节。
    // 参考起伏带迟滞：一边刷地形时 maxH 一直在变，若每帧都跟着改段数，轮廓边缘就会一抽一抽。
    if (maxH > g_hRef * 1.12f || maxH < g_hRef * 0.88f) g_hRef = maxH;
    float degStep = 1.f;
    {
        float rTip = (float)((g_shapeRadius + (g_hRef > 0.f ? g_hRef : 0.f)) * scale);
        float pxPerDeg = rTip * (float)(PI / 180.0);
        degStep = (pxPerDeg > 0.02f) ? (2.2f / pxPerDeg) : 6.f;
        if (degStep < 0.05f) degStep = 0.05f;
        if (degStep > 6.f) degStep = 6.f;
    }

    ImU32 rim = IM_COL32(
        (int)(g_shapeCol[0] * 0.55f + 255 * 0.45f),
        (int)(g_shapeCol[1] * 0.55f + 255 * 0.45f),
        (int)(g_shapeCol[2] * 0.55f + 255 * 0.45f), 255);

    // 全圈按像素密度抽稀后的段数（放大到极限时也不超过原始采样数）
    int loop = (int)(360.0 / degStep + 0.5);
    if (loop < 48) loop = 48;
    if (loop > n) loop = n;

    // 盘面：叠多层做球面的假体积感。层数拉多色阶才细腻（8 层放大后是一条条同心色带）；
    // 放大细看时不画层，直接铺单色，免得看到一圈圈边。
    ImDrawListFlags savedFlags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    const int LAYERS = (g_pvZoom < 2.f) ? 24 : 1;
    int layerSeg = loop;
    if (layerSeg > 256) layerSeg = 256;
    if (layerSeg < 48) layerSeg = 48;
    for (int layer = 0; layer < LAYERS; layer++) {
        float t = (LAYERS > 1) ? (float)layer / (float)(LAYERS - 1) : 0.f;
        float ring = 1.0f - 0.72f * t;
        // 单色时取 24 层的平均亮度，切过去不会闪一下
        float shade = (LAYERS > 1) ? (0.62f + 0.34f * t) : 0.79f;
        ImU32 col = IM_COL32(
            (int)(g_shapeCol[0] * shade),
            (int)(g_shapeCol[1] * shade),
            (int)(g_shapeCol[2] * shade), 255);
        for (int k = 0; k < layerSeg; k++) {
            double d0 = 360.0 * k / layerSeg;
            double d1 = 360.0 * (k + 1) / layerSeg;
            float r0 = surfR(d0) * ring;
            float r1 = surfR(d1) * ring;
            ImVec2 q0 = ptAt(d0, r0);
            ImVec2 q1 = ptAt(d1, r1);
            dl->AddTriangleFilled(ctr, q0, q1, col);
        }
    }
    dl->Flags = savedFlags;

    // 海洋：海平面就是半径球面（地形高度 0）。低于海面的角度只画"地形表面 → 海面"这一层水，
    // 不能从圆心整个扇区涂蓝——那样一点点低洼就变成一大片深蓝，看着像汪洋，其实海底才差几十米。
    // 星球没有 water 模块（字段树里没有 water.* ）就不画，免得把干盆地涂成海。
    {
        bool hasWater = false;
        for (size_t fi = 0; fi < g_fields.size(); fi++) {
            if (g_fields[fi].path.rfind("water.", 0) == 0) { hasWater = true; break; }
        }
        if (hasWater) {
            const ImU32 seaWater = IM_COL32(64, 146, 220, 200);  // 水层（薄薄一层，透一点底色）
            const ImU32 seaRim = IM_COL32(176, 226, 255, 225);   // 海面线（水陆交界）
            float rSea = (float)(g_shapeRadius * scale);
            const float seaMinThick = 1.2f;   // 再浅的海也留一条看得见的蓝线，不然缩小时会消失
            ImDrawListFlags wFlags = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            // 内侧点：地形表面；但离海面不足 seaMinThick 时压到 seaMinThick，保证可见
            auto wetIn = [&](float s) {
                if (s >= rSea) return rSea;
                float lo = rSea - seaMinThick;
                return s < lo ? s : lo;
            };
            for (int k = 0; k < loop; k++) {
                double d0 = 360.0 * k / loop, d1 = 360.0 * (k + 1) / loop;
                float s0 = surfR(d0), s1 = surfR(d1);
                if (s0 >= rSea && s1 >= rSea) continue;      // 这一段全是陆地
                ImVec2 a0 = ptAt(d0, wetIn(s0)), a1 = ptAt(d1, wetIn(s1));
                ImVec2 b0 = ptAt(d0, rSea), b1 = ptAt(d1, rSea);
                dl->AddQuadFilled(a0, b0, b1, a1, seaWater);
            }
            dl->Flags = wFlags;
            // 海面弧只画在水域那几段，用连续路径描边（逐段 AddLine 会在接缝处冒出亮点）
            bool inArc = false;
            for (int k = 0; k <= loop; k++) {
                double deg = 360.0 * (double)k / (double)loop;
                bool wet = surfR(deg) < rSea;
                if (wet) {
                    if (!inArc) { dl->PathClear(); inArc = true; }
                    dl->PathLineTo(ptAt(deg, rSea));
                } else if (inArc) {
                    dl->PathStroke(seaRim, 0, 1.5f);
                    inArc = false;
                }
            }
            if (inArc) dl->PathStroke(seaRim, 0, 1.5f);
        }
    }

    // 地形轮廓（同样抽稀，避免上千条亚像素线段叠成摩尔纹）
    {
        dl->PathClear();
        for (int k = 0; k <= loop; k++) {
            double deg = 360.0 * (double)k / (double)loop;
            dl->PathLineTo(ptAt(deg, surfR(deg)));
        }
        dl->PathStroke(rim, 0, 1.2f);
    }

    // 方位刻度
    const ImU32 grid = IM_COL32(120, 130, 145, 90);
    const ImU32 dim = IM_COL32(150, 160, 175, 210);
    {
        float rg = (float)(g_shapeRadius * scale);
        if (g_pvZoom < 3.f) {
            dl->AddCircle(ctr, rg, grid, 64, 1.f);
            dl->AddCircle(ctr, rg * 0.5f, grid, 48, 1.f);
            dl->AddLine(ImVec2(ctr.x - rg, ctr.y), ImVec2(ctr.x + rg, ctr.y), grid, 1.f);
            dl->AddLine(ImVec2(ctr.x, ctr.y - rg), ImVec2(ctr.x, ctr.y + rg), grid, 1.f);
            // 标签压在圆盘内侧，放大后也不会跑出框
            float rt = rg * 0.93f;
            dl->AddText(ImVec2(ctr.x + rt, ctr.y - 7), dim, "0");
            dl->AddText(ImVec2(ctr.x - 4, ctr.y - rt - 6), dim, "90");
            dl->AddText(ImVec2(ctr.x - rt - 22, ctr.y - 7), dim, "180");
            dl->AddText(ImVec2(ctr.x - 8, ctr.y + rt - 6), dim, "270");
        }
    }

    // 地标范围：只在坐标预览打开时画
    const ImU32 red = IM_COL32(238, 74, 74, 255);
    const ImU32 redFill = IM_COL32(238, 74, 74, 52);
    const ImU32 redDim = IM_COL32(238, 74, 74, 150);
    int hitLm = -1, hitEnd = 0;
    float hitDist = 11.f;
    if (g_preview) {
        for (int i = 0; i < 64; i++) {
            std::string base = "landmarks[" + std::to_string(i) + "]";
            FieldRow* fs = FindFieldRow(base + ".startAngle");
            FieldRow* fe = FindFieldRow(base + ".endAngle");
            if (!fs && !fe) continue;
            double sv = fs ? atof(fs->buf) : 0.0;
            double ev = fe ? atof(fe->buf) : sv;
            if (!std::isfinite(sv) || !std::isfinite(ev)) continue;
            double w = ev - sv;
            while (w <= 0.0) w += 360.0;
            if (w > 360.0) w = 360.0;
            // 扇形：沿地形走，段数按像素密度抽稀（和轮廓同一把尺子）
            int seg = (int)(w / (double)degStep) + 1;
            if (seg < 6) seg = 6;
            if (seg > 720) seg = 720;
            float rIn = (float)(g_shapeRadius * scale) * 0.35f;
            // 关掉填充抗锯齿：相邻条带用同一个顶点（d1 完全相同），光栅化时共享边只画一次；
            // 开着抗锯齿则每段边缘各混一次 alpha，半透明叠加后就是一条条放射状亮纹（摩尔纹）。
            ImDrawListFlags fillFlags = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (int k = 0; k < seg; k++) {
                double d0 = sv + w * k / seg;
                double d1 = sv + w * (k + 1) / seg;
                ImVec2 o0 = ptAt(d0, surfR(d0)), o1 = ptAt(d1, surfR(d1));
                ImVec2 i0 = ptAt(d0, rIn), i1 = ptAt(d1, rIn);
                dl->AddQuadFilled(i0, o0, o1, i1, redFill);
            }
            dl->Flags = fillFlags;

            // 外弧描边
            dl->PathClear();
            for (int k = 0; k <= seg; k++) {
                double deg = sv + w * k / seg;
                dl->PathLineTo(ptAt(deg, surfR(deg)));
            }
            dl->PathStroke(red, 0, 2.2f);

            // 两条边界线 + 端点手柄
            ImVec2 pStart = ptAt(sv, surfR(sv));
            ImVec2 pEnd = ptAt(sv + w, surfR(sv + w));
            ImVec2 innerStart = ptAt(sv, rIn);
            ImVec2 innerEnd = ptAt(sv + w, rIn);
            dl->AddLine(innerStart, pStart, redDim, 1.4f);
            dl->AddLine(innerEnd, pEnd, redDim, 1.4f);

            bool active = (g_pvDragLm == i);
            dl->AddCircleFilled(pStart, active && g_pvDragEnd == 0 ? 7.f : 5.f, red);
            dl->AddCircleFilled(pEnd, active && g_pvDragEnd == 1 ? 7.f : 5.f, red);

            // 名字挤在角度中点
            FieldRow* fn = FindFieldRow(base + ".name");
            if (fn && fn->buf[0] && g_pvZoom < 6.f) {
                ImVec2 tp = ptAt(sv + w * 0.5, surfR(sv + w * 0.5) * 0.62f);
                dl->AddText(ImVec2(tp.x - 14.f, tp.y - 6.f), red, fn->buf);
            }

            float d0 = (mp.x - pStart.x) * (mp.x - pStart.x) + (mp.y - pStart.y) * (mp.y - pStart.y);
            float d1 = (mp.x - pEnd.x) * (mp.x - pEnd.x) + (mp.y - pEnd.y) * (mp.y - pEnd.y);
            if (d0 < hitDist * hitDist && d0 <= d1) { hitLm = i; hitEnd = 0; hitDist = sqrtf(d0); }
            if (d1 < hitDist * hitDist && d1 < d0) { hitLm = i; hitEnd = 1; hitDist = sqrtf(d1); }
        }
    }

    // 拖动地标端点
    if (!ImGui::IsMouseDown(0)) g_pvDragLm = -1;
    if (hovered && hitLm >= 0) {
        if (ImGui::IsMouseClicked(0)) { g_pvDragLm = hitLm; g_pvDragEnd = hitEnd; }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (g_pvDragLm >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        double deg = atan2((double)(ctr.y - mp.y), (double)(mp.x - ctr.x)) * 180.0 / PI;
        if (now - g_pvLastSet > 0.05) {
            g_pvLastSet = now;
            SetLandmarkAngle(g_pvDragLm, g_pvDragEnd, deg);
        }
    }

    // 左下角提示 + 右下角缩放值
    char zbuf[32];
    snprintf(zbuf, sizeof(zbuf), "%.1fx", g_pvZoom);
    dl->AddText(ImVec2(p.x + 8.f, p.y + H - 18.f), dim, TR("滚轮放大 中键平移"));
    dl->AddText(ImVec2(p.x + W - 44.f, p.y + H - 18.f), dim, zbuf);
    if (hovered) {
        double ha = atan2((double)(ctr.y - mp.y), (double)(mp.x - ctr.x)) * 180.0 / PI;
        if (ha < 0) ha += 360.0;
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "%.1f", ha);
        dl->AddText(ImVec2(p.x + W - 88.f, p.y + H - 18.f), dim, abuf);
    }

    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + H));
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
                    TR("选中星球后这里显示全周地形高度"));
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

    dl->AddText(ImVec2(q0.x + 6, q0.y + 2), IM_COL32(160, 172, 188, 255), TR("高度 (m)"));
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f m", g_viewMaxH);
        dl->AddText(ImVec2(q0.x + 6, q0.y + 20), IM_COL32(120, 132, 150, 220), buf);
        snprintf(buf, sizeof(buf), "%.0f m", g_viewMinH);
        dl->AddText(ImVec2(q0.x + 6, q1.y - 18), IM_COL32(120, 132, 150, 220), buf);
    }
    dl->AddText(ImVec2(q0.x + 6, q1.y - 38), IM_COL32(150, 162, 178, 220),
                TR("滚轮缩放 / Ctrl+滚轮纵向 / 中键平移 / 左键画 / 右键削"));

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##canvas", sz);
    bool hovered = ImGui::IsItemHovered();
    bool lmb = ImGui::IsMouseDown(0) && hovered;
    bool rmb = ImGui::IsMouseDown(1) && hovered;

    ImVec2 mp = ImGui::GetMousePos();
    bool inside = mp.x >= q0.x && mp.x <= q1.x && mp.y >= q0.y && mp.y <= q1.y;
    float angle = 0.f, targetH = 0.f, tx = 0.f;
    if (inside) {
        tx = (mp.x - q0.x) / (q1.x - q0.x);
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
        // 刚按下也要先落一笔，否则「点一下不动」看着像没反应
        bool pressed = ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1);
        if ((moved || pressed) && now - lastSend > 0.033 && !g_selected.empty()) {
            lastSend = now;
            char cmd[160];
            if (g_paintMode == 0) {
                float delta = g_rate * 0.033f * (rmb ? -1.f : 1.f);
                snprintf(cmd, sizeof(cmd), "PAINTAT %s %.6f %.2f", g_selected.c_str(), angle, (double)delta);
                SendCmd(cmd);
                Dbg("paint px=%.1f x0=%.1f tx=%.4f deg=%.2f delta=%.1f R=%.0f n=%d",
                    mp.x, q0.x, tx, angle * 57.29577951308232f, delta, g_shapeRadius, (int)g_shape.size());
                SendCmd("SHAPE " + g_selected);
            } else {
                double tgt = rmb ? 0.0 : (double)targetH;
                snprintf(cmd, sizeof(cmd), "PAINTTO %s %.6f %.2f 0.45", g_selected.c_str(), angle, tgt);
                SendCmd(cmd);
                Dbg("paintto px=%.1f deg=%.2f target=%.1f", mp.x, angle * 57.29577951308232f, tgt);
                SendCmd("SHAPE " + g_selected);
            }
        }
    }

    if (hovered) ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
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
    if (ImGui::Button(TR("< 返回"), ImVec2(88, 30))) g_view = 0;
    ImGui::SameLine();
    ImGui::Text(TR("%s   笔刷编辑"), g_selected.empty() ? TR("(未选星球)") : g_selected.c_str());

    ImGui::Spacing();
    DrawBigHeightMap(W, H * 0.40f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    SliderWithInput(TR("笔刷半径"), &g_brush, 10.f, 20000.f, "%.0f m", "CMD brush %.0f");
    ImGui::Spacing();
    SliderWithInputInt(TR("采样点"), &g_samples, 256, 8192, "CMD samples %d");
    ImGui::Spacing();
    if (SliderWithInput(TR("强度"), &g_rate, 10.f, 20000.f, "%.0f m/s", "CMD rate %.0f")) g_rateTouched = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (GlassButton(g_paint ? TR("绘制中（点此关闭）") : TR("开启笔刷"), ImVec2(190, 34), false, false, g_paint)) {
        SetPaint(!g_paint);
    }
    ImGui::SameLine();
    if (GlassButton(TR("撤销一笔"), ImVec2(110, 34))) SendCmd("CMD undo " + g_selected);
    ImGui::SameLine();
    if (GlassButton(TR("重做"), ImVec2(80, 34))) SendCmd("CMD redo " + g_selected);

    ImGui::Spacing();
    ImGui::TextUnformatted(TR("笔刷形状"));
    {
        const char* shapeNames[3] = { TR("圆滑"), TR("方头"), TR("尖峰") };
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
    ImGui::TextUnformatted(TR("绘制方式"));
    {
        const char* modeNames[2] = { TR("画笔（沿轨迹加减）"), TR("吸附（拉向光标）") };
        for (int i = 0; i < 2; i++) {
            if (i) ImGui::SameLine();
            if (GlassButton(modeNames[i], ImVec2(178, 30), false, false, g_paintMode == i)) g_paintMode = i;
        }
        ImGui::SameLine();
        if (GlassButton(TR("重置缩放"), ImVec2(104, 30))) { g_zoomX = 1.f; g_zoomY = 1.f; g_panX = 0.5f; }
        ImGui::SameLine();
        if (GlassButton(TR("适应视图"), ImVec2(104, 30))) { g_viewRangeInit = false; }
        ImGui::SameLine();
        if (g_confirmClear) {
            if (GlassButton(TR("确认清除？"), ImVec2(118, 30), false, true)) { SendCmd("CMD clear " + g_selected); g_confirmClear = false; }
            ImGui::SameLine();
            if (GlassButton(TR("取消"), ImVec2(64, 30))) g_confirmClear = false;
        } else {
            if (GlassButton(TR("还原原始地形"), ImVec2(118, 30))) g_confirmClear = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(TR("导出高度图（PNG）"));
    ImGui::TextUnformatted(TR("宽"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##expw", &g_expW, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("高"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##exph", &g_expH, 0, 0);
    if (g_expW < 16) g_expW = 16;  if (g_expW > 8192) g_expW = 8192;
    if (g_expH < 16) g_expH = 16;  if (g_expH > 8192) g_expH = 8192;
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("填充色"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::ColorEdit3("##expcol", g_expColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    if (GlassButton(TR("导出 PNG"), ImVec2(110, 28))) {
        g_wantFine = true;
        SendCmd("CMD shapefine " + g_selected + " 4096");
        snprintf(g_expMsg, sizeof(g_expMsg), TR("正在获取高精度轮廓…"));
    }
    if (g_expMsg[0]) ImGui::TextDisabled("%s", g_expMsg);
    ImGui::TextDisabled(TR("地形以下填纯色，以上为全透明（游戏按有无像素判定地形）"));


    ImGui::Spacing();
    ImGui::TextDisabled(TR("游戏窗口里也能刷：左键抬高 右键压低"));
    ImGui::TextDisabled(TR("采样点 = 笔刷曲线精度（越大越细腻）"));
    ImGui::TextDisabled(TR("Ctrl+Z 撤销 / Ctrl+Y 重做 / Ctrl+S 保存"));
}

// ---------------- 左侧笔刷功能区 ----------------
static void DrawBrushPanel() {
    ImGui::Separator();
    ImGui::TextUnformatted(TR("笔刷"));

    ImGui::PushStyleColor(ImGuiCol_Button,
        g_paint ? ImVec4(0.35f, 0.65f, 1.f, 0.55f) : ImVec4(1, 1, 1, 0.08f));
    if (ImGui::Button(g_paint ? TR("绘制中") : TR("开启笔刷"), ImVec2(104, 28))) {
        SetPaint(!g_paint);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(TR("撤销一笔"), ImVec2(96, 28))) SendCmd("CMD undo " + g_selected);

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

    ImGui::TextDisabled(TR("游戏窗口：左键抬高 / 右键压低"));
    ImGui::TextDisabled(TR("点上面的高度图可在这里直接画"));
}


// 改名 / 删除星球 的弹窗（半透明风格，无标题栏，按钮居中）
// ---------------- 包信息 / 备份 / 存模板 ----------------
static void DrawPackInfo() {
    if (g_packInfoOpen) {
        ImGui::OpenPopup("##packinfo");
        g_packInfoOpen = false;
        SendCmd("CMD getmeta");
        SendCmd("CMD backups");
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.08f, 0.11f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    if (ImGui::BeginPopupModal("##packinfo", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const float PW = 380.f;
        ImGui::Dummy(ImVec2(PW, 0.f));
        ImGui::Text(TR("包信息"));
        ImGui::Spacing();
        ImGui::TextDisabled(TR("作者"));
        ImGui::SetNextItemWidth(PW - 8.f);
        ImGui::InputText("##author", g_authorBuf, sizeof(g_authorBuf));
        ImGui::TextDisabled(TR("版本"));
        ImGui::SetNextItemWidth(PW - 8.f);
        ImGui::InputText("##ver", g_verBuf, sizeof(g_verBuf));
        ImGui::TextDisabled(TR("描述"));
        ImGui::SetNextItemWidth(PW - 8.f);
        ImGui::InputText("##desc", g_descBuf, sizeof(g_descBuf));
        ImGui::Spacing();
        if (ImGui::Button(TR("保存信息"), ImVec2(120, 30))) {
            SendCmd(std::string("CMD setmeta author ") + g_authorBuf);
            SendCmd(std::string("CMD setmeta version ") + g_verBuf);
            SendCmd(std::string("CMD setmeta description ") + g_descBuf);
            LogLine("pack meta saved");
        }
        ImGui::Separator();
        ImGui::Text(TR("备份"));
        ImGui::Spacing();
        if (ImGui::Button(TR("打快照"), ImVec2(110, 28))) { SendCmd("CMD backup"); }
        ImGui::SameLine();
        if (ImGui::Button(TR("恢复最近"), ImVec2(110, 28))) { SendCmd("CMD restore"); }
        ImGui::SameLine();
        if (ImGui::Button(TR("导出 zip"), ImVec2(110, 28))) { SendCmd("CMD exportzip"); }
        if (g_backups.empty()) {
            ImGui::TextDisabled(TR("还没有快照（每次保存星球会自动留上一版）"));
        } else {
            for (int i = 0; i < (int)g_backups.size() && i < 5; i++) {
                if (ImGui::Selectable(g_backups[i].c_str(), false, 0, ImVec2(PW - 8.f, 22)))
                    SendCmd("CMD restore " + g_backups[i]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("点一下恢复这个快照"));
            }
        }
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (PW - total) * 0.5f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
        if (ImGui::Button(TR("关闭"), ImVec2(120, 32))) ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(TR("重读包"), ImVec2(120, 32))) {
            SendCmd("CMD reload");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

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
        ImGui::Text(TR("把「%s」改名为："), g_selected.c_str());
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
        if (ImGui::Button(TR("确定"), ImVec2(120, 32))) {
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
        if (ImGui::Button(TR("取消"), ImVec2(120, 32))) ImGui::CloseCurrentPopup();
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
        ImGui::Text(TR("确定要删除「%s」吗？"), g_selected.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled(TR("星球文件会从包里删掉，运行时也会移除。"));
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float total = 120.f * 2.f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (PW - total) * 0.5f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.3f, 0.26f, 0.55f));
        if (ImGui::Button(TR("确定删除"), ImVec2(120, 32))) {
            SendCmd("CMD delplanet " + g_selected + " live");
            LogLine("delplanet " + g_selected);
            g_pendingRefresh = 0.6f;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(TR("取消"), ImVec2(120, 32))) ImGui::CloseCurrentPopup();
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
    UpdateTitleDrag();
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
    float bodyH = h - 118.f;          // 底下留给日志；窗口很矮时别算成负数
    if (bodyH < 160.f) bodyH = 160.f;
    if (g_view == 0) {
    ImGui::BeginChild("##left", ImVec2(316, bodyH), ImGuiChildFlags_None);
    DrawPlanetList();
    DrawPreview();
    DrawBrushPanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, bodyH), ImGuiChildFlags_None);
    DrawFields();
    ImGui::EndChild();

    DrawLog();
    DrawPlanetDialogs();
    DrawPackInfo();
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
    LoadLang();
    // 测试时可换端口，免得跟正在运行的游戏抢 41729
    const char* ep = getenv("WORLDCREATER_PORT");
    if (ep) { int p = atoi(ep); if (p > 0 && p < 65536) g_port = p; }
    DbgOpen();
    SetUnhandledExceptionFilter(DbgCrash);
    Dbg("start port=%d", g_port);
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
            if (msg.message == WM_QUIT) { Dbg("wm_quit"); running = false; }
        }
        if (!running) break;
        { static int fc = 0; if (++fc % 300 == 0) Dbg("frame %d", fc); }

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
            // 正在收轮廓时别再发（重发会把缓冲清掉重来，慢一点的机器永远收不满）
            if (!g_selected.empty() && g_bridge.Connected() && g_pending_kind != 3 &&
                ImGui::GetTime() - lastShapeReq > 1.0) {
                lastShapeReq = ImGui::GetTime();
                RequestShape();
            }
        }
        // 断线自动重连（去掉了TR("连接")按钮，靠这个兜底）
        {
            static double lastTry = 0.0;
            static int failCount = 0;
            if (!g_bridge.Connected()) {
                // 游戏没开时别一直撞端口，连续失败后拉长间隔
                double interval = failCount >= 3 ? 5.0 : 2.0;
                if (ImGui::GetTime() - lastTry > interval) {
                    lastTry = ImGui::GetTime();
                    if (g_bridge.Connect(g_port)) {
                        failCount = 0;
                        g_status = TR("已连接 :") + std::to_string(g_port);
                        LogLine("reconnected");
                        SendCmd("STATUS");
                        RequestPlanets();
                    } else {
                        failCount++;
                        g_status = TR("等待游戏端 (127.0.0.1:") + std::to_string(g_port) + ")";
                    }
                }
            } else {
                failCount = 0;
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
        // 没连上时没有实时数据要刷，降到约 30fps，少占 CPU 和 GPU
        if (!g_bridge.Connected()) Sleep(16);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_glass.Shutdown();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    return 0;
}
