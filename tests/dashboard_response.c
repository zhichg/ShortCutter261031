/* Runs the actual Dashboard code without hooks, registry writes or user INI.
   Inject a slow icon extractor to exercise responsiveness and stale results. */
#include "../src/main.h"
#include <stdlib.h>

AppConfig g_cfg;
int g_enabled = 1;
HINSTANCE g_hInst;
HWND g_hWnd;
HMENU g_hTrayMenu;
int g_dpiScale = 96;
wchar_t g_exeDir[SC_PATH_CAP];
static int saveCount;
static int scanCount;
static int failures;
static int realIcons;
static volatile LONG extractedSize;
static volatile LONG extractionCount;
static HANDLE extracting;
static HICON latestIcon;

BOOL Config_Save(void) { saveCount++; return TRUE; }
void Scripts_ScanAndSync(void) { scanCount++; }
void Action_ExecuteCommand(int cmdId) { (void)cmdId; }

static UINT WINAPI TestExtract(LPCWSTR path, int index, int cx, int cy,
                              HICON *icons, UINT *ids, UINT count, UINT flags)
{
    if (realIcons)
        return PrivateExtractIconsW(path, index, cx, cy, icons, ids, count, flags);
    InterlockedIncrement(&extractionCount);
    SetEvent(extracting);
    Sleep(600);
    InterlockedExchange(&extractedSize, cx);
    latestIcon = CopyIcon(LoadIconW(NULL, MAKEINTRESOURCEW(IDI_INFORMATION)));
    *icons = latestIcon;
    return 1;
}

#define PrivateExtractIconsW TestExtract
#include "../src/dashboard.c"
#undef PrivateExtractIconsW

static void Check(int passed, const char *what)
{
    printf("%s: %s\n", passed ? "PASS" : "FAIL", what);
    if (!passed) failures++;
}

static double Milliseconds(LARGE_INTEGER start)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return 1000.0 * (double)(now.QuadPart - start.QuadPart) / frequency.QuadPart;
}

