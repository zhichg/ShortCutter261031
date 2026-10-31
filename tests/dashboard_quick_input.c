/* Real hook routing, Dashboard and queued messages; Shell state is isolated.
   No global hook installation, application launches or user INI writes. */
#include "../src/main.h"
static BYTE physical[256];
static SHORT TestKeyState(int key) { return physical[key] ? (SHORT)0x8000 : 0; }
static LRESULT TestNextHook(HHOOK hook, int code, WPARAM wp, LPARAM lp)
{ (void)hook; (void)code; (void)wp; (void)lp; return 42; }
#define GetAsyncKeyState TestKeyState
#define GetKeyState TestKeyState
#define CallNextHookEx TestNextHook
#include "../src/main.c"
#undef GetAsyncKeyState
#undef GetKeyState
#undef CallNextHookEx

static int failures, executed, saved;
static int gestureActive;
BOOL Config_Save(void) { saved++; return TRUE; }
void Config_Load(void) { }
void Config_SetDefaults(void) { }
BOOL Config_AutoStart(int enable) { (void)enable; return TRUE; }
void Scripts_ScanAndSync(void) { }
void UI_Show(HWND parent) { (void)parent; }
void Gesture_Start(void) { }
void Gesture_Stop(void) { }
int Gesture_IsActive(void) { return gestureActive; }
int Gesture_Match(const char *chain, int count, int modifier, int zone)
{ (void)chain; (void)count; (void)modifier; (void)zone; return -1; }
void Action_ExecuteCustom(const GestureDef *gesture) { (void)gesture; }
void Action_ExecuteCommand(int id) { executed = id; }
void Action_NoteUserInput(void) { }
LRESULT Action_HandleForeground(UINT message, WPARAM target, LPARAM generation)
{ (void)message; (void)target; (void)generation; return 0; }
int Action_HandleForegroundHotkey(WPARAM id, LPARAM key)
{ (void)id; (void)key; return 0; }
#include "../src/dashboard.c"

#define FH(n) ((HWND)(INT_PTR)(n))
static HWND foreground = FH(1);
static GUITHREADINFO shellInfo;
static const wchar_t *topClass = L"WorkerW", *focusClass = L"SysListView32";
static int shellProcess = 1, queryOk = 1, parentView = 1, infoOk = 1;
static int showFails, replayed, liveInspection;
static HWND TestForeground(void) { return liveInspection ? GetForegroundWindow() : foreground; }
static int TestClass(HWND window, LPWSTR text, int size)
{
    if (liveInspection) return GetClassNameW(window, text, size);
    const wchar_t *cls = window == FH(1) ? topClass : window == FH(2) ? focusClass :
        window == FH(3) ? (parentView ? L"SHELLDLL_DefView" : L"NavigationPane") : L"Unknown";
    lstrcpynW(text, cls, size); return (int)wcslen(text);
}
static HWND TestParent(HWND window)
{
    if (liveInspection) return GetParent(window);
    return window == FH(2) ? FH(3) : window == FH(3) ? FH(1) : NULL;
}
static BOOL TestChild(HWND parent, HWND child)
{ return liveInspection ? IsChild(parent, child) : parent == FH(1) && child == FH(2); }
static DWORD TestThread(HWND window, LPDWORD pid)
{
    if (liveInspection) return GetWindowThreadProcessId(window, pid);
    if (pid) *pid = 10;
    return window ? 10 : 0;
}
static BOOL TestGui(DWORD thread, PGUITHREADINFO info)
{
    if (liveInspection) return GetGUIThreadInfo(thread, info);
    if (!infoOk) return FALSE;
    *info = shellInfo; return TRUE;
}
static HANDLE TestOpen(DWORD access, BOOL inherit, DWORD pid)
{ return liveInspection ? OpenProcess(access, inherit, pid) : (HANDLE)(INT_PTR)1; }
static BOOL TestPath(HANDLE handle, DWORD flags, LPWSTR path, PDWORD size)
{
    if (liveInspection) return QueryFullProcessImageNameW(handle, flags, path, size);
    if (!queryOk) return FALSE;
    lstrcpyW(path, shellProcess ? L"C:\\Windows\\explorer.exe" : L"D:\\Other\\editor.exe");
    *size = (DWORD)wcslen(path); return TRUE;
}
static BOOL TestClose(HANDLE handle) { return liveInspection ? CloseHandle(handle) : TRUE; }
static SHORT TestAsync(int key)
{ return liveInspection ? GetAsyncKeyState(key) : physical[key] ? (SHORT)0x8000 : 0; }
static UINT TestSend(UINT count, LPINPUT input, int size)
{ (void)input; (void)size; replayed += (int)count / 2; return count; }
static void TestShow(void) { if (!showFails) Dashboard_Show(); }
#define GetForegroundWindow TestForeground
#define GetClassNameW TestClass
#define GetParent TestParent
#define IsChild TestChild
#define GetWindowThreadProcessId TestThread
#define GetGUIThreadInfo TestGui
#define OpenProcess TestOpen
#define QueryFullProcessImageNameW TestPath
#define CloseHandle TestClose
#define GetAsyncKeyState TestAsync
#define SendInput TestSend
#define Dashboard_Show TestShow
#include "../src/quick_input.c"
#undef GetForegroundWindow
#undef GetClassNameW
#undef GetParent
#undef IsChild
#undef GetWindowThreadProcessId
#undef GetGUIThreadInfo
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef CloseHandle
#undef GetAsyncKeyState
#undef GetKeyboardState
#undef GetKeyboardLayout
#undef SendInput
#undef Dashboard_Show

