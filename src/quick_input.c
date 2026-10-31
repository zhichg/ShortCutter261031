#include "main.h"

#define QUICK_MAX_KEYS 512
typedef struct {
    DWORD vk, scan, flags;
    wchar_t character;
} QuickKey;
static QuickKey g_quickKeys[QUICK_MAX_KEYS];
static int g_quickCount, g_quickPending;
static HWND g_quickOrigin;
/* Keep releases paired even if Escape closes Dashboard before key-up. */
static BYTE g_quickSwallowed[256];

static int Quick_ClassIs(HWND window, const wchar_t *name)
{
    wchar_t cls[128];
    return window && GetClassNameW(window, cls, 128) && !wcscmp(cls, name);
}

static int Quick_ShellProcess(HWND window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) return 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 0;
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    int shell = 0;
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        const wchar_t *base = wcsrchr(path, L'\\');
        shell = base && !lstrcmpiW(base + 1, L"explorer.exe");
    }
    CloseHandle(process);
    return shell;
}

static int Quick_MouseBusy(void)
{
    const int buttons[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (int i = 0; i < 5; i++)
        if (GetAsyncKeyState(buttons[i]) & 0x8000) return 1;
    return 0;
}

static int Quick_ContextAllows(HWND foreground)
{
    if (!foreground || !g_enabled || !g_cfg.dashboardQuickInput || Gesture_IsActive() || Quick_MouseBusy()) return 0;
    int desktop = Quick_ClassIs(foreground, L"Progman") || Quick_ClassIs(foreground, L"WorkerW");
    int explorer = Quick_ClassIs(foreground, L"CabinetWClass") || Quick_ClassIs(foreground, L"ExploreWClass");
    if ((!desktop || !g_cfg.dashboardQuickDesktop) &&
        (!explorer || !g_cfg.dashboardQuickExplorer)) return 0;

    GUITHREADINFO info = {0};
    info.cbSize = sizeof(info);
    DWORD thread = GetWindowThreadProcessId(foreground, NULL);
    if (!thread || !GetGUIThreadInfo(thread, &info) || info.hwndActive != foreground ||
        info.hwndCapture || info.hwndCaret ||
        (info.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_INMOVESIZE))) return 0;

    /* Positive identification of the file view, not merely "no caret".
       Edit controls, navigation trees, address/search bars and other apps
       are never candidates, even if their caret is hidden or custom-drawn. */
    HWND focus = info.hwndFocus;
    if (!Quick_ClassIs(focus, L"SysListView32") && !Quick_ClassIs(focus, L"DirectUIHWND")) return 0;
    int fileView = 0;
    for (HWND parent = GetParent(focus); parent; parent = GetParent(parent)) {
        if (Quick_ClassIs(parent, L"SHELLDLL_DefView")) fileView = 1;
        if (parent == foreground) break;
    }
    return fileView && IsChild(foreground, focus) && Quick_ShellProcess(foreground);
}

static void Quick_Append(DWORD vk, DWORD scan, DWORD flags)
{
    if (g_quickCount >= QUICK_MAX_KEYS) return;
    QuickKey *key = &g_quickKeys[g_quickCount++];
    key->vk = vk; key->scan = scan; key->flags = flags; key->character = 0;
    if (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB || vk == VK_SPACE ||
        vk == VK_BACK || vk == VK_DELETE || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END) return;

    if (vk >= 'A' && vk <= 'Z') {
        /* Command letters refer to the physical A-Z key, independently of
           the input method, keyboard layout and pending dead-key state. */
        int upper = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) ^ ((GetKeyState(VK_CAPITAL) & 1) != 0);
        key->character = (wchar_t)(upper ? vk : vk + ('a' - 'A'));
    }
}

int QuickInput_HandlePending(UINT msg, DWORD vk, DWORD scan, DWORD flags)
{
    if (vk >= 256 || (flags & LLKHF_INJECTED)) return 0;
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        int swallowed = g_quickSwallowed[vk];
        g_quickSwallowed[vk] = 0;
        return swallowed;
    }
    if (!g_quickPending || msg != WM_KEYDOWN) return 0;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_LWIN || vk == VK_RWIN) return 0;
    Quick_Append(vk, scan, flags);
    g_quickSwallowed[vk] = 1;
    return 1;
}

int QuickInput_TryStart(DWORD vk, DWORD scan, DWORD flags, BYTE mods)
{
    if (!g_enabled || !g_cfg.dashboardQuickInput || vk < 'A' || vk > 'Z' ||
        (flags & (LLKHF_INJECTED | LLKHF_ALTDOWN)) ||
        (mods & (SC_MOD_CTRL | SC_MOD_ALT | SC_MOD_WIN)) ||
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
        (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000) ||
        g_quickSwallowed[vk] || Dashboard_IsVisible()) return 0;
    HWND foreground = GetForegroundWindow();
    if (!Quick_ContextAllows(foreground)) return 0;
    g_quickOrigin = foreground;
    g_quickCount = 0;
    Quick_Append(vk, scan, flags);
    g_quickPending = 1;
    if (!PostMessageW(g_hWnd, WM_SC_QUICKINPUT, 0, 0)) {
        g_quickPending = g_quickCount = 0;
        return 0;
    }
    g_quickSwallowed[vk] = 1;
    return 1;
}

static void Quick_Replay(void)
{
    /* If opening was cancelled/failed while still on the original window,
       return the intercepted keystrokes. Injected events bypass our hook. */
    if (GetForegroundWindow() != g_quickOrigin) return;
    for (int i = 0; i < g_quickCount; i++) {
        INPUT input[2] = {0};
        input[0].type = input[1].type = INPUT_KEYBOARD;
        input[0].ki.wVk = input[1].ki.wVk = (WORD)g_quickKeys[i].vk;
        input[0].ki.wScan = input[1].ki.wScan = (WORD)g_quickKeys[i].scan;
        if (g_quickKeys[i].flags & LLKHF_EXTENDED)
            input[0].ki.dwFlags = input[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
        SendInput(2, input, sizeof(INPUT));
    }
}

void QuickInput_Cancel(void)
{
    g_quickPending = g_quickCount = 0;
    g_quickOrigin = NULL;
}

void QuickInput_Dispatch(void)
{
    if (!g_quickPending) return;
    if (GetForegroundWindow() != g_quickOrigin || !Quick_ContextAllows(g_quickOrigin)) {
        Quick_Replay();
        QuickInput_Cancel();
        return;
    }
    Dashboard_Show();
    if (!Dashboard_IsVisible()) Quick_Replay();
    else {
        for (int i = 0; i < g_quickCount && Dashboard_IsVisible(); i++) {
            QuickKey key = g_quickKeys[i];
            if (key.character) Dashboard_InputChar(key.character);
            else Dashboard_ForwardKey(key.vk, key.scan, key.flags);
        }
    }
    QuickInput_Cancel();
}