static LRESULT CALLBACK SinkProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SC_DASHICONS) {
        Dashboard_AcceptIcons();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static int AwaitIcons(DWORD timeout)
{
    ULONGLONG deadline = GetTickCount64() + timeout;
    while (GetTickCount64() < deadline) {
        Pump();
        int all = 1;
        for (int i = 0; i < g_cfg.dockCount; i++)
            if (!g_hDockIcons[i]) all = 0;
        if (all) return 1;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
    return 0;
}

int main(int argc, char **argv)
{
    realIcons = argc > 1 && strcmp(argv[1], "--real-icons") == 0;
    g_hInst = GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = SinkProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"SC_DashboardTestSink";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, L"", WS_POPUP,
                          0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    extracting = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_cfg.dashboardOpacity = 100;
    g_cfg.dashboardTheme = realIcons ? DASH_THEME_GRAY : DASH_THEME_BLACK;
    g_cfg.commandCount = realIcons ? 4 : 1;
    g_cfg.dockCount = g_cfg.commandCount;
    for (int i = 0; i < g_cfg.commandCount; i++) {
        g_cfg.commands[i].id = i + 1;
        g_cfg.commands[i].type = CMD_RUN;
        snprintf(g_cfg.commands[i].name, 64, "Test command %d", i + 1);
        strcpy(g_cfg.commands[i].runPath, "%SystemRoot%\\system32\\notepad.exe");
        g_cfg.dockItems[i].commandId = i + 1;
        g_dockPos[i] = i;
    }
    if (realIcons) {
        strcpy(g_cfg.dockItems[1].iconSource, "src\\app.ico");
        strcpy(g_cfg.dockItems[2].iconSource, "%SystemRoot%\\system32\\shell32.dll");
        g_cfg.commands[3].type = CMD_KEYS;
        g_cfg.commands[3].runPath[0] = 0;
    }

    HWND previousFocus = GetFocus();
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    Dashboard_Init();
    printf("Hidden initialization: %.2f ms\n", Milliseconds(start));
    Check(g_hDash && !Dashboard_IsVisible(), "startup creates a hidden Dashboard");
    Check(g_dashPalette == &g_dashPalettes[g_cfg.dashboardTheme],
          "startup prewarm applies the configured theme before first show");
    Check(GetFocus() == previousFocus, "prewarm does not steal focus");
    HWND dash = g_hDash, search = g_hSearchEdit;
    if (!realIcons)
        Check(WaitForSingleObject(extracting, 2000) == WAIT_OBJECT_0,
              "worker is reading a deliberately slow icon");

    QueryPerformanceCounter(&start);
    Dashboard_Show();
    double first = Milliseconds(start);
    printf("First show: %.2f ms\n", first);
    Check(Dashboard_IsVisible() && first < 200, "first show responds during icon extraction");
    QueryPerformanceCounter(&start);
    SendMessageW(search, WM_CHAR, L'T', 0);
    double typing = Milliseconds(start);
    printf("Search input: %.2f ms\n", typing);
    Check(g_sbLen == 1 && typing < 100, "search responds while icons load");

    QueryPerformanceCounter(&start);
    Dashboard_Close();
    double close = Milliseconds(start);
    printf("Close: %.2f ms\n", close);
    Check(!Dashboard_IsVisible() && IsWindow(dash) && close < 100,
          "close hides and retains controls immediately");
    Dashboard_ForwardKey(VK_BACK, 0, 0);
    Check(g_sbLen == 1 && !Dashboard_HasFocus(), "hidden Dashboard ignores forwarded keys");
    Check(AwaitIcons(10000), "icon prewarm finishes while Dashboard is hidden");

    Dashboard_RecordUsage(1);
    QueryPerformanceCounter(&start);
    Dashboard_Show();
    double reopen = Milliseconds(start);
    printf("Reopen: %.2f ms\n", reopen);
    Check(g_hDash == dash && g_hSearchEdit == search && reopen < 100,
          "reopen reuses window and controls");
    Check(g_sbLen == 0 && SendMessageW(g_hMostList, LB_GETCOUNT, 0, 0) == 1,
          "reopen resets search and refreshes usage");
    Check(SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0,
          "history is ready on first frame");
    HICON cachedIcon = g_hDockIcons[0];
    DWORD generation = g_iconRequest.generation;
    Dashboard_Close();
    Dashboard_Show();
    Check(g_hDockIcons[0] == cachedIcon && g_iconRequest.generation == generation,
          "unchanged reopen retains icon cache");

    if (!realIcons) {
        ResetEvent(extracting);
        strcpy(g_cfg.dockItems[0].iconSource, "slow-old.ico");
        Dash_RefreshDockPane();
        Check(WaitForSingleObject(extracting, 2000) == WAIT_OBJECT_0,
              "custom icon refresh runs on worker");
        strcpy(g_cfg.dockItems[0].iconSource, "new-current.ico");
        RECT rect;
        GetWindowRect(dash, &rect);
        QueryPerformanceCounter(&start);
        SendMessageW(dash, WM_DPICHANGED, MAKELONG(144, 144), (LPARAM)&rect);
        double dpi = Milliseconds(start);
        printf("DPI change with pending icons: %.2f ms\n", dpi);
        Check(dpi < 200 && !g_hDockIcons[0], "DPI/config change queues fresh icons without waiting");
        Check(AwaitIcons(5000), "latest icon generation finishes");
        Check(g_hDockIcons[0] == latestIcon && extractedSize == DS(42) &&
              strcmp(g_iconRequest.sources[0].iconSource, "new-current.ico") == 0,
              "stale custom-icon/DPI result is discarded");
        Check(extractionCount == 3, "single worker only extracts required generations");
    }

    Dashboard_Close();
    Pump();
    /* A synthetic DPI change above may be corrected by Windows when shown
       on the physical monitor. Wait for that final icon batch before counting. */
    Dashboard_Show();
    Dashboard_Close();
    Check(AwaitIcons(5000), "monitor DPI icon refresh settles before resource check");
    Pump();
    DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    double maxShow = 0;
    for (int i = 0; i < 50; i++) {
        QueryPerformanceCounter(&start);
        Dashboard_Show();
        double elapsed = Milliseconds(start);
        if (elapsed > maxShow) maxShow = elapsed;
        SendMessageW(dash, WM_CLOSE, 0, 0);
        Pump();
    }
    printf("50 reopen cycles: max %.2f ms; GDI %lu -> %lu; USER %lu -> %lu\n",
           maxShow, (unsigned long)gdiBefore,
           (unsigned long)GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
           (unsigned long)userBefore,
           (unsigned long)GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
    Check(g_hDash == dash && !Dashboard_IsVisible() && maxShow < 200,
          "repeated show/WM_CLOSE stays responsive and retains window");
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore &&
          GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore,
          "reopening does not grow GDI/USER resources");
    Check(saveCount == 0 && scanCount == 0, "show/close never saves INI or scans files");
    if (!realIcons) {
        ResetEvent(extracting);
        strcpy(g_cfg.dockItems[0].iconSource, "slow-exit.ico");
        Dash_RefreshDockPane();
        Check(WaitForSingleObject(extracting, 2000) == WAIT_OBJECT_0,
              "worker is extracting during shutdown");
    }
    QueryPerformanceCounter(&start);
    Dashboard_Shutdown();
    double shutdown = Milliseconds(start);
    printf("Shutdown: %.2f ms\n", shutdown);
    Check(shutdown < 100, "shutdown does not wait for slow Shell extraction");
    Check(!g_hDash && WaitForSingleObject(g_iconThread, 2000) == WAIT_OBJECT_0,
          "shutdown releases window and worker exits without publishing stale icons");
    Check(g_readyIcons[0] == NULL, "no icon results remain after shutdown");
    CloseHandle(g_iconThread);
    CloseHandle(g_iconEvent);
    DeleteCriticalSection(&g_iconLock);
    CloseHandle(extracting);
    DestroyWindow(g_hWnd);
    printf("Result: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}

DWORD Config_GetLastError(void){return ERROR_WRITE_FAULT;}
void SC_ReportError(const wchar_t *operation,DWORD error){(void)operation;(void)error;}
