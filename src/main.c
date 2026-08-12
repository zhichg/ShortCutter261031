#include "main.h"
#include "resource.h"

/* ── 全局变量 ─────────────────────────────── */
AppConfig g_cfg;
int       g_enabled = 1;
HINSTANCE g_hInst;
HWND      g_hWnd;
HMENU     g_hTrayMenu;
int       g_dpiScale = 96;
wchar_t   g_exeDir[MAX_PATH];

/* ── 辅助 ─────────────────────────────────── */
const wchar_t *GetExeDir(void) { return g_exeDir; }
const wchar_t *GetIniPath(void)
{
    static wchar_t path[MAX_PATH];
    lstrcpyW(path, g_exeDir);
    lstrcatW(path, L"\\ShortCutter.ini");
    return path;
}

/* ── 托盘菜单 ─────────────────────────────── */
static void Tray_ShowMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    TrackPopupMenu(g_hTrayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
}

static void Toggle_Notify(int on)
{
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = g_hWnd;
    nid.uID    = ID_TRAYICON;
    nid.uFlags = NIF_INFO | NIF_TIP | NIF_ICON;
    wcscpy(nid.szTip, on ? L"ShortCutter - ON" : L"ShortCutter - OFF");
    nid.hIcon  = LoadIconW(g_hInst, MAKEINTRESOURCEW(on ? IDI_APP : IDI_APP_GRAY));
    nid.dwInfoFlags = NIIF_INFO;
    wcscpy(nid.szInfo, on ? L"Gestures & hotkeys enabled" : L"Gestures & hotkeys disabled");
    wcscpy(nid.szInfoTitle, on ? L"ShortCutter ON" : L"ShortCutter OFF");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (g_hTrayMenu)
        CheckMenuItem(g_hTrayMenu, IDM_TOGGLE, on ? MF_CHECKED : MF_UNCHECKED);
}

/* ── 快捷键 ────────────────────────────────── */
static UINT ScModsToWin(BYTE scMods)
{
    UINT w = 0;
    if (scMods & SC_MOD_CTRL)  w |= MOD_CONTROL;
    if (scMods & SC_MOD_ALT)   w |= MOD_ALT;
    if (scMods & SC_MOD_SHIFT) w |= MOD_SHIFT;
    if (scMods & SC_MOD_WIN)   w |= MOD_WIN;
    return w;
}

/* ── 键盘钩子（替代 RegisterHotKey，不占坑）── */
static HHOOK  g_hKbHook = NULL;
static BYTE   g_hkLastVk = 0;
static int    g_hkFired  = 0;  /* 防重复触发 */

static int IsModVk(DWORD vk)
{
    return (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
            vk == VK_LWIN || vk == VK_RWIN ||
            vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LMENU || vk == VK_RMENU);
}

static LRESULT CALLBACK KbHookProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode < 0) return CallNextHookEx(NULL, nCode, wp, lp);

    KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lp;
    /* 跳过注入事件 */
    if (kb->flags & LLKHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wp, lp);

    BYTE vk = (BYTE)kb->vkCode;

    /* 弹框可见但失焦时转发按键（应对高权限窗口 UIPI 隔离） */
    if (Popup_IsVisible() && !Popup_HasFocus()) {
        if ((wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && !IsModVk(vk)) {
            Popup_ForwardKey(vk, kb->scanCode, kb->flags);
            return 1;
        }
        if ((wp == WM_KEYUP || wp == WM_SYSKEYUP) && !IsModVk(vk))
            return 1;
    }

    /* 仪表盘可见但失焦时转发按键 */
    if (Dashboard_IsVisible() && !Dashboard_HasFocus()) {
        if ((wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && !IsModVk(vk)) {
            Dashboard_ForwardKey(vk, kb->scanCode, kb->flags);
            return 1;
        }
        if ((wp == WM_KEYUP || wp == WM_SYSKEYUP) && !IsModVk(vk))
            return 1;
    }

    if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
        if (vk == g_hkLastVk) { g_hkLastVk = 0; g_hkFired = 0; }
        return CallNextHookEx(NULL, nCode, wp, lp);
    }

    if (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN)
        return CallNextHookEx(NULL, nCode, wp, lp);

    if (!g_enabled)
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* 跳过纯修饰键按下 */
    if (IsModVk(vk))
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* 防重复：同一按键未释放前不再次触发 */
    if (g_hkFired && vk == g_hkLastVk)
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* 收集当前修饰键 */
    BYTE mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= SC_MOD_CTRL;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= SC_MOD_ALT;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= SC_MOD_SHIFT;
    if (GetKeyState(VK_LWIN)    & 0x8000) mods |= SC_MOD_WIN;
    if (GetKeyState(VK_RWIN)    & 0x8000) mods |= SC_MOD_WIN;

    /* 匹配用户热键 */
    for (int i = 0; i < g_cfg.hotkeyCount && i < MAX_HOTKEYS; i++) {
        HotkeyDef *hk = &g_cfg.hotkeys[i];
        if (!hk->enabled) continue;
        if (hk->vk != vk) continue;
        if (hk->mods != mods) continue;
        /* 命中：PostMessage 延迟到主循环执行（避免钩子中 SetFocus 失效） */
        g_hkLastVk = vk;
        g_hkFired  = 1;
        PostMessageW(g_hWnd, WM_APP + 300, hk->commandId, 0);
        break;
    }

    /* 始终透传按键，不阻止其他应用 */
    return CallNextHookEx(NULL, nCode, wp, lp);
}

