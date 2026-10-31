/* Actual Win32 controls and release timer; no app launches, hooks or INI writes. */
#include "../src/main.h"
AppConfig g_cfg;
int g_enabled = 1;
HINSTANCE g_hInst;
HWND g_hWnd;
HMENU g_hTrayMenu;
int g_dpiScale = 96;
wchar_t g_exeDir[SC_PATH_CAP];
static int failures, saved, executed[256], executedCount, executedWhileVisible;
static int leftCtrl, rightCtrl;
BOOL Config_Save(void) { saved++; return TRUE; }
void Scripts_ScanAndSync(void) { }
void Action_ExecuteCommand(int id)
{
    executed[executedCount++] = id;
    if (Dashboard_IsVisible()) executedWhileVisible++;
}
static SHORT TestKeyState(int key)
{
    if (key == VK_CONTROL) return leftCtrl || rightCtrl ? (SHORT)0x8000 : 0;
    return GetKeyState(key);
}
static SHORT TestAsyncKeyState(int key)
{
    if (key == VK_LCONTROL) return leftCtrl ? (SHORT)0x8000 : 0;
    if (key == VK_RCONTROL) return rightCtrl ? (SHORT)0x8000 : 0;
    return GetAsyncKeyState(key);
}
#define GetKeyState TestKeyState
#define GetAsyncKeyState TestAsyncKeyState
#include "../src/dashboard.c"
#undef GetKeyState
#undef GetAsyncKeyState
static void Check(int passed, const char *what)
{
    printf("%s: %s\n", passed ? "PASS" : "FAIL", what);
    if (!passed) failures++;
}
static LRESULT CALLBACK SinkProc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SC_DASHICONS) { Dashboard_AcceptIcons(); return 0; }
    return DefWindowProcW(window, msg, wp, lp);
}
static void PumpFor(DWORD milliseconds)
{
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
static LPARAM ItemPoint(int index)
{
    int local = g_dockPos[index] % DOCK_PAGE_SIZE;
    return MAKELPARAM(DockPane_ColX(POS_COL(local)) + g_dpItemW / 2,
                     POS_ROW(local) * g_dpStepY + g_dpItemH / 2);
}
static void ClickDock(int index, int ctrl)
{
    LPARAM point = ItemPoint(index);
    SendMessageW(g_hDockPane, WM_LBUTTONDOWN, MK_LBUTTON | (ctrl ? MK_CONTROL : 0), point);
    SendMessageW(g_hDockPane, WM_LBUTTONUP, ctrl ? MK_CONTROL : 0, point);
}
static void ClickList(HWND list, int row)
{
    RECT rect;
    SendMessageW(list, LB_GETITEMRECT, row, (LPARAM)&rect);
    LPARAM point = MAKELPARAM(rect.left + DS(20), (rect.top + rect.bottom) / 2);
    SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, point);
    SendMessageW(list, WM_LBUTTONUP, MK_CONTROL, point);
}
static void Restart(void)
{
    Dashboard_Close();
    leftCtrl = rightCtrl = 0;
    executedCount = executedWhileVisible = 0;
    Dashboard_Show();
}
int main(void)
{
    g_hInst = GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = SinkProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"SC_MultiselectTestSink";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, L"", WS_POPUP,
                          0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    g_cfg.dashboardOpacity = 100;
    g_cfg.commandCount = g_cfg.dockCount = 5;
    g_cfg.dockPageCount = 2;
    for (int i = 0; i < 5; i++) {
        g_cfg.commands[i].id = i + 1;
        g_cfg.commands[i].type = CMD_KEYS;
        snprintf(g_cfg.commands[i].name, 64, "Command %d", i + 1);
        g_cfg.dockItems[i].commandId = i + 1;
        g_dockPos[i] = i;
    }
    g_dockPos[4] = DOCK_PAGE_SIZE;
    Dashboard_Init();
    Dashboard_Show();
    leftCtrl = 1;
    ClickDock(2, 1);
    ClickDock(0, 1);
    PumpFor(120);
    Check(Dashboard_IsVisible() && !executedCount && g_pendingCount == 2,
          "Ctrl clicks stay visible and launch nothing while held");
    leftCtrl = 0;
    PumpFor(120);
    Check(executedCount == 2 && executed[0] == 3 && executed[1] == 1 &&
          !executedWhileVisible && !Dashboard_IsVisible() && !g_pendingCount,
          "real release timer hides Dashboard and launches in click order");
    PumpFor(90);
    Check(executedCount == 2, "batch executes exactly once");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    ClickDock(1, 1);
    ClickDock(0, 1);
    ClickDock(0, 1);
    Check(g_pendingCount == 2 && g_pendingCommands[0] == 2 && g_pendingCommands[1] == 1,
          "repeat Ctrl click toggles off; selecting again appends to the order");
    ClickDock(1, 1);
    ClickDock(0, 1);
    leftCtrl = 0;
    PumpFor(90);
    Check(!executedCount && Dashboard_IsVisible() && !g_pendingCount,
          "deselecting all keeps Dashboard open without launching");
    Restart();
    leftCtrl = rightCtrl = 1;
    ClickDock(1, 1);
    leftCtrl = 0;
    PumpFor(90);
    Check(!executedCount && Dashboard_IsVisible(), "holding right Ctrl delays launch after left Ctrl release");
    rightCtrl = 0;
    PumpFor(90);
    Check(executedCount == 1 && executed[0] == 2, "right Ctrl release opens the pending selection");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    LPARAM point = ItemPoint(1);
    SendMessageW(g_hDockPane, WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, point);
    leftCtrl = 0;
    PumpFor(90);
    Check(!executedCount && Dashboard_IsVisible(), "release during mouse-down waits for the final click");
    SendMessageW(g_hDockPane, WM_LBUTTONUP, 0, point);
    PumpFor(90);
    Check(executedCount == 2 && executed[0] == 1 && executed[1] == 2,
          "final mouse-up joins the batch even when Ctrl was released first");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    SendMessageW(g_hDockPane, WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, ItemPoint(1));
    SendMessageW(g_hDockPane, WM_MOUSEMOVE, MK_LBUTTON | MK_CONTROL, ItemPoint(2));
    SendMessageW(g_hDockPane, WM_LBUTTONUP, MK_CONTROL, ItemPoint(2));
    Check(g_pendingCount == 1 && g_dockPos[1] == 1 && !saved,
          "Ctrl dragging off an icon neither selects it nor rearranges the layout");
    int folderIndex = Dock_CreateFolder("Tools");
    int folder = g_cfg.dockItems[folderIndex].folderId;
    Dock_MoveItem(3, folder, 0);
    ClickDock(folderIndex, 1);
    ClickDock(3, 1);
    Dash_SetDockView(0, 1);
    ClickDock(4, 1);
    Check(g_pendingCount == 3 && Dashboard_IsVisible(), "selection survives folder navigation and page changes");
    leftCtrl = 0;
    PumpFor(90);
    Check(executedCount == 3 && executed[0] == 1 && executed[1] == 4 && executed[2] == 5,
          "commands across folders and pages launch in selection order");
    Restart();
    Dashboard_RecordUsage(2);
    Dash_FillMostUsed();
    SearchBox_SetText(L"Command 3");
    leftCtrl = 1;
    ClickList(g_hMostList, 0);
    ClickList(g_hMostList, 0);
    Check(!g_pendingCount, "Ctrl click on the same list row toggles selection off");
    ClickList(g_hMostList, 0);
    ClickList(g_hHistList, 0);
    SearchBox_SetText(L"Command 1");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Check(g_pendingCount == 3 && !executedCount && Dashboard_IsVisible(),
          "most-used, search results and Ctrl+Enter share the deferred queue");
    leftCtrl = 0;
    PumpFor(90);
    Check(executedCount == 3 && executed[0] == 2 && executed[1] == 3 && executed[2] == 1,
          "mixed list and keyboard selections preserve their order");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    Dash_SetDockView(folder, 0);
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_ESCAPE, 0);
    leftCtrl = 0;
    PumpFor(90);
    Check(!executedCount && !Dashboard_IsVisible(), "Escape cancels the entire batch even inside a folder");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    SendMessageW(g_hDash, WM_ACTIVATE, WA_INACTIVE, 0);
    leftCtrl = 0;
    PumpFor(90);
    Dashboard_Show();
    Check(!executedCount && !g_pendingCount, "focus loss cancels pending launches and reopening starts clean");
    leftCtrl = 1;
    ClickDock(0, 1);
    g_dockDialog++;
    leftCtrl = 0;
    PumpFor(90);
    Check(!executedCount, "an open Dashboard dialog postpones release execution");
    g_dockDialog--;
    PumpFor(90);
    Check(executedCount == 1, "batch resumes once the Dashboard dialog closes");
    Restart();
    leftCtrl = 1;
    ClickDock(0, 1);
    ClickDock(1, 1);
    g_cfg.commands[1].id = 99;
    leftCtrl = 0;
    PumpFor(90);
    Check(executedCount == 1 && executed[0] == 1, "deleted commands are skipped safely at release");
    g_cfg.commands[1].id = 2;
    Restart();
    ClickDock(1, 0);
    Check(executedCount == 1 && executed[0] == 2 && !Dashboard_IsVisible(),
          "ordinary icon click still launches once and closes");
    Restart();
    SearchBox_SetText(L"Command 3");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Check(executedCount == 1 && executed[0] == 3 && !Dashboard_IsVisible(),
          "ordinary search Enter still launches once and closes");
    Dashboard_Shutdown();
    DestroyWindow(g_hWnd);
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}

DWORD Config_GetLastError(void){return ERROR_WRITE_FAULT;}
void SC_ReportError(const wchar_t *operation,DWORD error){(void)operation;(void)error;}