static void Check(int passed, const char *what)
{ printf("%s: %s\n", passed ? "PASS" : "FAIL", what); if (!passed) failures++; }
static LRESULT CALLBACK Sink(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SC_QUICKINPUT) { QuickInput_Dispatch(); return 0; }
    if (msg == WM_SC_DASHICONS) { Dashboard_AcceptIcons(); return 0; }
    if (msg == WM_APP + 300) { Action_ExecuteCommand((int)wp); return 0; }
    return DefWindowProcW(window, msg, wp, lp);
}
static void Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
static LRESULT Hook(DWORD vk, UINT msg, DWORD flags)
{
    KBDLLHOOKSTRUCT key = {0}; key.vkCode = vk;
    key.scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC); key.flags = flags;
    return KbHookProc(HC_ACTION, msg, (LPARAM)&key);
}
static void Reset(void)
{
    QuickInput_Cancel(); Dashboard_Close(); Pump();
    memset(physical, 0, sizeof(physical)); memset(g_quickSwallowed, 0, sizeof(g_quickSwallowed));
    memset(&shellInfo, 0, sizeof(shellInfo));
    shellInfo.hwndActive = foreground = FH(1); shellInfo.hwndFocus = FH(2);
    topClass = L"WorkerW"; focusClass = L"SysListView32";
    shellProcess = queryOk = parentView = infoOk = 1;
    showFails = replayed = executed = 0;
    gestureActive = 0;
    g_enabled = 1; g_cfg.dashboardQuickInput = g_cfg.dashboardQuickDesktop = g_cfg.dashboardQuickExplorer = 1;
    g_cfg.hotkeyCount = 0; memset(g_hkDown,0,sizeof(g_hkDown));
}
static void Blocked(const char *what)
{ Check(Hook('C', WM_KEYDOWN, 0) == 42 && !g_quickPending, what); }
static void TestTrayLanguage(void)
{
    HWND originalWindow = g_hWnd;
    HMENU originalMenu = g_hTrayMenu;
    int originalLanguage = g_cfg.uiLanguage, originalEnabled = g_enabled, originalSaved = saved;
    g_hWnd = NULL; /* Exercise menu text only; never create or modify a tray icon. */
    g_hTrayMenu = CreatePopupMenu();
    AppendMenuW(g_hTrayMenu,MF_STRING|MF_CHECKED,IDM_TOGGLE,L"Enabled");
    AppendMenuW(g_hTrayMenu,MF_STRING|MF_GRAYED,IDM_SETTINGS,L"Settings");
    AppendMenuW(g_hTrayMenu,MF_STRING,IDM_EXIT,L"Exit");
    const UINT ids[] = {IDM_TOGGLE,IDM_SETTINGS,IDM_EXIT};
    const wchar_t *english[] = {L"Enabled",L"Settings",L"Exit"};
    const wchar_t *chinese[] = {L"启用",L"设置",L"退出"};
    UINT states[3];
    for (int i = 0; i < 3; i++) {
        MENUITEMINFOW item = {0}; item.cbSize = sizeof(item); item.fMask = MIIM_STATE;
        GetMenuItemInfoW(g_hTrayMenu,ids[i],FALSE,&item); states[i] = item.fState;
    }
    int correct = 1;
    const int languages[] = {UI_LANG_CHINESE,UI_LANG_ENGLISH};
    for (int l = 0; l < 2; l++) {
        g_cfg.uiLanguage = languages[l]; Tray_ApplyLanguage();
        for (int i = 0; i < 3; i++) {
            wchar_t text[64]; MENUITEMINFOW item = {0};
            item.cbSize = sizeof(item); item.fMask = MIIM_STRING|MIIM_STATE;
            item.dwTypeData = text; item.cch = 64;
            correct &= GetMenuItemInfoW(g_hTrayMenu,ids[i],FALSE,&item) && item.fState == states[i] &&
                !wcscmp(text,languages[l] == UI_LANG_CHINESE ? chinese[i] : english[i]);
        }
    }
    Check(correct,"real tray menu switches between Chinese and English while preserving checked and disabled states");
    Check(saved == originalSaved && g_enabled == originalEnabled,"tray language refresh never saves configuration or changes enabled state");
    DestroyMenu(g_hTrayMenu); g_hTrayMenu = originalMenu; g_hWnd = originalWindow;
    g_cfg.uiLanguage = originalLanguage;
}