/* ── Toggle 开关热键（RegisterHotKey，始终占坑）─ */
void Hotkeys_RegisterToggle(void)
{
    UnregisterHotKey(g_hWnd, HKID_TOGGLE);
    if (g_cfg.toggleMods != 0 || g_cfg.toggleVk != 0) {
        UINT mods = ScModsToWin(g_cfg.toggleMods);
        RegisterHotKey(g_hWnd, HKID_TOGGLE, mods, g_cfg.toggleVk);
    }
}

/* ── 用户热键钩子（始终运行，按 g_enabled 决定是否触发）─ */
void Hotkeys_RegisterUser(void)
{
    if (!g_hKbHook) {
        g_hKbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc,
                                       GetModuleHandleW(NULL), 0);
    }
}

static void Hotkeys_UnregisterUser(void)
{
    if (g_hKbHook) {
        UnhookWindowsHookEx(g_hKbHook);
        g_hKbHook = NULL;
    }
    g_hkLastVk = 0;
    g_hkFired  = 0;
}

void Hotkeys_Register(void)
{
    Hotkeys_RegisterToggle();
    Hotkeys_RegisterUser();
}

void Hotkeys_Unregister(void)
{
    UnregisterHotKey(g_hWnd, HKID_TOGGLE);
    Hotkeys_UnregisterUser();
}

