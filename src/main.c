#include "main.h"
#include "resource.h"

AppConfig g_cfg;
int g_enabled = 1;
HINSTANCE g_hInst;
HWND g_hWnd;
HMENU g_hTrayMenu;
int g_dpiScale = 96;
wchar_t g_exeDir[SC_PATH_CAP];
static HHOOK g_hKbHook;
static BYTE g_hkDown[256];
static int g_toggleRegistered, g_shuttingDown;
static UINT g_taskbarCreated;

const wchar_t *GetExeDir(void) { return g_exeDir; }
const wchar_t *GetIniPath(void)
{
    static wchar_t path[SC_PATH_CAP+32];
    swprintf(path,sizeof(path)/sizeof(path[0]),L"%ls\\ShortCutter.ini",g_exeDir);
    return path;
}
static void SC_AppendLog(const wchar_t *line)
{
    wchar_t path[SC_PATH_CAP+64];
    OutputDebugStringW(line);
    if (g_exeDir[0]) {
        swprintf(path,sizeof(path)/sizeof(path[0]),L"%ls\\ShortCutter.log",g_exeDir);
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetFileAttributesExW(path,GetFileExInfoStandard,&attributes) &&
            (attributes.nFileSizeHigh || attributes.nFileSizeLow >= 1024*1024)) {
            wchar_t previous[SC_PATH_CAP+64];
            swprintf(previous,sizeof(previous)/sizeof(previous[0]),L"%ls\\ShortCutter.previous.log",g_exeDir);
            MoveFileExW(path,previous,MOVEFILE_REPLACE_EXISTING);
        }
        FILE *file = _wfopen(path,L"ab");
        if (file) {
            char text[4096];
            int count = WideCharToMultiByte(CP_UTF8,0,line,-1,text,sizeof(text),NULL,NULL);
            if (count > 0) fwrite(text,1,(size_t)count-1,file);
            fclose(file);
        }
    }
}
void SC_LogDiagnostic(const wchar_t *message)
{
    wchar_t line[1400]; SYSTEMTIME time; GetLocalTime(&time);
    swprintf(line,1400,L"[%04u-%02u-%02u %02u:%02u:%02u] %.1280ls\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,message ? message : L"");
    SC_AppendLog(line);
}
void SC_ReportError(const wchar_t *operation, DWORD error)
{
    wchar_t detail[256] = L"", line[1024];
    if (!error) error = ERROR_GEN_FAILURE;
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error,0,detail,256,NULL))
        swprintf(detail,256,SC_Text(L"Error %lu",L"错误 %lu"),(unsigned long)error);
    size_t length = wcslen(detail);
    while (length && (detail[length-1] == L'\r' || detail[length-1] == L'\n')) detail[--length] = 0;
    swprintf(line,1024,L"%ls (error=%lu): %ls",operation ? operation : L"ShortCutter",(unsigned long)error,detail);
    SC_LogDiagnostic(line);
    if (!g_shuttingDown && g_hWnd && IsWindow(g_hWnd)) {
        NOTIFYICONDATAW icon = {0};
        icon.cbSize = sizeof(icon);
        icon.hWnd = g_hWnd;
        icon.uID = ID_TRAYICON;
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_WARNING;
        lstrcpynW(icon.szInfoTitle,SC_Text(L"ShortCutter",L"ShortCutter"),64);
        /* Additional diagnostic lines belong in the log, not the notification. */
        const wchar_t *summary = operation ? operation : L"ShortCutter";
        int summaryLength = (int)wcscspn(summary,L"\r\n");
        if (summaryLength > 90) summaryLength = 90;
        swprintf(icon.szInfo,sizeof(icon.szInfo)/sizeof(icon.szInfo[0]),L"%.*ls\n%.140ls",
            summaryLength,summary,detail);
        Shell_NotifyIconW(NIM_MODIFY,&icon);
    }
}
static void SC_Save(void)
{
    if (!Config_Save()) SC_ReportError(SC_Text(L"Save configuration",L"保存配置"),Config_GetLastError());
}
static void Tray_SetState(int notify)
{
    NOTIFYICONDATAW icon = {0};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_hWnd;
    icon.uID = ID_TRAYICON;
    icon.uFlags = NIF_ICON|NIF_TIP;
    icon.hIcon = LoadIconW(g_hInst,MAKEINTRESOURCEW(g_enabled ? IDI_APP : IDI_APP_GRAY));
    lstrcpynW(icon.szTip,g_enabled ? SC_Text(L"ShortCutter - ON",L"ShortCutter - 已启用") :
        SC_Text(L"ShortCutter - OFF",L"ShortCutter - 已暂停"),128);
    if (notify) {
        icon.uFlags |= NIF_INFO;
        icon.dwInfoFlags = NIIF_INFO;
        lstrcpynW(icon.szInfo,g_enabled ? SC_Text(L"Gestures and hotkeys enabled",L"已启用手势和热键") :
            SC_Text(L"Gestures and hotkeys disabled",L"已暂停手势和热键"),256);
        lstrcpynW(icon.szInfoTitle,g_enabled ? SC_Text(L"ShortCutter ON",L"ShortCutter 已启用") :
            SC_Text(L"ShortCutter OFF",L"ShortCutter 已暂停"),64);
    }
    if (g_hWnd) Shell_NotifyIconW(NIM_MODIFY,&icon);
    if (g_hTrayMenu) CheckMenuItem(g_hTrayMenu,IDM_TOGGLE,MF_BYCOMMAND|(g_enabled ? MF_CHECKED : MF_UNCHECKED));
}
static void Tray_Add(HWND window)
{
    NOTIFYICONDATAW icon = {0};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = ID_TRAYICON;
    icon.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP;
    icon.uCallbackMessage = WM_TRAYICON;
    icon.hIcon = LoadIconW(g_hInst,MAKEINTRESOURCEW(g_enabled ? IDI_APP : IDI_APP_GRAY));
    lstrcpynW(icon.szTip,g_enabled ? SC_Text(L"ShortCutter - ON",L"ShortCutter - 已启用") :
        SC_Text(L"ShortCutter - OFF",L"ShortCutter - 已暂停"),128);
    if (!Shell_NotifyIconW(NIM_ADD,&icon)) SC_ReportError(SC_Text(L"Create tray icon",L"创建托盘图标"),GetLastError());
}
void Tray_ApplyLanguage(void)
{
    if (g_hTrayMenu) {
        const UINT ids[] = {IDM_TOGGLE,IDM_SETTINGS,IDM_EXIT,IDM_DASHBOARD};
        const wchar_t *labels[] = {SC_Text(L"Enabled",L"启用"),SC_Text(L"Settings",L"设置"),SC_Text(L"Exit",L"退出"),SC_Text(L"Dashboard",L"仪表盘")};
        for (int i = 0; i < 4; i++) {
            MENUITEMINFOW item = {0};
            item.cbSize = sizeof(item);
            item.fMask = MIIM_STRING;
            item.dwTypeData = (LPWSTR)labels[i];
            SetMenuItemInfoW(g_hTrayMenu,ids[i],FALSE,&item);
        }
    }
    Tray_SetState(0);
}
static void Tray_ShowMenu(HWND window)
{
    POINT position;
    GetCursorPos(&position);
    SetForegroundWindow(window);
    TrackPopupMenu(g_hTrayMenu,TPM_RIGHTBUTTON,position.x,position.y,0,window,NULL);
    PostMessageW(window,WM_NULL,0,0);
}
static UINT ScModsToWin(BYTE mods)
{
    UINT value = MOD_NOREPEAT;
    if (mods & SC_MOD_CTRL) value |= MOD_CONTROL;
    if (mods & SC_MOD_ALT) value |= MOD_ALT;
    if (mods & SC_MOD_SHIFT) value |= MOD_SHIFT;
    if (mods & SC_MOD_WIN) value |= MOD_WIN;
    return value;
}
static int IsModVk(DWORD vk)
{
    return vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN ||
        vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LMENU || vk == VK_RMENU;
}
static LRESULT CALLBACK KbHookProc(int code, WPARAM message, LPARAM param)
{
    if (code != HC_ACTION) return CallNextHookEx(NULL,code,message,param);
    KBDLLHOOKSTRUCT *key = (KBDLLHOOKSTRUCT*)param;
    if (key->flags & LLKHF_INJECTED || key->vkCode >= 256)
        return CallNextHookEx(NULL,code,message,param);
    BYTE vk = (BYTE)key->vkCode;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) Action_NoteUserInput();
    if (message == WM_KEYUP || message == WM_SYSKEYUP) g_hkDown[vk] = 0;
    if(UI_IsCapturingInput())return CallNextHookEx(NULL,code,message,param);
    if (QuickInput_HandlePending((UINT)message,vk,key->scanCode,key->flags)) return 1;
    if (Dashboard_HandlePhysicalKey((UINT)message,vk,key->scanCode,key->flags)) return 1;
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        return CallNextHookEx(NULL,code,message,param);
    if (!g_enabled || IsModVk(vk) || g_hkDown[vk])
        return CallNextHookEx(NULL,code,message,param);
    BYTE mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= SC_MOD_CTRL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= SC_MOD_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= SC_MOD_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= SC_MOD_WIN;
    for (int i = 0; i < g_cfg.hotkeyCount && i < MAX_HOTKEYS; i++) {
        const HotkeyDef *hotkey = &g_cfg.hotkeys[i];
        if (hotkey->enabled && hotkey->vk == vk && hotkey->mods == mods) {
            if (PostMessageW(g_hWnd,WM_APP+300,(WPARAM)(INT_PTR)hotkey->commandId,0)) g_hkDown[vk] = 1;
            return CallNextHookEx(NULL,code,message,param);
        }
    }
    if (QuickInput_TryStart(vk,key->scanCode,key->flags,mods)) return 1;
    return CallNextHookEx(NULL,code,message,param);
}
void Hotkeys_RegisterToggle(void)
{
    UnregisterHotKey(g_hWnd,HKID_TOGGLE);
    g_toggleRegistered = 0;
    if (g_cfg.toggleVk) {
        g_toggleRegistered = RegisterHotKey(g_hWnd,HKID_TOGGLE,ScModsToWin(g_cfg.toggleMods),g_cfg.toggleVk);
        if (!g_toggleRegistered) SC_ReportError(SC_Text(L"Register enable/pause hotkey",L"注册启用/暂停快捷键"),GetLastError());
    }
}
void Hotkeys_RegisterUser(void)
{
    if (!g_hKbHook) {
        g_hKbHook = SetWindowsHookExW(WH_KEYBOARD_LL,KbHookProc,GetModuleHandleW(NULL),0);
        if (!g_hKbHook) SC_ReportError(SC_Text(L"Install keyboard hook",L"安装键盘监听"),GetLastError());
    }
}
void Hotkeys_Register(void) { Hotkeys_RegisterToggle(); Hotkeys_RegisterUser(); }
void Hotkeys_Unregister(void)
{
    UnregisterHotKey(g_hWnd,HKID_TOGGLE);
    g_toggleRegistered = 0;
    if (g_hKbHook) { UnhookWindowsHookEx(g_hKbHook); g_hKbHook = NULL; }
    memset(g_hkDown,0,sizeof(g_hkDown));
}
DWORD SC_GetInputStatus(void)
{
    return (g_hKbHook ? SC_INPUT_KEYBOARD : 0) | (Gesture_GetHookStatus() ? SC_INPUT_MOUSE : 0) |
        (g_toggleRegistered ? SC_INPUT_TOGGLE : 0);
}
const wchar_t *SC_GetInputStatusText(void)
{
    if (!g_enabled) return SC_Text(L"Paused",L"已暂停");
    DWORD required = SC_INPUT_KEYBOARD|SC_INPUT_MOUSE|(g_cfg.toggleVk ? SC_INPUT_TOGGLE : 0);
    if ((SC_GetInputStatus() & required) == required) return SC_Text(L"Listening",L"监听中");
    return SC_Text(L"Input unavailable - check log",L"监听异常，请查看日志");
}
void SC_SetEnabled(int enabled)
{
    enabled = !!enabled;
    if (g_enabled == enabled) { Tray_SetState(0); return; }
    g_enabled = g_cfg.enabled = enabled;
    Action_Cancel();
    QuickInput_Cancel();
    Gesture_Cancel();
    Gesture_DiscardPendingEvents();
    MSG message;
    while (PeekMessageW(&message,g_hWnd,WM_APP+300,WM_APP+300,PM_REMOVE)) { }
    memset(g_hkDown,0,sizeof(g_hkDown));
    Dashboard_Close();
    if (enabled) {
        Hotkeys_RegisterUser();
        Gesture_Start();
    } else Gesture_Stop();
    SC_Save();
    Tray_SetState(1);
}
static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (g_taskbarCreated && message == g_taskbarCreated) {
        Tray_Add(window);
        Tray_SetState(0);
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        Tray_Add(window);
        return 0;
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) Tray_ShowMenu(window);
        else if (lp == WM_LBUTTONDBLCLK) Dashboard_Show();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE: SC_SetEnabled(!g_enabled); return 0;
        case IDM_SETTINGS: UI_Show(window); return 0;
        case IDM_DASHBOARD: Dashboard_Show(); return 0;
        case IDM_EXIT: DestroyWindow(window); return 0;
        }
        break;
    case WM_HOTKEY:
        if (Action_HandleForegroundHotkey(wp,lp)) return 0;
        if(UI_IsCapturingInput())return 0;
        if (wp == HKID_TOGGLE) SC_SetEnabled(!g_enabled);
        return 0;
    case WM_APP+300:
        if (g_enabled) Action_ExecuteCommand((int)wp);
        return 0;
    case WM_SC_MATCH: Gesture_HandleMatch(wp,lp); return 0;
    case WM_SC_ACTIONRESULT: Action_HandleResult(lp); return 0;
    case WM_SC_FOREGROUND:
    case WM_SC_FOREGROUNDGRANT: return Action_HandleForeground(message,wp,lp);
    case WM_SC_DASHICONS: Dashboard_AcceptIcons(); return 0;
    case WM_SC_QUICKINPUT: QuickInput_Dispatch(); return 0;
    case WM_DISPLAYCHANGE: Gesture_OnDisplayChange(); return 0;
    case WM_TIMER:
        if (wp == SC_ACTION_TIMER) { Action_Tick(); return 0; }
        break;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (wp) { Action_Cancel(); Gesture_Cancel(); SC_Save(); }
        return 0;
    case WM_DESTROY: {
        g_shuttingDown = 1;
        Hotkeys_Unregister();
        Gesture_Stop();
        Gesture_DiscardPendingEvents();
        QuickInput_Cancel();
        Action_Shutdown();
        Dashboard_Shutdown();
        SC_Save();
        NOTIFYICONDATAW icon = {0};
        icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = ID_TRAYICON;
        Shell_NotifyIconW(NIM_DELETE,&icon);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window,message,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int show)
{
    (void)previous; (void)commandLine; (void)show;
    HANDLE mutex = CreateMutexW(NULL,TRUE,L"ShortCutter_SingleInstance_8F3A");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(APP_NAME,NULL);
        if (running) PostMessageW(running,WM_COMMAND,IDM_SETTINGS,0);
        CloseHandle(mutex);
        return 0;
    }
    if (!mutex) return 1;
    g_hInst = instance;
    SetProcessDPIAware();
    HDC screen = GetDC(NULL);
    if (screen) { g_dpiScale = GetDeviceCaps(screen,LOGPIXELSX); ReleaseDC(NULL,screen); }
    DWORD length = GetModuleFileNameW(NULL,g_exeDir,SC_PATH_CAP);
    if (!length || length >= SC_PATH_CAP) {
        MessageBoxW(NULL,SC_Text(L"Application path is too long.",L"程序路径过长。"),L"ShortCutter",MB_OK|MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    wchar_t *slash = wcsrchr(g_exeDir,L'\\');
    if (!slash) { CloseHandle(mutex); return 1; }
    *slash = 0;
    Config_SetDefaults();
    Config_Load();
    g_enabled = !!g_cfg.enabled;
    /* Startup reads configuration only. Autorun is changed explicitly in
       Settings; distribution documentation has one maintained source on disk. */
    INITCOMMONCONTROLSEX controls = {sizeof(controls),ICC_WIN95_CLASSES|ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof(cls); cls.lpfnWndProc = WndProc; cls.hInstance = instance;
    cls.lpszClassName = APP_NAME; cls.hCursor = LoadCursor(NULL,IDC_ARROW);
    if (!RegisterClassExW(&cls)) { SC_ReportError(L"Register application window",GetLastError()); CloseHandle(mutex); return 1; }
    g_hWnd = CreateWindowExW(0,APP_NAME,APP_TITLE,WS_POPUP,0,0,0,0,NULL,NULL,instance,NULL);
    if (!g_hWnd) { SC_ReportError(L"Create application window",GetLastError()); CloseHandle(mutex); return 1; }
    g_hTrayMenu = CreatePopupMenu();
    AppendMenuW(g_hTrayMenu,MF_STRING|(g_enabled ? MF_CHECKED : 0),IDM_TOGGLE,SC_Text(L"Enabled",L"启用"));
    AppendMenuW(g_hTrayMenu,MF_STRING,IDM_DASHBOARD,SC_Text(L"Dashboard",L"仪表盘"));
    AppendMenuW(g_hTrayMenu,MF_STRING,IDM_SETTINGS,SC_Text(L"Settings",L"设置"));
    AppendMenuW(g_hTrayMenu,MF_SEPARATOR,0,NULL);
    AppendMenuW(g_hTrayMenu,MF_STRING,IDM_EXIT,SC_Text(L"Exit",L"退出"));
    Scripts_ScanAndSync();
    Dashboard_Init();
    Hotkeys_Register();
    if (g_enabled) Gesture_Start();
    MSG message = {0};
    int status;
    while ((status = GetMessageW(&message,NULL,0,0)) > 0) {
        if (!UI_ProcessDialogMessage(&message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (status == -1) {
        SC_ReportError(SC_Text(L"Read application messages",L"读取程序消息"),GetLastError());
        if (IsWindow(g_hWnd)) DestroyWindow(g_hWnd);
    }
    Hotkeys_Unregister();
    Gesture_Stop();
    if (g_hTrayMenu) DestroyMenu(g_hTrayMenu);
    CloseHandle(mutex);
    return status == -1 ? 1 : (int)message.wParam;
}