int main(void)
{
    g_hInst = GetModuleHandleW(NULL);
    g_cfg.dashboardOpacity = 100;
    g_cfg.dashboardQuickInput = g_cfg.dashboardQuickDesktop = g_cfg.dashboardQuickExplorer = 1;
    liveInspection = 1;
    HWND actual = GetForegroundWindow();
    wchar_t cls[128]; GetClassNameW(actual, cls, 128);
    wprintf(L"Live Shell inspection: foreground class=%ls; allowed=%d\n", cls, Quick_ContextAllows(actual));
    liveInspection = 0;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES}; InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0}; wc.lpfnWndProc = Sink; wc.hInstance = g_hInst; wc.lpszClassName = L"SC_QuickInputTest";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,NULL,NULL,g_hInst,NULL);
    Dashboard_Init();
    Reset();
    Check(Hook('C',WM_KEYDOWN,0) == 1 && g_quickPending && !Dashboard_IsVisible(),
          "desktop letter is intercepted and opening is deferred outside the hook");
    Check(Hook('C',WM_KEYUP,0) == 1, "intercepted key-up is paired before opening");
    const char *tail = "HROME";
    for (int i = 0; tail[i]; i++) { Hook(tail[i],WM_KEYDOWN,0); Hook(tail[i],WM_KEYUP,0); }
    Pump();
    Check(Dashboard_IsVisible() && !wcscmp(g_sbText,L"chrome") && !g_quickPending,
          "rapid typing preserves the first letter and complete query exactly once");
    QuickInput_Dispatch();
    Check(!wcscmp(g_sbText,L"chrome"), "duplicate queued dispatch does not duplicate input");
    Reset(); Hook('A',WM_KEYDOWN,0);Hook('1',WM_KEYDOWN,0);Hook('1',WM_KEYUP,0);Hook(VK_NUMPAD2,WM_KEYDOWN,0);Hook(VK_NUMPAD2,WM_KEYUP,0);Pump();
    Check(!wcscmp(g_sbText,L"a12"),"digits typed during Dashboard opening retain order and appear once");
    Reset(); topClass = L"CabinetWClass"; focusClass = L"DirectUIHWND";
    Check(Hook('F',WM_KEYDOWN,0) == 1, "Explorer file-list focus permits quick input");
    Pump(); Check(!wcscmp(g_sbText,L"f"), "Explorer first letter reaches the actual search box");
    Reset(); physical[VK_SHIFT] = 1;
    Hook('A',WM_KEYDOWN,0); physical[VK_SHIFT] = 0; Pump();
    Check(!wcscmp(g_sbText,L"A"), "letter case is captured before Shift release and focus change");
    Reset(); Hook('A',WM_KEYDOWN,0); Hook(VK_BACK,WM_KEYDOWN,0); Hook('B',WM_KEYDOWN,0); Pump();
    Check(!wcscmp(g_sbText,L"b"), "buffered editing keys retain their order");
    Reset(); Hook('A',WM_KEYDOWN,0); Hook(VK_ESCAPE,WM_KEYDOWN,0); Pump();
    Check(!Dashboard_IsVisible() && !g_quickPending && Hook('A',WM_KEYUP,0) == 1,
          "Escape closes pending input and its original key-up stays paired");
    Reset(); g_cfg.dashboardQuickInput = 0; Blocked("master switch off preserves normal letters");
    Reset(); g_cfg.dashboardQuickDesktop = 0; Blocked("desktop scope can be disabled independently");
    Reset(); topClass = L"CabinetWClass"; g_cfg.dashboardQuickExplorer = 0;
    Blocked("Explorer scope can be disabled independently");
    Reset(); g_enabled = 0; Blocked("global Disabled also disables quick input");
    Reset(); topClass = L"EditorWindow"; Blocked("other applications are excluded");
    Reset(); shellProcess = 0; Blocked("Shell class names in another process are excluded");
    Reset(); queryOk = 0; Blocked("process inspection failure preserves the original input");
    Reset(); infoOk = 0; Blocked("focus inspection failure preserves the original input");
    Reset(); shellInfo.hwndActive = FH(9); Blocked("foreground activation mismatch is excluded");
    Reset(); shellInfo.hwndFocus = NULL; Blocked("unknown focus is excluded");
    Reset(); focusClass = L"Edit"; Blocked("file rename edit is excluded even without a visible caret");
    Reset(); topClass = L"CabinetWClass"; focusClass = L"Windows.UI.Input.InputSite.WindowClass";
    Blocked("modern address and search input controls are excluded");
    Reset(); focusClass = L"SysTreeView32"; Blocked("navigation trees keep normal key input");
    Reset(); parentView = 0; Blocked("generic DirectUI/list controls outside the file view are excluded");
    Reset(); shellInfo.hwndCaret = FH(2); Blocked("an active caret blocks automatic opening");
    Reset(); shellInfo.flags = GUI_INMENUMODE; Blocked("menus block automatic opening");
    Reset(); shellInfo.flags = GUI_INMOVESIZE; Blocked("window moving/resizing blocks automatic opening");
    Reset(); shellInfo.hwndCapture = FH(2); Blocked("mouse capture blocks automatic opening");
    Reset(); physical[VK_LBUTTON] = 1; Blocked("mouse button/drag in progress blocks automatic opening");
    Reset(); gestureActive = 1; Blocked("a tracked gesture blocks opening even when its mouse-down was intercepted");
    Reset(); physical[VK_CONTROL] = 1; Blocked("Ctrl combinations keep their original operation");
    Reset(); physical[VK_MENU] = 1; Blocked("Alt combinations keep their original operation");
    Reset(); physical[VK_LWIN] = 1; Blocked("Win combinations keep their original operation");
    Reset();
    Check(Hook('1',WM_KEYDOWN,0) == 42 && Hook(VK_F2,WM_KEYDOWN,0) == 42,
          "digits and function keys cannot initiate quick input");
    Check(Hook('A',WM_KEYDOWN,LLKHF_INJECTED) == 42 && !g_quickPending,
          "injected keys cannot initiate quick input");
    g_cfg.hotkeyCount = 1; g_cfg.hotkeys[0].enabled = 1; g_cfg.hotkeys[0].vk = 'C'; g_cfg.hotkeys[0].commandId = 7;
    Check(Hook('C',WM_KEYDOWN,0) == 42 && !g_quickPending, "existing unmodified hotkeys take precedence");
    Pump(); Check(executed == 7, "existing hotkey still reaches its deferred command");
    Reset(); Hook('A',WM_KEYDOWN,0); showFails = 1; Pump();
    Check(replayed == 1 && !g_quickPending && !Dashboard_IsVisible(),
          "failed Dashboard opening replays the intercepted letter to its original window");
    Reset(); Hook('A',WM_KEYDOWN,0); shellInfo.hwndCaret = FH(2); Pump();
    Check(replayed == 1 && !Dashboard_IsVisible(), "focus is rechecked before opening and changed input is restored");
    Reset(); Hook('A',WM_KEYDOWN,0); foreground = FH(9); Pump();
    Check(!replayed && !Dashboard_IsVisible(), "foreground changes never steal focus or replay letters into another app");
    Reset(); Hook('A',WM_KEYDOWN,0); QuickInput_Cancel(); Pump();
    Check(!Dashboard_IsVisible() && Hook('A',WM_KEYUP,0) == 1,
          "cancelled queued messages cannot reopen Dashboard or leak matching key-up");
    Check(!saved, "quick input never saves the user configuration");
    TestTrayLanguage();
    Dashboard_Shutdown(); DestroyWindow(g_hWnd);
    printf("Failures: %d\n", failures); return failures ? 1 : 0;
}

DWORD Config_GetLastError(void){return ERROR_WRITE_FAULT;}
BOOL UI_ProcessDialogMessage(MSG *message){(void)message;return FALSE;}
int UI_GetLanguage(void){return g_cfg.uiLanguage;}
int UI_IsCapturingInput(void){return 0;}
void Gesture_Cancel(void){}
void Gesture_DiscardPendingEvents(void){}
void Gesture_HandleMatch(WPARAM wp,LPARAM lp){(void)wp;(void)lp;}
void Gesture_OnDisplayChange(void){}
int Gesture_GetHookStatus(void){return 1;}
void Action_Tick(void){}
void Action_Cancel(void){}
void Action_Shutdown(void){}
void Action_HandleResult(LPARAM result){(void)result;}