/* ── 窗口过程 ─────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        NOTIFYICONDATAW nid = {0};
        nid.cbSize           = sizeof(NOTIFYICONDATAW);
        nid.hWnd             = hWnd;
        nid.uID              = ID_TRAYICON;
        nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon            = LoadIconW(g_hInst, MAKEINTRESOURCEW(g_enabled ? IDI_APP : IDI_APP_GRAY));
        wcscpy(nid.szTip, APP_TITLE);
        Shell_NotifyIconW(NIM_ADD, &nid);
        return 0;
    }

    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP) Tray_ShowMenu(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE:
            g_enabled = !g_enabled;
            g_cfg.enabled = g_enabled;
            Config_Save();
            Toggle_Notify(g_enabled);
            return 0;
        case IDM_SETTINGS:
            UI_Show(hWnd);
            return 0;
        case IDM_DASHBOARD:
            Dashboard_Show();
            return 0;
        case IDM_EXIT:
            Config_Save();
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_HOTKEY: {
        if (wp == HKID_TOGGLE) {
            g_enabled = !g_enabled;
            g_cfg.enabled = g_enabled;
            Config_Save();
            Toggle_Notify(g_enabled);
            return 0;
        }
        /* 用户热键已改为键盘钩子处理，不再走 WM_HOTKEY */
        return 0;
    }

    case WM_APP + 300:  /* 键盘钩子延迟执行命令（避免钩子中 UI 操作失效） */
        Action_ExecuteCommand((int)wp);
        return 0;

    case WM_SC_MATCH: {
        if (!g_enabled) return 0;
        int chainLen = (int)wp & 0xFF;
        if (chainLen == 0) {
            /* 普通点击：异步合成正常点击事件 */
            int btn = ((int)lp >> 24) & 0xFF;
            INPUT in[2] = {0};
            in[0].type = INPUT_MOUSE;
            in[0].mi.dwFlags = (btn == 2) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_RIGHTDOWN;
            in[1].type = INPUT_MOUSE;
            in[1].mi.dwFlags = (btn == 2) ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_RIGHTUP;
            SendInput(2, in, sizeof(INPUT));
        } else {
            /* 手势匹配执行 */
            int zone = ((int)lp >> 8) & 0xFF;
            int mod = ((int)lp >> 16) & 0xFF;
            extern char g_pendChain[64];
            int match = Gesture_Match(g_pendChain, chainLen, mod, zone);
            if (match >= 0)
                Action_ExecuteCustom(&g_cfg.gestures[match]);
        }
        return 0;
    }

    case WM_DESTROY: {
        Config_Save();
        NOTIFYICONDATAW nid = {0};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd   = hWnd;
        nid.uID    = ID_TRAYICON;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ── 入口 ─────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd; (void)nShow;

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ShortCutter_SingleInstance_8F3A");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    g_hInst = hInst;
    SetProcessDPIAware(); /* 轨迹窗口需要物理坐标一致 */
    { HDC hdc = GetDC(NULL); g_dpiScale = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(NULL, hdc); }

    /* exe 目录 */
    {
        char tmpA[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, tmpA, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            char *pA = strrchr(tmpA, '\\');
            if (pA) *pA = 0;
            MultiByteToWideChar(CP_ACP, 0, tmpA, -1, g_exeDir, MAX_PATH);
        } else {
            GetCurrentDirectoryW(MAX_PATH, g_exeDir);
        }
    }

    Config_SetDefaults();
    Config_Load();
    g_enabled = g_cfg.enabled;
    Config_AutoStart(g_cfg.autoStart);

    /* 首次运行时生成文档 */
    { /* README.md — 内容与磁盘 README.md 一致 */
      wchar_t rpath[MAX_PATH]; lstrcpyW(rpath, g_exeDir); lstrcatW(rpath, L"\\README.md");
      if (GetFileAttributesW(rpath) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = _wfopen(rpath, L"wb");
        if (f) { wchar_t bom = 0xFEFF; fwrite(&bom, sizeof(wchar_t), 1, f);
          const wchar_t *txt = L"# ShortCutter 3.0 — 鼠标手势工具\r\n\r\n## 概述\r\n\r\nShortCutter 是一款 Windows 全局鼠标手势工具。按住鼠标右键（或中键）绘制任意形状手势，即可触发对应命令。同时支持全局热键、命令弹框启动器、脚本扩展、已装软件一键导入。纯 C + Win32 API，单 exe 静态链接，无外部依赖。\r\n\r\n## 核心功能\r\n\r\n| 功能 | 说明 |\r\n|------|------|\r\n| **自定义手势** | 录制任意形状轨迹，方向链编码 + LCS 匹配，支持多点采样提高识别率 |\r\n| **区域手势** | 屏幕分 5 区（四角 + 主区），同一手势不同位置绑不同命令 |\r\n| **实时轨迹** | 拖动时显示蓝色轨迹线，可选绿色分区边界线 |\r\n| **内置命令** | 27 个：窗口管理/标签切换 (Built-in:Keys) + 系统工具 (Built-in:Run) |\r\n| **Scan Apps** | Commands > Scan Apps... 扫描桌面和开始菜单快捷方式，勾选一键导入 |\r\n| **仪表盘** | Windows 风格仪表盘弹窗，搜索/常用排行/自定义 Dock，拖拽排列 |\r\n| **全局热键** | 独立快捷键绑定，直接触发命令 |\r\n| **命令弹框** | 输入关键字搜索执行：内置指令 → 脚本 → Commands 列表 |\r\n| **脚本扩展** | `scripts\\` 内 `.bat`/`.cmd`/`.ps1`/`.vbs` 自动注册，默认静默执行 |\r\n| **脚本窗口控制** | 文件名以 `_Show` 结尾 → 显示控制台，其余隐藏 |\r\n| **脚本改名同步** | Settings 中修改脚本 Name → `MoveFile` 同步重命名实际文件 |\r\n| **命令分组** | 每个命令可分配 Group（Built-in:Keys / Built-in:Run / Scripts / 自定义） |\r\n| **筛选系统** | Commands 标签页 Type + Group 双层筛选，Gestures 标签页 Zone 筛选 |\r\n| **单实例** | 同名互斥体保证只运行一个进程 |\r\n| **DPI 感知** | 手势轨迹自适应 DPI，高分辨率屏正常显示 |\r\n| **开机自启** | 可选 HKCU Run 注册表启动项 |\r\n\r\n## 文件结构\r\n\r\n```\r\nShortCutter\\\r\n├── ShortCutter.exe       ← 编译产物，独立运行（~200KB 静态链接）\r\n├── ShortCutter.ini       ← UTF-16 LE 编码，保存所有配置\r\n├── README.md             ← 首次运行时自动生成\r\n└── scripts\\              ← 脚本扩展目录（自动创建）\r\n    ├── AI_PROMPT.md      ← AI 编写脚本的指南（首次运行自动生成）\r\n    ├── Display.bat       ← 打开显示设置\r\n    └── run.ps1           ← 打开运行对话框\r\n```\r\n\r\n## 源码结构\r\n\r\n```\r\nsrc\\\r\n├── main.h        ← 所有公共定义：常量、枚举、结构体、全局变量声明\r\n├── main.c        ← WinMain 入口、托盘图标、消息循环、全局热键、文档生成\r\n├── gesture.c     ← 鼠标钩子（WH_MOUSE_LL）、方向链编码、LCS 匹配、轨迹窗口、区域判定\r\n├── actions.c     ← 动作执行：命令弹框、脚本扫描、内置指令、按键序列\r\n├── config.c      ← INI 配置读写（UTF-16 LE）、预置命令创建、脚本同步\r\n├── dashboard.c   ← 仪表盘弹窗：搜索、常用排行、自定义命令 Dock\r\n├── ui.c          ← 设置对话框（4 标签页）、录制弹窗、Help 弹窗、Scan Apps 弹窗\r\n├── resource.rc   ← 版本信息、图标资源\r\n└── resource.h    ← 资源 ID\r\n```\r\n\r\n## 关键数据结构\r\n\r\n```\r\nAppConfig\r\n├── Command commands[100]       ← 独立命令列表\r\n│   ├── id, name, group\r\n│   ├── CmdType type            ← CMD_RUN | CMD_KEYS | CMD_SCRIPT\r\n│   ├── runPath[512]            ← 程序路径或脚本路径\r\n│   ├── keySeq[512]             ← 按键序列\r\n│   └── locked                  ← 1=不可删除\r\n├── GestureDef gestures[100]    ← 自定义手势列表\r\n│   ├── name, modifier, zone, enabled\r\n│   ├── chains[3][64]           ← 多采样方向链\r\n│   └── ptsStr[3][1024]         ← 采样点数据\r\n├── HotkeyDef hotkeys[100]      ← 独立快捷键\r\n│   ├── mods, vk, label, description\r\n│   └── commandId, enabled\r\n├── 设置项：minGesture, matchThreshold, mouseButton\r\n│           trailEnabled, zoneLinesEnabled, autoStart, enabled\r\n└── toggleMods, toggleVk, toggleLabel  ← 开关快捷键\r\n```\r\n\r\n## 手势识别算法\r\n\r\n**方向链编码（Direction Chain Encoding）**：\r\n\r\n```\r\n原始轨迹 → 降采样 → 逐段量化到 8 方向 → 合并连续重复 → 方向链字符串\r\n例：画 \"W\" → 先右下、再右上、再右下 → 方向链 \"515\"\r\n```\r\n\r\n**匹配**：LCS（最长公共子序列）计算输入链与模板链的相似度百分比，达到 matchThreshold（默认 70%）即匹配。\r\n\r\n**自适应降采样**：降采样距离 = max(包围盒宽, 包围盒高) / 20，限制范围 [5, 50]px。\r\n\r\n## 区域划分\r\n\r\n```\r\n┌──────────┬──────────┬──────────┐\r\n│ ↖ TL     │          │ ↗ TR     │\r\n│  (1)     │   Main   │  (2)     │\r\n│          │   (0)    │          │\r\n├──────────┤          ├──────────┤  ← 1/3 屏幕高\r\n│          │          │          │\r\n│          │          │          │\r\n├──────────┤          ├──────────┤  ← 2/3 屏幕高\r\n│ ↙ BL     │          │ ↘ BR     │\r\n│  (3)     │   Main   │  (4)     │\r\n│          │   (0)    │          │\r\n└──────────┴──────────┴──────────┘\r\n```\r\n\r\n匹配优先级：精确 zone 匹配 > Main(zone=0) 作为 fallback。\r\n\r\n## 按键序列格式\r\n\r\n```\r\n{Key down}   按下并保持\r\n{Key up}     释放\r\n{Key}        按下并释放（默认）\r\n{Sleep N}    延迟 N 毫秒\r\n\r\n支持: Ctrl Alt Shift Win Tab Enter Space Esc Backspace Delete\r\n      Insert Home End PgUp PgDn Up Down Left Right\r\n      F1-F12 Apps PrtSc A-Z 0-9\r\n```\r\n\r\n例：`{Ctrl down}{C}{Ctrl up}` = Ctrl+C\r\n\r\n## 仪表盘 (Dashboard)\r\n\r\nShortCutter 3.0 新增仪表盘弹窗，类似 Windows 开始菜单风格，提供命令搜索、使用统计和自定义快捷方式。左栏搜索框+历史/匹配列表，右栏最常使用 Top 8，下半区 6x4 图标网格拖拽排列。双击 Custom 标签添加推荐列表高亮项，右键图标移除。退出时自动保存布局和使用统计。\r\n\r\n## 脚本系统\r\n\r\n### 命名约定\r\n\r\n| 文件名 | 效果 |\r\n|--------|------|\r\n| `run.ps1` | 静默执行，无窗口 |\r\n| `Ping_Show.ps1` | 显示 PowerShell 窗口 |\r\n| `Backup.bat` | 静默执行 |\r\n| `Install_Show.cmd` | 显示 CMD 窗口 |\r\n\r\n### 改名同步\r\n\r\nSettings → Commands → 选中脚本 → Edit → 修改 Name → OK。实际文件被 `MoveFile` 重命名，扩展名不变。\r\n\r\n## 配置文件（ShortCutter.ini）\r\n\r\nUTF-16 LE 编码，包含 `[Settings]` `[Commands]` `[Gestures]` `[Hotkeys]` `[Dashboard]` `[Usage]` `[History]` 七个 section。保存时全量重写，加载时自动增量补充缺省的预置命令。\r\n\r\n## 构建方式\r\n\r\n- **编译器**：MinGW-w64 gcc（静态链接）\r\n- **依赖**：仅 Win32 API（`comctl32 gdi32 user32 kernel32 shell32 imm32 advapi32 ole32 uuid`）\r\n- **编译**：\r\n  ```\r\n  windres src/resource.rc -o src/resource.o\r\n  gcc -static -Os -s -Wall -Wextra -o ShortCutter.exe src/*.c src/resource.o \\\r\n      -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -ladvapi32 -lole32 -luuid -mwindows\r\n  ```\r\n- **产物**：约 200KB，无额外 DLL\r\n\r\n## 设计原则\r\n\r\n- **零依赖**：纯 Win32 API，单 exe 文件即可运行\r\n- **无管理员权限**：配置写在 exe 同目录 INI，注册表仅用于开机自启（可选）\r\n- **UTF-8 内部存储**：所有文本字段存为 UTF-8（char[]），UI 用 Wide API 做转换\r\n- **进程去重**：单实例互斥体，重复启动静默退出\r\n- **静默优先**：脚本默认隐藏控制台窗口，GUI 程序不受影响\r\n";
          fwrite(txt, sizeof(wchar_t), wcslen(txt), f); fclose(f); } }
      /* scripts 目录 */
      wchar_t sdir[MAX_PATH]; lstrcpyW(sdir, g_exeDir); lstrcatW(sdir, L"\\scripts");
      CreateDirectoryW(sdir, NULL);
      /* AI_PROMPT.md — 内容与磁盘 AI_PROMPT.md 一致 */
      wchar_t apath[MAX_PATH]; lstrcpyW(apath, sdir); lstrcatW(apath, L"\\AI_PROMPT.md");
      if (GetFileAttributesW(apath) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = _wfopen(apath, L"wb");
        if (f) { wchar_t bom = 0xFEFF; fwrite(&bom, sizeof(wchar_t), 1, f);
          const wchar_t *txt = L"# ShortCutter 3.0 — AI 脚本编写指南\r\n\r\n## 工作原理\r\n\r\nShortCutter 启动时会扫描 `scripts\\` 文件夹，**文件名（去掉扩展名）即命令名**。Commands 标签页中自动注册为 `CMD_SCRIPT` 类型的命令（Group=\"Scripts\"，不可删除）。\r\n\r\n```\r\nscripts\\\r\n├── run.ps1           → 命令名 run     → 打开运行对话框\r\n├── backup.bat         → 命令名 backup → 执行备份\r\n├── Ping_Show.ps1     → 命令名 Ping_Show → ping 并显示窗口\r\n└── vpn.cmd            → 命令名 vpn    → 连接 VPN\r\n```\r\n\r\n## 脚本规范\r\n\r\n1. **静默执行** — ShortCutter 默认带 `-WindowStyle Hidden` / `SW_HIDE` 执行脚本（无黑框）。如需启动 GUI 程序，用 `start \"\" \"程序名\"`（bat）或 `Start-Process`（ps1）\r\n2. **窗口可见** — 文件名以 `_Show` 结尾（如 `Ping_Show.ps1`）→ 显示控制台窗口\r\n3. **无交互** — 不要 `pause`、`Read-Host`、弹确认框\r\n4. **快速完成** — 避免长时间阻塞，耗时操作考虑后台启动\r\n5. **独立自足** — 不依赖工作目录，路径用绝对路径或 `%~dp0` / `$PSScriptRoot`\r\n6. **幂等友好** — 重复执行不造成副作用\r\n\r\n## 改名同步\r\n\r\nSettings → Commands → Edit → 修改 Name 会通过 `MoveFile` 同步重命名实际脚本文件，扩展名保持不变。命令 ID 和绑定关系不受影响。\r\n\r\n## 支持的脚本类型\r\n\r\n| 扩展名 | 执行方式 | 默认窗口 |\r\n|--------|----------|:---:|\r\n| `.bat` | `ShellExecuteW` (open) | 隐藏 |\r\n| `.cmd` | `ShellExecuteW` (open) | 隐藏 |\r\n| `.ps1` | `powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File \"...\"` | 隐藏 |\r\n| `.vbs` | `ShellExecuteW` (open) | 隐藏 |\r\n\r\n> 文件名以 `_Show` 结尾（扩展名之前）→ 跳过 `-WindowStyle Hidden`，用 `SW_SHOW` 显示窗口。\r\n\r\n## 示例\r\n\r\n### .bat — 打开计算器\r\n```bat\r\nstart \"\" calc.exe\r\n```\r\n\r\n### .ps1 — 打开显示设置\r\n```powershell\r\nStart-Process \"ms-settings:display\"\r\n```\r\n\r\n### .ps1 — 显示窗口（_Show 后缀）\r\n```powershell\r\n# Ping_Show.ps1 — 文件名 _Show 后缀，窗口可见\r\nparam($hostname = \"8.8.8.8\")\r\nWrite-Host \"Pinging $hostname...\" -Foreground Cyan\r\nTest-Connection -ComputerName $hostname -Count 4\r\nWrite-Host \"Done.\" -Foreground Green\r\n```\r\n\r\n### .bat — 用默认浏览器打开网址\r\n```bat\r\nstart \"\" https://example.com\r\n```\r\n\r\n### .ps1 — 打开 Windows 运行对话框\r\n```powershell\r\n(New-Object -ComObject \"Shell.Application\").FileRun()\r\n```\r\n\r\n### .vbs — 创建快捷方式\r\n```vbs\r\nSet ws = CreateObject(\"WScript.Shell\")\r\nSet sc = ws.CreateShortcut(ws.SpecialFolders(\"Desktop\") & \"\\MyApp.lnk\")\r\nsc.TargetPath = \"D:\\tools\\myapp.exe\"\r\nsc.Save\r\n```\r\n\r\n## 注意事项\r\n\r\n- 同名文件只保留先扫描到的，避免重名\r\n- 脚本放入 `scripts\\` 后打开 Settings 窗口自动生效（`Scripts_ScanAndSync` 在刷新时调用）\r\n- `.ps1` 执行策略已设为 `Bypass`\r\n- 脚本命令在 Commands 列表中以 `*` 标记，不可手动删除（删除文件即移除命令）\r\n- GUI 程序不受 `_Show` 后缀影响——`start \"\"` 和 `Start-Process` 启动的窗口始终可见\r\n";
          fwrite(txt, sizeof(wchar_t), wcslen(txt), f); fclose(f); } } }

    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX),
                                  ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES |
                                  ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, APP_NAME, APP_TITLE, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, hInst, NULL);

    Hotkeys_RegisterToggle();   /* 开关热键始终注册 */
    Hotkeys_RegisterUser();     /* 键盘钩子始终运行，按 g_enabled 决定是否触发 */

    g_hTrayMenu = CreatePopupMenu();
    AppendMenuW(g_hTrayMenu, MF_STRING | (g_enabled ? MF_CHECKED : 0),
                IDM_TOGGLE, L"Enabled");
    AppendMenuW(g_hTrayMenu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(g_hTrayMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_hTrayMenu, MF_STRING, IDM_EXIT, L"Exit");

    Gesture_Start();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Hotkeys_Unregister();
    Gesture_Stop();
    DestroyMenu(g_hTrayMenu);
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
