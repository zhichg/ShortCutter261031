/* Real configuration round-trip and Win32 interaction checks. All fixture
   files stay in tests; no hooks, auto-start, registry or user INI is touched. */
#include "../src/main.h"
#include <stdlib.h>

AppConfig g_cfg;
int g_enabled = 1;
HINSTANCE g_hInst;
HWND g_hWnd;
HMENU g_hTrayMenu;
int g_dpiScale = 96;
wchar_t g_exeDir[SC_PATH_CAP];
static wchar_t testIni[MAX_PATH];
static int failures, executed;
const wchar_t *GetIniPath(void) { return testIni; }
const wchar_t *GetExeDir(void) { return g_exeDir; }
void Action_ExecuteCommand(int id) { executed = id; }
/* Keep Settings integration isolated from registry, hooks and script files. */
#define Config_AutoStart Fixture_UnusedAutoStart
#define Scripts_ScanAndSync Fixture_UnusedScriptsScan
#include "../src/config.c"
#undef Config_AutoStart
#undef Scripts_ScanAndSync
static int applySideEffects;
static int languageTrayUpdates;
BOOL Config_AutoStart(int enable) { applySideEffects++;g_cfg.autoStart=enable;return TRUE; }
void Tray_ApplyLanguage(void) { languageTrayUpdates++; }
void Scripts_ScanAndSync(void) { }
void Hotkeys_Register(void) { }
int Gesture_Downsample(const POINT *src, int count, POINT *dst, int max, int dist)
{ (void)src; (void)count; (void)dst; (void)max; (void)dist; return 0; }
int Gesture_EncodeChain(const POINT *pts, int count, char *chain, int max)
{ (void)pts; (void)count; (void)chain; (void)max; return 0; }
#include "../src/dashboard.c"

static void Check(int passed, const char *what)
{
    printf("%s: %s\n", passed ? "PASS" : "FAIL", what);
    if (!passed) failures++;
}

static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SC_DASHICONS) { Dashboard_AcceptIcons(); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void SelectSettingsTab(HWND settings, int index)
{
    HWND tab = GetDlgItem(settings, 2000);
    NMHDR notification = {tab, 2000, TCN_SELCHANGE};
    SendMessageW(tab, TCM_SETCURSEL, index, 0);
    SendMessageW(settings, WM_NOTIFY, 2000, (LPARAM)&notification);
    Pump();
}

static void SelectSettingsLanguage(HWND settings, int language)
{
    HWND combo = GetDlgItem(settings, 3047);
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int row = 0; row < count; row++) {
        if ((int)SendMessageW(combo, CB_GETITEMDATA, row, 0) != language) continue;
        SendMessageW(combo, CB_SETCURSEL, row, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(3047, CBN_SELCHANGE), (LPARAM)combo);
        Pump();
        return;
    }
    Check(0, "requested language exists in the Settings selector");
}

static int WindowTextIs(HWND window, const wchar_t *expected)
{
    wchar_t text[256];
    GetWindowTextW(window, text, 256);
    return !wcscmp(text, expected);
}

static int HasChineseText(const wchar_t *text)
{
    for (; *text; text++) if (*text >= 0x4e00 && *text <= 0x9fff) return 1;
    return 0;
}

static int LocalizedControl(HWND control, const wchar_t *english)
{
    wchar_t text[256];
    GetWindowTextW(control, text, 256);
    return UI_GetLanguage() == UI_LANG_CHINESE ? HasChineseText(text) : !wcscmp(text, english);
}

static int LocalizedControlPair(HWND control, const wchar_t *english, const wchar_t *chinese)
{
    return WindowTextIs(control, UI_GetLanguage() == UI_LANG_CHINESE ? chinese : english);
}

static int LocalizedComboPair(HWND combo, int row, const wchar_t *english, const wchar_t *chinese)
{
    wchar_t text[256] = {0};
    if (SendMessageW(combo, CB_GETLBTEXTLEN, row, 0) >= 256) return 0;
    SendMessageW(combo, CB_GETLBTEXT, row, (LPARAM)text);
    return !wcscmp(text, UI_GetLanguage() == UI_LANG_CHINESE ? chinese : english);
}

static int LocalizedColumnPair(HWND list, int column, const wchar_t *english, const wchar_t *chinese)
{
    wchar_t text[256] = {0};
    LVCOLUMNW item = {0}; item.mask = LVCF_TEXT; item.pszText = text; item.cchTextMax = 256;
    SendMessageW(list, LVM_GETCOLUMNW, column, (LPARAM)&item);
    return !wcscmp(text, UI_GetLanguage() == UI_LANG_CHINESE ? chinese : english);
}

static DWORD IniChecksum(void)
{
    FILE *file = _wfopen(testIni, L"rb");
    DWORD checksum = 2166136261u;
    if (!file) return 0;
    int value;
    while ((value = fgetc(file)) != EOF) checksum = (checksum ^ (BYTE)value) * 16777619u;
    fclose(file);
    return checksum;
}

static int SettingsTextFits(HWND control)
{
    wchar_t text[256], className[32];
    RECT rect;
    GetClientRect(control, &rect);
    GetWindowTextW(control, text, 256);
    GetClassNameW(control, className, 32);
    if (!text[0]) return 1;
    HDC dc = GetDC(control);
    HFONT old = SelectObject(dc, (HFONT)SendMessageW(control, WM_GETFONT, 0, 0));
    SIZE size;
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    int inset = 0;
    if (!wcscmp(className, L"Button")) {
        DWORD type = (DWORD)GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
        inset = MulDiv(type == BS_AUTOCHECKBOX ? 22 : 12, g_dpiScale, 96);
    } else if (!wcscmp(className, L"ComboBox")) {
        inset = MulDiv(32, g_dpiScale, 96);
    } else if (!wcscmp(className, L"Edit")) {
        inset = MulDiv(8, g_dpiScale, 96);
    }
    if (size.cx + inset > rect.right || size.cy > rect.bottom) {
        printf("Clipped Settings text: control %d, measured %ld x %ld, client %ld x %ld, inset %d\n",
               GetDlgCtrlID(control), size.cx, size.cy, rect.right, rect.bottom, inset);
        return 0;
    }
    return 1;
}

static void CaptureWindow(HWND window, const wchar_t *path)
{
    RECT rect;
    GetClientRect(window, &rect);
    int width = rect.right, height = rect.bottom;
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    HBITMAP old = SelectObject(dc, bitmap);
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    /* Owned alpha corners are separate compositor surfaces; WM_PRINT alone
       omits them. Capture only this test window's client rectangle on screen. */
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI *FlushFn)(void);
        FlushFn flush = (FlushFn)(void*)GetProcAddress(dwm, "DwmFlush");
        if (flush) flush();
        FreeLibrary(dwm);
    }
    if (GetPropW(window, L"SC_SmoothFrame")) {
        POINT origin = {0, 0};
        ClientToScreen(window, &origin);
        HDC screen = GetDC(NULL);
        BitBlt(dc, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
        ReleaseDC(NULL, screen);
    } else {
        /* Native Settings has no compositor corners; print its own contents
           so the capture remains valid if another app is in front of it. */
        PrintWindow(window, dc, PW_CLIENTONLY);
    }
    GdiFlush();
    BITMAPFILEHEADER header = {0};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + width * height * 4;
    FILE *file = _wfopen(path, L"wb");
    if (file) {
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file);
        fwrite(pixels, width * height * 4, 1, file);
        fclose(file);
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void Capture(const wchar_t *path)
{
    CaptureWindow(g_hDash, path);
}

static void WriteLegacy(void)
{
    FILE *file = _wfopen(testIni, L"wb");
    wchar_t bom = 0xfeff, line[256];
    fwrite(&bom, sizeof(bom), 1, file);
    WrLine(file, L"[Settings]\r\nDashboardOpacity=100\r\n[Commands]\r\nCount=70\r\n");
    for (int i = 0; i < 70; i++) {
        swprintf(line, 256, L"Id%d=%d\r\nName%d=Test %02d\r\nType%d=1\r\nKeys%d={Esc}\r\n",
                 i, i + 1, i, i + 1, i, i);
        WrLine(file, line);
    }
    WrLine(file, L"[Dashboard]\r\nDockCount=24\r\n");
    for (int i = 0; i < 24; i++) {
        swprintf(line, 256, L"DockCmd%d=%d\r\nDockPos%d=%d\r\n", i, i + 1, i, 23 - i);
        WrLine(file, line);
    }
    fclose(file);
}

static int AppendCommand(int id, int folder, int page)
{
    int pos = Dock_AllocCell(folder, page, -1);
    int index = g_cfg.dockCount++;
    memset(&g_cfg.dockItems[index], 0, sizeof(DockItem));
    g_cfg.dockItems[index].commandId = id;
    g_cfg.dockItems[index].parentFolderId = folder;
    g_dockPos[index] = pos;
    Dock_EnsurePage(folder, pos);
    return index;
}

static void TestPageDeletion(void)
{
    static AppConfig original;
    static int originalPos[MAX_DOCK_ITEMS];
    original = g_cfg;
    memcpy(originalPos, g_dockPos, sizeof(originalPos));
    g_cfg.dockCount = 0;
    g_cfg.dockPageCount = 4;
    g_dockFolder = g_dockPage = 0;
    AppendCommand(1, 0, 0);
    int moved = AppendCommand(2, 0, 1);
    g_dockPos[moved] = DOCK_PAGE_SIZE + 5;
    g_dockPage = 2;
    int folderIndex = Dock_CreateFolder("Page deletion");
    int folder = g_cfg.dockItems[folderIndex].folderId;
    g_dockPos[folderIndex] = DOCK_PAGE_SIZE * 2 + 7;
    int childA = AppendCommand(3, folder, 0);
    int childB = AppendCommand(4, folder, 1);
    g_dockPos[childB] = DOCK_PAGE_SIZE + 3;
    g_dockPage = 0;
    Check(Dock_DeletePage(0, 1) && g_cfg.dockPageCount == 3 &&
          g_cfg.dockCount == 5 && g_dockPos[moved] == 1 &&
          g_dockPos[folderIndex] == DOCK_PAGE_SIZE + 7 &&
          g_dockPos[childA] == 0 && g_dockPos[childB] == DOCK_PAGE_SIZE + 3 &&
          g_cfg.dockItems[folderIndex].pageCount == 2 && !Dash_ValidateDockConfig(),
          "deleting occupied middle page preserves shortcuts, shifts later pages and leaves folder contents intact");
    Check(Dock_DeletePage(0, 2) && g_cfg.dockPageCount == 2,
          "empty last page can be deleted");
    Check(!Dock_DeletePage(0, 0) && !Dock_DeletePage(0, 2) &&
          !Dock_DeletePage(99999, 1) && g_cfg.dockPageCount == 2,
          "home first page and invalid pages cannot be deleted");
    Check(Dock_DeletePage(folder, 0) && Dock_PageCount(folder) == 1 &&
          g_dockPos[childA] == 0 && g_dockPos[childB] == 3 &&
          !Dock_DeletePage(folder, 0) && !Dash_ValidateDockConfig(),
          "folder first page can be removed while preserving children and its last page");
    Config_Save();
    Config_SetDefaults();
    Config_Load();
    Check(g_cfg.dockCount == 5 && g_cfg.dockPageCount == 2 &&
          Dock_PageCount(folder) == 1 && g_dockPos[moved] == 1 &&
          g_dockPos[folderIndex] == DOCK_PAGE_SIZE + 7 && g_dockPos[childB] == 3 &&
          !Dash_ValidateDockConfig(), "deleted pages and relocated icons persist through actual INI save/load");
    g_cfg.dockCount = 0;
    g_cfg.dockPageCount = 2;
    for (int i = 0; i < DOCK_PAGE_SIZE + 1; i++) AppendCommand(i + 1, 0, 0);
    static DockItem before[MAX_DOCK_ITEMS];
    static int beforePos[MAX_DOCK_ITEMS];
    memcpy(before, g_cfg.dockItems, sizeof(before));
    memcpy(beforePos, g_dockPos, sizeof(beforePos));
    Check(!Dock_DeletePage(0, 1) && g_cfg.dockPageCount == 2 &&
          g_cfg.dockCount == DOCK_PAGE_SIZE + 1 &&
          !memcmp(before, g_cfg.dockItems, sizeof(before)) &&
          !memcmp(beforePos, g_dockPos, sizeof(beforePos)),
          "insufficient capacity rejects deletion without changing any shortcut or position");
    g_cfg = original;
    memcpy(g_dockPos, originalPos, sizeof(originalPos));
}

static void ClickItem(int index)
{
    int local = g_dockPos[index] % DOCK_PAGE_SIZE;
    int x = DockPane_ColX(POS_COL(local)) + g_dpItemW / 2;
    int y = POS_ROW(local) * g_dpStepY + g_dpItemH / 2;
    SendMessageW(g_hDockPane, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
    Pump(); /* Real clicks dispatch pending focus messages before mouse-up. */
    SendMessageW(g_hDockPane, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    Pump();
}

static void ClickButton(HWND button)
{
    RECT rect;
    GetClientRect(button, &rect);
    LPARAM point = MAKELPARAM(rect.right / 2, rect.bottom / 2);
    PostMessageW(g_hDash, WM_SC_REFOCUS, 0, 0);
    SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, point);
    Pump();
    /* Pump may deliver a capture mouse-move using the real desktop cursor,
       outside this synthetic click. Restore the intended pointer position
       before mouse-up so BUTTON keeps its pressed state. */
    SendMessageW(button, WM_MOUSEMOVE, MK_LBUTTON, point);
    SendMessageW(button, WM_LBUTTONUP, 0, point);
}

static int addOpened;
static void CALLBACK ObserveAddMenu(HWND window, UINT msg, UINT_PTR timer, DWORD time)
{
    (void)window; (void)msg; (void)time;
    GUITHREADINFO info = {0};
    info.cbSize = sizeof(info);
    GetGUIThreadInfo(GetCurrentThreadId(), &info);
    addOpened = (info.flags & GUI_POPUPMENUMODE) != 0;
    EndMenu();
    KillTimer(NULL, timer);
}

static void DragItem(int source, int target)
{
    int a = g_dockPos[source] % DOCK_PAGE_SIZE;
    int b = g_dockPos[target] % DOCK_PAGE_SIZE;
    int x1 = DockPane_ColX(POS_COL(a)) + g_dpItemW / 2;
    int y1 = POS_ROW(a) * g_dpStepY + g_dpItemH / 2;
    int x2 = DockPane_ColX(POS_COL(b)) + g_dpItemW / 2;
    int y2 = POS_ROW(b) * g_dpStepY + g_dpItemH / 2;
    SendMessageW(g_hDockPane, WM_LBUTTONDOWN, 0, MAKELPARAM(x1, y1));
    Pump();
    SendMessageW(g_hDockPane, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x2, y2));
    Pump();
    SendMessageW(g_hDockPane, WM_LBUTTONUP, 0, MAKELPARAM(x2, y2));
}

static void CALLBACK FillName(HWND window, UINT msg, UINT_PTR timer, DWORD time)
{
    (void)window; (void)msg; (void)time;
    HWND dialog = GetWindow(g_hDash, GW_ENABLEDPOPUP);
    if (dialog == g_hDash || !GetDlgItem(dialog, 100)) return;
    Check(g_dockDialog && Dashboard_IsVisible() && !Dashboard_HasFocus(),
          "folder-name dialog keeps Dashboard visible and disables key forwarding");
    SetDlgItemTextW(dialog, 100, L"开发工具");
    CaptureWindow(dialog, L"tests\\dashboard-rename-dialog.bmp");
    PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    KillTimer(NULL, timer);
}

static int nameDialogStep, nameDialogMode;
static const wchar_t *themeDialogCapture;
static void CALLBACK ExerciseNameDialog(HWND window, UINT msg, UINT_PTR timer, DWORD time)
{
    (void)window; (void)msg; (void)time;
    HWND dialog = GetWindow(g_hDash, GW_ENABLEDPOPUP);
    HWND edit = GetDlgItem(dialog, 100);
    if (!edit) return;
    if (nameDialogMode == 1) {
        PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
        KillTimer(NULL, timer);
        return;
    }
    if (nameDialogMode == 2) {
        PostMessageW(dialog, WM_COMMAND, 101, 0);
        KillTimer(NULL, timer);
        return;
    }
    if (!nameDialogStep++) {
        wchar_t label[32];
        GetWindowTextW(GetDlgItem(dialog, IDOK), label, 32);
        Check(!wcscmp(label, L"Create"), "new-folder dialog uses Create action");
        CaptureWindow(dialog, L"tests\\dashboard-new-folder.bmp");
        if (themeDialogCapture) CaptureWindow(dialog, themeDialogCapture);
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        DockNameDialog *name = (DockNameDialog*)GetWindowLongPtrW(dialog, DWLP_USER);
        Check(IsWindow(dialog) && name && name->error == 1 && GetFocus() == edit,
              "blank folder name shows inline validation and keeps input focused");
        return;
    }
    SetWindowTextW(edit, L"工作与学习");
    DockNameDialog *name = (DockNameDialog*)GetWindowLongPtrW(dialog, DWLP_USER);
    Check(name && !name->error, "editing clears folder-name validation");
    PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
    KillTimer(NULL, timer);
}

static void TestNameDialog(void)
{
    DockNameDialog name = {0};
    nameDialogMode = nameDialogStep = 0;
    SetTimer(NULL, 0, 40, ExerciseNameDialog);
    Check(Dock_PromptName(g_hDash, &name) && !strcmp(name.text, "工作与学习"),
          "Enter confirms new folder name and preserves Chinese text");
    nameDialogMode = 1;
    SetTimer(NULL, 0, 40, ExerciseNameDialog);
    Check(!Dock_PromptName(g_hDash, &name), "Escape cancels name dialog");
    nameDialogMode = 2;
    SetTimer(NULL, 0, 40, ExerciseNameDialog);
    Check(!Dock_PromptName(g_hDash, &name) && Dashboard_IsVisible(),
          "custom close button cancels dialog without hiding Dashboard");
}

static void TestMissingCommand(void)
{
    SearchBox_SetText(L"");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    wchar_t notice[32];
    GetWindowTextW(g_hSearchNotice, notice, 32);
    Check(!wcscmp(notice, L"Command not found"), "missing-command notice uses English text");
    Check(Dashboard_IsVisible() && !IsWindowVisible(g_hSearchNotice),
          "empty search does not show missing-command notice");
    SearchBox_SetText(L"no_such_command");
    SetFocus(g_hSearchEdit);
    HWND focus = GetFocus();
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Check(IsWindowVisible(g_hSearchNotice) && Dashboard_IsVisible() && !executed &&
          GetFocus() == focus && !wcscmp(g_sbText, L"no_such_command"),
          "unknown Enter shows non-modal notice while preserving focus, query and Dashboard");
    Capture(L"tests\\dashboard-missing-command.bmp");
    ULONGLONG deadline = GetTickCount64() + DASH_NOTICE_MS + 700;
    while (IsWindowVisible(g_hSearchNotice) && GetTickCount64() < deadline) {
        Pump();
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
    Check(!IsWindowVisible(g_hSearchNotice) && Dashboard_IsVisible() && GetFocus() == focus,
          "real timer dismisses notice without user interaction or focus change");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_SPACE, 0);
    Check(IsWindowVisible(g_hSearchNotice) && Dashboard_IsVisible(),
          "unknown Space shows notice instead of closing Dashboard");
    SendMessageW(g_hSearchEdit, WM_CHAR, L'x', 0);
    Check(!IsWindowVisible(g_hSearchNotice), "typing clears notice immediately");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Dashboard_Close();
    Dashboard_Show();
    Check(!IsWindowVisible(g_hSearchNotice) && !g_sbLen,
          "reopening never carries over stale notice or query");
    SearchBox_SetText(L"Test 01");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Check(executed == 1 && !Dashboard_IsVisible(), "valid command still executes and closes Dashboard");
    executed = 0;
    Dashboard_Show();
}

static void TestSmoothEdges(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    HDC windowDC = GetDC(g_hDash);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDC, 48, 48);
    ReleaseDC(g_hDash, windowDC);
    HBITMAP old = SelectObject(dc, bitmap);
    RECT canvas = {0, 0, 48, 48}, shape = {4, 4, 44, 44};
    Dash_ColorRect(dc, canvas, CLR_CARD);
    Dash_RoundFill(dc, &shape, 24, CLR_ACCENT);
    int mixed = 0;
    for (int y = 4; y < 16; y++) for (int x = 4; x < 16; x++) {
        COLORREF pixel = GetPixel(dc, x, y);
        if (pixel != CLR_ACCENT && pixel != CLR_CARD && pixel != CLR_INVALID) mixed++;
    }
    Check(mixed > 0 && GetPixel(dc, 24, 24) == CLR_ACCENT && GetPixel(dc, 0, 0) == CLR_CARD,
          "rounded shape has blended edge pixels and crisp solid interiors");
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    DashSmoothFrame *frame = (DashSmoothFrame*)GetPropW(g_hDash, L"SC_SmoothFrame");
    int smooth = frame != NULL, fractional = 0;
    if (frame) {
        for (int i = 0; i < frame->radius * frame->radius; i++)
            if (frame->coverage[i] > 0 && frame->coverage[i] < 255) fractional++;
        for (int i = 0; i < 4; i++)
            smooth &= !IsWindowEnabled(frame->corner[i]) &&
                      (GetWindowLongPtrW(frame->corner[i], GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0;
    }
    Check(smooth && fractional > 0, "outer corners use fractional alpha and never accept activation or input");
    Dashboard_Close();
    int hidden = 1;
    if (frame) for (int i = 0; i < 4; i++) hidden &= !IsWindowVisible(frame->corner[i]);
    Check(hidden, "closing Dashboard hides all outer corner surfaces");
    Dashboard_Show();
}

static void TestThemes(void)
{
    static AppConfig saved;
    saved = g_cfg;
    HWND dashboard = g_hDash;
    HFONT font = g_hSearchFont;
    DWORD generation = g_iconRequest.generation;
    SearchBox_SetText(L"Test 01");
    Dash_SetDockView(0, 1);
    /* Startup icon extraction runs concurrently and also creates GDI objects;
       let it settle before measuring resources owned by theme switching. */
    ULONGLONG deadline = GetTickCount64() + 5000;
    int iconsReady = 0;
    while (GetTickCount64() < deadline) {
        Pump();
        iconsReady = 1;
        for (int i = 0; i < g_cfg.dockCount; i++) if (!g_hDockIcons[i]) iconsReady = 0;
        if (iconsReady) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
    Check(iconsReady, "startup icons settle before measuring theme resource usage");
    /* Include each palette's first repaint before measuring steady resource
       usage, then compare the same theme after 60 more switches. */
    for (int i = 0; i < DASH_THEME_COUNT; i++) {
        g_cfg.dashboardTheme = i;
        Dashboard_ApplyTheme();
    }
    DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 60; i++) {
        g_cfg.dashboardTheme = i % DASH_THEME_COUNT;
        Dashboard_ApplyTheme();
    }
    Check(g_hDash == dashboard && g_hSearchFont == font &&
          g_iconRequest.generation == generation && !wcscmp(g_sbText, L"Test 01") && g_dockPage == 1,
          "theme changes preserve the window, fonts, icon cache, search and page");
    DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    printf("Theme GDI resources: %lu -> %lu\n", (unsigned long)before, (unsigned long)after);
    Check(after == before,
          "60 theme changes do not leak drawing resources");
    Dashboard_Close();
    UI_Show(NULL);
    HWND settings = FindWindowW(L"ShortCutterSettings", NULL);
    SelectSettingsTab(settings, 1);
    HWND combo = GetDlgItem(settings, 3036);
    Check(settings && combo && SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3,
          "Dashboard settings exposes exactly Black, White and Gray");
    RECT themeRect, saveRect;
    GetWindowRect(combo, &themeRect);
    GetWindowRect(GetDlgItem(settings, 4000), &saveRect);
    Check(themeRect.bottom < saveRect.top && IsWindowVisible(combo),
          "theme selector fits above Save & Apply without overlap");
    const int themeIds[] = {DASH_THEME_BLACK, DASH_THEME_WHITE, DASH_THEME_GRAY};
    for (int row = 0; row < 3; row++) {
        SendMessageW(combo, CB_SETCURSEL, row, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(3036, CBN_SELCHANGE), (LPARAM)combo);
        Check(g_dashPalette == &g_dashPalettes[g_cfg.dashboardTheme],
              "selecting a theme previews the draft without changing the running Dashboard");
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(4000, BN_CLICKED), 0);
        Pump();
        Config_SetDefaults();
        Config_Load();
        Check(g_cfg.dashboardTheme == themeIds[row] && g_cfg.dashboardOpacity == saved.dashboardOpacity,
              "Save & Apply persists each theme while preserving opacity");
    }
    Check(applySideEffects == 0, "unchanged startup settings never trigger system actions");
    SelectSettingsTab(settings, 2);
    Check(!IsWindowVisible(combo) && !IsWindowVisible(GetDlgItem(settings, 3037)),
          "theme selector and label hide on other Settings tabs");
    DestroyWindow(settings);
    WritePrivateProfileStringW(L"Settings", L"DashboardTheme", L"999", testIni);
    Config_Load();
    Check(g_cfg.dashboardTheme == DASH_THEME_WHITE, "invalid persisted theme recovers to White");
    g_cfg = saved;
    Config_Save();
    Dashboard_ApplyTheme();
    UI_Show(NULL);
    settings = FindWindowW(L"ShortCutterSettings", NULL);
    SelectSettingsTab(settings, 1);
    combo = GetDlgItem(settings, 3036);
    LRESULT row = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    Check(SendMessageW(combo, CB_GETITEMDATA, row, 0) == saved.dashboardTheme,
          "reopening Settings restores the selected theme");
    CaptureWindow(settings, L"tests\\dashboard-theme-settings.bmp");
    DestroyWindow(settings);
    int savedDpi = g_dpiScale;
    const int testDpis[] = {96, 144, 192};
    for (int i = 0; i < 3; i++) {
        g_dpiScale = testDpis[i];
        UI_Show(NULL);
        settings = FindWindowW(L"ShortCutterSettings", NULL);
        SelectSettingsTab(settings, 1);
        HWND label = GetDlgItem(settings, 3037);
        HWND opacityLabel = GetDlgItem(settings, 3035);
        GetWindowRect(GetDlgItem(settings, 3036), &themeRect);
        GetWindowRect(GetDlgItem(settings, 4000), &saveRect);
        Check(SettingsTextFits(label) && SettingsTextFits(opacityLabel) &&
              themeRect.bottom < saveRect.top,
              "Dashboard settings labels and theme selector fit at 100%, 150% and 200% scaling");
        DestroyWindow(settings);
    }
    g_dpiScale = savedDpi;
    Dashboard_Show();
    SearchBox_SetText(L"");
}

static void TestQuickInputSettings(void)
{
    static AppConfig original;
    original = g_cfg;
    int originalDpi = g_dpiScale;
    Dashboard_Close();
    const int dpis[] = {96,144,192};
    const int ids[] = {3038,3039,3040,3041};
    for (int i = 0; i < 3; i++) {
        g_dpiScale = dpis[i];
        g_cfg.dashboardQuickInput = 0;
        g_cfg.dashboardQuickDesktop = g_cfg.dashboardQuickExplorer = 1;
        UI_Show(NULL);
        HWND settings = FindWindowW(L"ShortCutterSettings",NULL);
        SelectSettingsTab(settings, 1);
        HWND master = GetDlgItem(settings,3038);
        HWND desktop = GetDlgItem(settings,3039), explorer = GetDlgItem(settings,3040);
        Check(master && desktop && explorer && IsWindowVisible(master) &&
              SendMessageW(master,BM_GETCHECK,0,0) == BST_UNCHECKED &&
              !IsWindowEnabled(desktop) && !IsWindowEnabled(explorer),
              "Dashboard exposes a disabled-by-default master and separate Desktop/Explorer scopes");
        int fits = 1;
        RECT client,save; GetClientRect(settings,&client); GetWindowRect(GetDlgItem(settings,4000),&save);
        RECT saveClient = save;
        MapWindowPoints(NULL,settings,(POINT*)&saveClient,2);
        fits &= saveClient.bottom <= client.bottom;
        for (int n = 0; n < 4; n++) {
            HWND control = GetDlgItem(settings,ids[n]);
            RECT bounds; GetWindowRect(control,&bounds);
            fits &= SettingsTextFits(control) && bounds.bottom < save.top;
            MapWindowPoints(NULL,settings,(POINT*)&bounds,2);
            fits &= bounds.right <= client.right && bounds.bottom <= client.bottom;
        }
        Check(fits,"quick input controls fit without overlap at 100%, 150% and 200% scaling");
        SendMessageW(master,BM_CLICK,0,0);
        Check(IsWindowEnabled(desktop) && IsWindowEnabled(explorer),
              "master checkbox enables both scope controls immediately");
        SendMessageW(desktop,BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(settings,WM_COMMAND,MAKEWPARAM(4000,BN_CLICKED),0);
        Pump();
        Config_SetDefaults(); Config_Load();
        Check(g_cfg.dashboardQuickInput && !g_cfg.dashboardQuickDesktop && g_cfg.dashboardQuickExplorer,
              "Save & Apply persists the master and each scope independently through INI reload");
        SelectSettingsTab(settings, 2);
        Check(!IsWindowVisible(master) && !IsWindowVisible(desktop) && !IsWindowVisible(explorer) &&
              !IsWindowVisible(GetDlgItem(settings,3041)),"quick input controls hide on other Settings tabs");
        if (i == 0) {
            SelectSettingsTab(settings, 1);
            CaptureWindow(settings,L"tests\\dashboard-quick-input-settings.bmp");
        }
        DestroyWindow(settings);
        UI_Show(NULL);
        settings = FindWindowW(L"ShortCutterSettings",NULL);
        SelectSettingsTab(settings, 1);
        Check(SendMessageW(GetDlgItem(settings,3038),BM_GETCHECK,0,0) == BST_CHECKED &&
              SendMessageW(GetDlgItem(settings,3039),BM_GETCHECK,0,0) == BST_UNCHECKED &&
              SendMessageW(GetDlgItem(settings,3040),BM_GETCHECK,0,0) == BST_CHECKED,
              "reopened Settings restores quick input selections");
        DestroyWindow(settings);
    }
    WritePrivateProfileStringW(L"Settings",L"DashboardQuickInput",L"99",testIni);
    Config_Load();
    Check(!g_cfg.dashboardQuickInput,"invalid persisted master value cannot enable interception");
    g_cfg = original; g_dpiScale = originalDpi;
    Config_Save(); Dashboard_Show();
}

static int SettingsPageFits(HWND settings, const int *ids, int count, const RECT *panel)
{
    int fits = 1;
    for (int i = 0; i < count; i++) {
        HWND control = GetDlgItem(settings, ids[i]);
        if (!control || !IsWindowVisible(control)) {
            printf("Missing or hidden Settings control: %d\n", ids[i]);
            fits = 0;
            continue;
        }
        RECT bounds;
        GetWindowRect(control, &bounds);
        MapWindowPoints(NULL, settings, (POINT *)&bounds, 2);
        if (bounds.left < panel->left || bounds.top < panel->top ||
            bounds.right > panel->right || bounds.bottom > panel->bottom) {
            printf("Settings control %d outside page: %ld,%ld-%ld,%ld; page %ld,%ld-%ld,%ld\n",
                   ids[i], bounds.left, bounds.top, bounds.right, bounds.bottom,
                   panel->left, panel->top, panel->right, panel->bottom);
            fits = 0;
        }
        fits &= SettingsTextFits(control);
        wchar_t className[32];
        GetClassNameW(control, className, 32);
        int isGroup = !wcscmp(className, L"Button") &&
            ((GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX);
        for (int j = 0; j < i; j++) {
            HWND previousControl = GetDlgItem(settings, ids[j]);
            GetClassNameW(previousControl, className, 32);
            if (isGroup || (!wcscmp(className, L"Button") &&
                ((GetWindowLongPtrW(previousControl, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX))) continue;
            RECT previous, overlap;
            GetWindowRect(previousControl, &previous);
            MapWindowPoints(NULL, settings, (POINT *)&previous, 2);
            if (IntersectRect(&overlap, &bounds, &previous)) {
                printf("Overlapping Settings controls: %d and %d\n", ids[i], ids[j]);
                fits = 0;
            }
        }
    }
    return fits;
}

static void SetSettingsCombo(HWND settings, int id, int row)
{
    HWND combo = GetDlgItem(settings, id);
    SendMessageW(combo, CB_SETCURSEL, row, 0);
    SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), (LPARAM)combo);
    Pump();
}

static void TestLanguageSettings(void)
{
    static AppConfig original;
    original = g_cfg;
    int originalDpi = g_dpiScale;
    Dashboard_Close();
    Config_SetDefaults();
    Check(g_cfg.uiLanguage == UI_LANG_ENGLISH,
          "new configurations default to English without changing existing language expectations");
    const wchar_t *invalidValues[] = {L"-1", L"2", L"999", L"bad"};
    for (int i = 0; i < 4; i++) {
        WritePrivateProfileStringW(L"Settings", L"UILanguage", invalidValues[i], testIni);
        Config_Load();
        Check(g_cfg.uiLanguage == UI_LANG_ENGLISH, "invalid persisted language safely recovers to English");
    }
    g_cfg = original;
    g_cfg.uiLanguage = UI_LANG_ENGLISH;
    int customIndex = 0;
    for (int i = 0; i < g_cfg.commandCount; i++) {
        if (!g_cfg.commands[i].locked && g_cfg.commands[i].type != CMD_SCRIPT) { customIndex = i; break; }
    }
    strcpy(g_cfg.commands[customIndex].group, "Language test");
    g_cfg.gestureCount = 1;
    memset(&g_cfg.gestures[0], 0, sizeof(g_cfg.gestures[0]));
    strcpy(g_cfg.gestures[0].name, "Gesture survives translation");
    strcpy(g_cfg.gestures[0].chains[0], "012");
    g_cfg.gestures[0].chainCount = 1;
    g_cfg.gestures[0].commandId = g_cfg.commands[0].id;
    g_cfg.gestures[0].enabled = 1;
    g_cfg.gestures[0].zone = ZONE_MAIN;
    g_cfg.hotkeyCount = 1;
    memset(&g_cfg.hotkeys[0], 0, sizeof(g_cfg.hotkeys[0]));
    strcpy(g_cfg.hotkeys[0].label, "Ctrl+F12");
    strcpy(g_cfg.hotkeys[0].description, "Description survives translation");
    g_cfg.hotkeys[0].commandId = g_cfg.commands[0].id;
    g_cfg.hotkeys[0].enabled = 1;
    Config_Save();
    g_dpiScale = 144;
    Dashboard_ApplyLanguage();
    SearchBox_SetText(L"Test");
    HWND dashboard = g_hDash;
    DWORD iconGeneration = g_iconRequest.generation;
    UI_Show(NULL);
    HWND settings = FindWindowW(L"ShortCutterSettings", NULL);
    HWND language = GetDlgItem(settings, 3047);
    wchar_t firstChoice[32], secondChoice[32];
    SendMessageW(language, CB_GETLBTEXT, 0, (LPARAM)firstChoice);
    SendMessageW(language, CB_GETLBTEXT, 1, (LPARAM)secondChoice);
    Check(language && IsWindowVisible(language) && SendMessageW(language, CB_GETCOUNT, 0, 0) == 2 &&
          !wcscmp(firstChoice, L"中文") && !wcscmp(secondChoice, L"English") &&
          SendMessageW(language, CB_GETITEMDATA, 0, 0) == UI_LANG_CHINESE &&
          SendMessageW(language, CB_GETITEMDATA, 1, 0) == UI_LANG_ENGLISH &&
          SendMessageW(language, CB_GETCURSEL, 0, 0) == 1,
          "General offers stable Chinese and English choices with explicit persisted language IDs");

    SetWindowTextW(GetDlgItem(settings, 3002), L"142");
    SetWindowTextW(GetDlgItem(settings, 3003), L"81");
    SetWindowTextW(GetDlgItem(settings, 3023), L"6");
    SetSettingsCombo(settings, 3001, 0);
    SetSettingsCombo(settings, 3036, 2); /* Gray */
    SendMessageW(GetDlgItem(settings, 3033), TBM_SETPOS, TRUE, 76);
    SendMessageW(GetDlgItem(settings, 3004), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(settings, 3005), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(settings, 3038), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(GetDlgItem(settings, 3039), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(settings, 3040), BM_SETCHECK, BST_CHECKED, 0);
    SetSettingsCombo(settings, 5002, 1); /* Custom */
    int groupRow = (int)SendMessageW(GetDlgItem(settings,5008),CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)L"Language test");
    SetSettingsCombo(settings, 5008, groupRow); /* User group */
    SetSettingsCombo(settings, 5007, 1); /* Main */
    const int listIds[] = {2100, 2200, 2300};
    for (int i = 0; i < 3; i++) {
        HWND list = GetDlgItem(settings, listIds[i]);
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    Check(groupRow >= 1 && ListView_GetItemCount(GetDlgItem(settings,2100)) == 1 &&
          ListView_GetNextItem(GetDlgItem(settings,2100),-1,LVNI_SELECTED) == 0,
          "language-state fixture starts with a selected custom command in its named user group");
    DWORD checksum = IniChecksum();
    WIN32_FILE_ATTRIBUTE_DATA before = {0}, after = {0};
    GetFileAttributesExW(testIni, GetFileExInfoStandard, &before);
    int sideEffectsBefore = applySideEffects, trayBefore = languageTrayUpdates;
    const int languages[] = {UI_LANG_CHINESE, UI_LANG_ENGLISH, UI_LANG_CHINESE, UI_LANG_ENGLISH, UI_LANG_CHINESE};
    const wchar_t *tabNames[] = {L"General", L"Dashboard", L"Commands", L"Gestures", L"Hotkeys"};
    const wchar_t *chineseTabs[] = {L"常规", L"仪表盘", L"命令", L"手势", L"热键"};
    const int pageIds[] = {3047, 3036, 2100, 2200, 2300};
    int translates = 1, preserves = 1;
    for (int page = 0; page < 5; page++) {
        SelectSettingsTab(settings, page);
        SelectSettingsLanguage(settings, languages[page]);
        translates &= UI_GetLanguage() == languages[page] && g_cfg.uiLanguage == UI_LANG_ENGLISH &&
            LocalizedControl(settings, L"ShortCutter - Settings") &&
            LocalizedControlPair(GetDlgItem(settings, 3046), L"Language:", L"语言：") &&
            LocalizedControlPair(GetDlgItem(settings, 3050), L"Preferences", L"常规选项") &&
            LocalizedControl(GetDlgItem(settings, 3012), L"Mouse button:") &&
            LocalizedControl(GetDlgItem(settings, 3037), L"Theme:") &&
            LocalizedControl(GetDlgItem(settings, 3038), L"Type letters to open Dashboard") &&
            LocalizedControl(GetDlgItem(settings, 2101), L"New...") &&
            LocalizedControl(GetDlgItem(settings, 2201), L"Record...") &&
            LocalizedControl(GetDlgItem(settings, 2301), L"New...") &&
            LocalizedControlPair(GetDlgItem(settings, 5001), L"Help", L"帮助") &&
            LocalizedControlPair(GetDlgItem(settings, 4000), L"Save && Apply", L"保存并应用");
        for (int tabIndex = 0; tabIndex < 5; tabIndex++) {
            wchar_t text[64] = {0}; TCITEMW item = {0};
            item.mask = TCIF_TEXT; item.pszText = text; item.cchTextMax = 64;
            SendMessageW(GetDlgItem(settings, 2000), TCM_GETITEMW, tabIndex, (LPARAM)&item);
            translates &= !wcscmp(text, UI_GetLanguage() == UI_LANG_CHINESE ? chineseTabs[tabIndex] : tabNames[tabIndex]);
        }
        const wchar_t *themes[] = {L"Black", L"White", L"Gray"};
        const wchar_t *chineseThemes[] = {L"黑色", L"白色", L"灰色"};
        for (int row = 0; row < 3; row++)
            translates &= LocalizedComboPair(GetDlgItem(settings, 3036), row, themes[row], chineseThemes[row]);
        translates &= LocalizedComboPair(GetDlgItem(settings, 3001), 0, L"Right Button", L"右键") &&
            LocalizedComboPair(GetDlgItem(settings, 3001), 1, L"Middle Button", L"中键") &&
            LocalizedComboPair(GetDlgItem(settings, 5002), 0, L"All", L"全部") &&
            LocalizedComboPair(GetDlgItem(settings, 5002), 1, L"Custom", L"自定义") &&
            LocalizedComboPair(GetDlgItem(settings, 5002), 2, L"Built-in", L"内置") &&
            LocalizedComboPair(GetDlgItem(settings, 5002), 3, L"Scripts", L"脚本") &&
            LocalizedComboPair(GetDlgItem(settings, 5008), 0, L"All", L"全部") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 0, L"All", L"全部") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 1, L"Main", L"主区域") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 2, L"↖ TL", L"↖ 左上") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 3, L"↗ TR", L"↗ 右上") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 4, L"↙ BL", L"↙ 左下") &&
            LocalizedComboPair(GetDlgItem(settings, 5007), 5, L"↘ BR", L"↘ 右下") &&
            LocalizedColumnPair(GetDlgItem(settings, 2100), 0, L"Name", L"名称") &&
            LocalizedColumnPair(GetDlgItem(settings, 2100), 1, L"Type", L"类型") &&
            LocalizedColumnPair(GetDlgItem(settings, 2100), 2, L"Group", L"分组") &&
            LocalizedColumnPair(GetDlgItem(settings, 2100), 3, L"Target", L"目标") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 0, L"Name", L"名称") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 1, L"Chain", L"方向链") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 2, L"Mod", L"修饰") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 3, L"Zone", L"区域") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 4, L"Command", L"命令") &&
            LocalizedColumnPair(GetDlgItem(settings, 2200), 5, L"On", L"启用") &&
            LocalizedColumnPair(GetDlgItem(settings, 2300), 0, L"Hotkey", L"快捷键") &&
            LocalizedColumnPair(GetDlgItem(settings, 2300), 1, L"Description", L"说明") &&
            LocalizedColumnPair(GetDlgItem(settings, 2300), 2, L"Command", L"命令") &&
            LocalizedColumnPair(GetDlgItem(settings, 2300), 3, L"On", L"启用");
        int pagePreserves = TabCtrl_GetCurSel(GetDlgItem(settings, 2000)) == page &&
            IsWindowVisible(GetDlgItem(settings, pageIds[page])) &&
            WindowTextIs(GetDlgItem(settings, 3002), L"142") &&
            WindowTextIs(GetDlgItem(settings, 3003), L"81") &&
            WindowTextIs(GetDlgItem(settings, 3023), L"6") &&
            SendMessageW(GetDlgItem(settings, 3001), CB_GETCURSEL, 0, 0) == 0 &&
            SendMessageW(GetDlgItem(settings, 3036), CB_GETCURSEL, 0, 0) == 2 &&
            SendMessageW(GetDlgItem(settings, 3033), TBM_GETPOS, 0, 0) == 76 &&
            SendMessageW(GetDlgItem(settings, 3004), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
            SendMessageW(GetDlgItem(settings, 3005), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
            SendMessageW(GetDlgItem(settings, 3038), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SendMessageW(GetDlgItem(settings, 3039), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
            SendMessageW(GetDlgItem(settings, 3040), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SendMessageW(GetDlgItem(settings, 5002), CB_GETCURSEL, 0, 0) == 1 &&
            SendMessageW(GetDlgItem(settings, 5008), CB_GETCURSEL, 0, 0) == groupRow &&
            SendMessageW(GetDlgItem(settings, 5007), CB_GETCURSEL, 0, 0) == 1 &&
            g_hDash == dashboard && g_iconRequest.generation == iconGeneration && !wcscmp(g_sbText, L"Test");
        for (int i = 0; i < 3; i++)
            pagePreserves &= ListView_GetNextItem(GetDlgItem(settings, listIds[i]), -1, LVNI_SELECTED) == 0;
        if (!pagePreserves) {
            printf("Language preservation details: page=%d active=%d visible=%d; combo=%ld,%ld; filters=%ld,%ld,%ld; lists=%d,%d,%d; dashboard=%d generation=%lu/%lu search=%ls\n",
                page, TabCtrl_GetCurSel(GetDlgItem(settings, 2000)), IsWindowVisible(GetDlgItem(settings,pageIds[page])),
                (long)SendMessageW(GetDlgItem(settings,3001),CB_GETCURSEL,0,0),
                (long)SendMessageW(GetDlgItem(settings,3036),CB_GETCURSEL,0,0),
                (long)SendMessageW(GetDlgItem(settings,5002),CB_GETCURSEL,0,0),
                (long)SendMessageW(GetDlgItem(settings,5008),CB_GETCURSEL,0,0),
                (long)SendMessageW(GetDlgItem(settings,5007),CB_GETCURSEL,0,0),
                ListView_GetNextItem(GetDlgItem(settings,2100),-1,LVNI_SELECTED),
                ListView_GetNextItem(GetDlgItem(settings,2200),-1,LVNI_SELECTED),
                ListView_GetNextItem(GetDlgItem(settings,2300),-1,LVNI_SELECTED),
                g_hDash == dashboard, (unsigned long)g_iconRequest.generation, (unsigned long)iconGeneration, g_sbText);
        }
        preserves &= pagePreserves;
    }
    Check(translates, "draft language switches update all Settings pages and leave the running language unchanged");
    Check(preserves, "repeated language switches preserve active tab, unsaved edits, checks, filters, selected rows and Dashboard search");
    GetFileAttributesExW(testIni, GetFileExInfoStandard, &after);
    Check(IniChecksum() == checksum && !CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) &&
          applySideEffects == sideEffectsBefore && languageTrayUpdates == trayBefore,
          "language preview stays inside Settings without tray or system writes");

    for (int targetLanguage = UI_LANG_ENGLISH; targetLanguage < UI_LANG_COUNT; targetLanguage++) {
        SelectSettingsLanguage(settings, targetLanguage);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(4000, BN_CLICKED), 0);
        Pump();
        Config_SetDefaults(); Config_Load();
        Check(g_cfg.uiLanguage == targetLanguage &&
              (int)GetPrivateProfileIntW(L"Settings", L"UILanguage", -1, testIni) == targetLanguage &&
              g_cfg.minGesture == 142 && g_cfg.matchThreshold == 81 && g_cfg.trailGlowWidth == 6 &&
              g_cfg.mouseButton == 1 && !g_cfg.trailEnabled && !g_cfg.zoneLinesEnabled &&
              g_cfg.dashboardTheme == DASH_THEME_GRAY && g_cfg.dashboardOpacity == 76 &&
              g_cfg.dashboardQuickInput && !g_cfg.dashboardQuickDesktop && g_cfg.dashboardQuickExplorer,
              "Save & Apply persists each language and the unsaved edits from every settings page");
        DestroyWindow(settings);
        UI_Show(NULL);
        settings = FindWindowW(L"ShortCutterSettings", NULL);
        HWND reopened = GetDlgItem(settings, 3047);
        LRESULT row = SendMessageW(reopened, CB_GETCURSEL, 0, 0);
        Check(SendMessageW(reopened, CB_GETITEMDATA, row, 0) == targetLanguage &&
              LocalizedControlPair(GetDlgItem(settings, 3046), L"Language:", L"语言："),
              "reopening Settings restores the persisted language and localized controls");
    }
    SelectSettingsLanguage(settings,UI_LANG_CHINESE);
    SendMessageW(settings,WM_COMMAND,MAKEWPARAM(2101,BN_CLICKED),0);Pump();
    HWND commandDialog=FindWindowW(L"SC_CmdDlg",NULL);
    SetWindowTextW(GetDlgItem(commandDialog,5100),L"未保存的命令名称");
    Check(commandDialog && !IsWindowEnabled(settings) && WindowTextIs(commandDialog,L"新建命令"),"command editor owns a modal draft and inherits the Settings language");
    CaptureWindow(commandDialog,L"tests\\command-dialog-language-zh.bmp");
    SelectSettingsLanguage(settings,UI_LANG_ENGLISH);
    Check(UI_GetLanguage()==UI_LANG_CHINESE && WindowTextIs(GetDlgItem(commandDialog,5100),L"未保存的命令名称"),"disabled parent cannot change the language or edit another object during an editor session");
    DestroyWindow(commandDialog);
    SelectSettingsLanguage(settings,UI_LANG_ENGLISH);
    SendMessageW(settings,WM_COMMAND,MAKEWPARAM(5001,BN_CLICKED),0);Pump();
    HWND helpDialog=FindWindowW(L"SC_HelpDlg",NULL);wchar_t helpText[8192];GetDlgItemTextW(helpDialog,5402,helpText,8192);
    Check(helpDialog && WindowTextIs(helpDialog,L"ShortCutter - Help") && wcsstr(helpText,L"4.1.0"),"searchable topic Help uses the selected language and current version");
    DestroyWindow(helpDialog);DestroyWindow(settings);
    g_cfg = original;
    g_dpiScale = originalDpi;
    Config_Save();
    Dashboard_ApplyLanguage();
    Dashboard_Show();
    SearchBox_SetText(L"");
}

static void TestSettingsLayout(void)
{
    static AppConfig original;
    original = g_cfg;
    int originalDpi = g_dpiScale;
    Dashboard_Close();
    const int general[] = {2550,2700,3001,3002,3003,3004,3005,3010,3011,3012,3013,
        3020,3021,3022,3023,3024,3025,3026,3028,3029,3030,3031,3032,
        3042,3043,3044,3046,3047,3050,3051,3052};
    const int dashboard[] = {3033,3034,3035,3036,3037,3038,3039,3040,3041,3045,3053,3054};
    const int commands[] = {5005,5002,5009,5008,2100,2101,2102,2103,2104};
    const int gestures[] = {5006,5007,2200,2201,2202,2203};
    const int hotkeys[] = {2300,2301,2302,2303};
    const int *pages[] = {general,dashboard,commands,gestures,hotkeys};
    const int counts[] = {(int)(sizeof(general)/sizeof(*general)),
        (int)(sizeof(dashboard)/sizeof(*dashboard)),(int)(sizeof(commands)/sizeof(*commands)),
        (int)(sizeof(gestures)/sizeof(*gestures)),(int)(sizeof(hotkeys)/sizeof(*hotkeys))};
    const wchar_t *captures[] = {L"tests\\settings-layout-general.bmp", L"tests\\settings-layout-dashboard.bmp",
        L"tests\\settings-layout-commands.bmp", L"tests\\settings-layout-gestures.bmp", L"tests\\settings-layout-hotkeys.bmp"};
    const int dpis[] = {96,144,192};
    for (int language = UI_LANG_ENGLISH; language < UI_LANG_COUNT; language++) {
    g_cfg.uiLanguage = language;
    for (int d = 0; d < 3; d++) {
        g_dpiScale = dpis[d];
        UI_Show(NULL);
        HWND settings = FindWindowW(L"ShortCutterSettings", NULL);
        HWND tab = GetDlgItem(settings, 2000);
        Check(TabCtrl_GetItemCount(tab) == 5,
              "Settings exposes General, Dashboard, Commands, Gestures and Hotkeys at every scale");
        int headersFit = 1;
        RECT tabClient; GetClientRect(tab,&tabClient);
        HDC tabDc = GetDC(tab);
        HFONT oldFont = SelectObject(tabDc,(HFONT)SendMessageW(tab,WM_GETFONT,0,0));
        const wchar_t *tabNames[] = {L"General",L"Dashboard",L"Commands",L"Gestures",L"Hotkeys"};
        const wchar_t *chineseTabs[] = {L"常规",L"仪表盘",L"命令",L"手势",L"热键"};
        for (int page = 0; page < 5; page++) {
            RECT itemRect = {0}; TabCtrl_GetItemRect(tab,page,&itemRect);
            wchar_t caption[32] = {0}; TCITEMW item = {0};
            item.mask = TCIF_TEXT; item.pszText = caption; item.cchTextMax = 32;
            SendMessageW(tab,TCM_GETITEMW,page,(LPARAM)&item);
            SIZE size; GetTextExtentPoint32W(tabDc,caption,(int)wcslen(caption),&size);
            headersFit &= !wcscmp(caption,language == UI_LANG_CHINESE ? chineseTabs[page] : tabNames[page]) && itemRect.left >= 0 &&
                itemRect.right <= tabClient.right && size.cx + MulDiv(12,g_dpiScale,96) <= itemRect.right-itemRect.left &&
                size.cy <= itemRect.bottom-itemRect.top;
        }
        SelectObject(tabDc,oldFont); ReleaseDC(tab,tabDc);
        Check(headersFit,"all five tab titles fit on one header row at every scale");
        RECT panel;
        GetClientRect(tab, &panel);
        TabCtrl_AdjustRect(tab, FALSE, &panel);
        MapWindowPoints(tab, settings, (POINT *)&panel, 2);
        int allFit = 1, correctVisibility = 1;
        for (int page = 0; page < 5; page++) {
            SelectSettingsTab(settings, page);
            allFit &= SettingsPageFits(settings, pages[page], counts[page], &panel);
            for (int other = 0; other < 5; other++) {
                for (int n = 0; n < counts[other]; n++) {
                    HWND control = GetDlgItem(settings, pages[other][n]);
                    correctVisibility &= control && !!IsWindowVisible(control) == (other == page);
                }
            }
            if (d == 1 && language == UI_LANG_ENGLISH) CaptureWindow(settings, captures[page]);
        }
        Check(allFit, "every Settings page fits its panel with readable labels and no control overlap at 100%, 150% and 200%");
        Check(correctVisibility, "switching all five tabs shows only controls from the selected page");
        const int listIds[] = {2100,2200,2300};
        const int buttonIds[] = {2101,2201,2301};
        int listsFill = 1;
        for (int n = 0; n < 3; n++) {
            RECT list,button;
            GetWindowRect(GetDlgItem(settings,listIds[n]),&list);
            GetWindowRect(GetDlgItem(settings,buttonIds[n]),&button);
            MapWindowPoints(NULL,settings,(POINT*)&list,2);
            MapWindowPoints(NULL,settings,(POINT*)&button,2);
            listsFill &= list.bottom <= button.top &&
                button.top - list.bottom <= MulDiv(44,g_dpiScale,96) && GetDlgItem(settings,5004) &&
                panel.bottom - button.bottom <= MulDiv(12,g_dpiScale,96);
        }
        Check(listsFill,"lists fill the page above the full-target details and action row");
        if (d == 1 && language == UI_LANG_ENGLISH) {
            SelectSettingsTab(settings, 0);
            HWND trail = GetDlgItem(settings, 3004), zone = GetDlgItem(settings, 3005);
            if (SendMessageW(trail,BM_GETCHECK,0,0) != BST_CHECKED) SendMessageW(trail,BM_CLICK,0,0);
            if (SendMessageW(zone,BM_GETCHECK,0,0) != BST_CHECKED) SendMessageW(zone,BM_CLICK,0,0);
            const int trailChildren[] = {3022,3023,3024,3025,3026,3028,3030,3031};
            const int zoneChildren[] = {3020,3021,3032};
            int enabled = 1, disabled = 1;
            for (int n = 0; n < 8; n++) enabled &= IsWindowEnabled(GetDlgItem(settings,trailChildren[n]));
            for (int n = 0; n < 3; n++) enabled &= IsWindowEnabled(GetDlgItem(settings,zoneChildren[n]));
            Check(enabled,"enabling trail and zone boundaries enables their color and width controls");
            const int colorIds[] = {3020,3022,3024};
            int namedColors = 1;
            for (int n = 0; n < 3; n++) {
                wchar_t caption[32]; GetWindowTextW(GetDlgItem(settings,colorIds[n]),caption,32);
                namedColors &= caption[0] == L'#' && wcslen(caption) == 7;
            }
            Check(namedColors,"each color selector exposes a readable hex value alongside its swatch");
            SetWindowTextW(GetDlgItem(settings,3002),L"144");
            SetWindowTextW(GetDlgItem(settings,3003),L"82");
            SetWindowTextW(GetDlgItem(settings,3023),L"7");
            SetWindowTextW(GetDlgItem(settings,3025),L"4");
            SetWindowTextW(GetDlgItem(settings,3021),L"3");
            SendMessageW(trail,BM_CLICK,0,0);
            SendMessageW(zone,BM_CLICK,0,0);
            for (int n = 0; n < 8; n++) disabled &= !IsWindowEnabled(GetDlgItem(settings,trailChildren[n]));
            for (int n = 0; n < 3; n++) disabled &= !IsWindowEnabled(GetDlgItem(settings,zoneChildren[n]));
            Check(disabled,"disabling trail and zone boundaries greys their dependent color and width controls");
            SelectSettingsTab(settings,1);
            SendMessageW(GetDlgItem(settings,3033),TBM_SETPOS,TRUE,77);
            SendMessageW(GetDlgItem(settings,3038),BM_SETCHECK,BST_CHECKED,0);
            SendMessageW(GetDlgItem(settings,3039),BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(GetDlgItem(settings,3040),BM_SETCHECK,BST_CHECKED,0);
            SelectSettingsTab(settings,2);
            SendMessageW(settings,WM_COMMAND,MAKEWPARAM(4000,BN_CLICKED),0);
            Pump();
            Config_SetDefaults(); Config_Load();
            Check(g_cfg.minGesture == 144 && g_cfg.matchThreshold == 82 &&
                  !g_cfg.trailEnabled && !g_cfg.zoneLinesEnabled && g_cfg.trailGlowWidth == 7 &&
                  g_cfg.trailInnerWidth == 4 && g_cfg.zoneLineWidth == 3 &&
                  g_cfg.trailGlowColor == original.trailGlowColor &&
                  g_cfg.trailInnerColor == original.trailInnerColor &&
                  g_cfg.zoneLineColor == original.zoneLineColor &&
                  g_cfg.dashboardOpacity == 77 && g_cfg.dashboardQuickInput &&
                  !g_cfg.dashboardQuickDesktop && g_cfg.dashboardQuickExplorer,
                  "saving from Commands preserves hidden General and Dashboard edits, including disabled appearance values");
        }
        DestroyWindow(settings);
    }
    }
    g_cfg = original;
    g_dpiScale = originalDpi;
    Config_Save();
    Dashboard_Show();
}

static int Preview(void)
{
    /* Work on a copy, including any normalization saves made by Dashboard. */
    if (!CopyFileW(L"ShortCutter.ini", testIni, FALSE)) return 1;
    Config_SetDefaults();
    Config_Load();
    int configuredOpacity = g_cfg.dashboardOpacity;
    int configuredLanguage = g_cfg.uiLanguage;
    g_cfg.dashboardOpacity = 100; /* inspect edge quality against a quiet backdrop; copy only */
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0};
    wc.hInstance = g_hInst;
    wc.lpfnWndProc = SinkProc;
    wc.lpszClassName = L"SC_PreviewSink";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    Dashboard_Init();
    Dashboard_Show();
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        Pump();
        Dashboard_AcceptIcons();
        int complete = 1;
        for (int i = 0; i < g_cfg.dockCount; i++) if (!g_hDockIcons[i]) complete = 0;
        if (complete) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
    }
    Dashboard_Close();
    g_cfg.dashboardOpacity = configuredOpacity;
    int savedDpi = g_dpiScale;
    HDC desktopDC = GetDC(NULL);
    g_dpiScale = GetDeviceCaps(desktopDC, LOGPIXELSX);
    ReleaseDC(NULL, desktopDC);
    UI_Show(NULL);
    HWND settings = FindWindowW(L"ShortCutterSettings", NULL);
    const wchar_t *settingsCaptures[] = {L"tests\\settings-layout-general.bmp",L"tests\\settings-layout-dashboard.bmp",
        L"tests\\settings-layout-commands.bmp",L"tests\\settings-layout-gestures.bmp",L"tests\\settings-layout-hotkeys.bmp"};
    for (int page = 0; page < 5; page++) {
        SelectSettingsTab(settings,page);
        CaptureWindow(settings,settingsCaptures[page]);
    }
    SelectSettingsTab(settings,1);
    CaptureWindow(settings, L"tests\\dashboard-theme-settings.bmp");
    const wchar_t *languageNames[] = {L"en", L"zh"};
    const wchar_t *pageNames[] = {L"general", L"dashboard", L"commands", L"gestures", L"hotkeys"};
    for (int language = UI_LANG_ENGLISH; language < UI_LANG_COUNT; language++) {
        SelectSettingsLanguage(settings, language);
        for (int page = 0; page < 5; page++) {
            wchar_t path[MAX_PATH];
            swprintf(path, MAX_PATH, L"tests\\settings-language-%ls-%ls.bmp", languageNames[language], pageNames[page]);
            SelectSettingsTab(settings, page);
            CaptureWindow(settings, path);
        }
    }
    DestroyWindow(settings);
    g_cfg.uiLanguage = UI_LANG_CHINESE;
    g_cfg.dashboardOpacity = 100;
    Dashboard_ApplyLanguage();
    Dashboard_Show();
    SearchBox_SetText(L"");
    Capture(L"tests\\dashboard-language-zh.bmp");
    SearchBox_SetText(L"no_such_command");
    SendMessageW(g_hSearchEdit,WM_KEYDOWN,VK_RETURN,0);
    Capture(L"tests\\dashboard-language-zh-notice.bmp");
    Dashboard_Close();
    g_cfg.uiLanguage = configuredLanguage;
    Dashboard_ApplyLanguage();
    g_cfg.dashboardOpacity = 100;
    g_dpiScale = savedDpi;
    Dashboard_Show();
    SearchBox_SetText(L"");
    Capture(L"tests\\dashboard-preview.bmp");
    SearchBox_SetText(L"no_such_command");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_RETURN, 0);
    Capture(L"tests\\dashboard-missing-command.bmp");
    SearchBox_SetText(L"");
    TestNameDialog();
    const wchar_t *homePaths[] = {L"tests\\dashboard-theme-white.bmp",
        L"tests\\dashboard-theme-black.bmp", L"tests\\dashboard-theme-gray.bmp"};
    const wchar_t *dialogPaths[] = {L"tests\\dashboard-folder-white.bmp",
        L"tests\\dashboard-folder-black.bmp", L"tests\\dashboard-folder-gray.bmp"};
    for (int theme = 0; theme < DASH_THEME_COUNT; theme++) {
        g_cfg.dashboardTheme = theme;
        Dashboard_ApplyTheme();
        Dash_SetDockView(0, 0);
        Capture(homePaths[theme]);
        themeDialogCapture = dialogPaths[theme];
        TestNameDialog();
    }
    themeDialogCapture = NULL;
    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (!g_cfg.dockItems[i].folderId) continue;
        Dash_SetDockView(g_cfg.dockItems[i].folderId, 0);
        Capture(L"tests\\dashboard-preview-folder.bmp");
        break;
    }
    Dashboard_Shutdown();
    WaitForSingleObject(g_iconThread, 5000);
    CloseHandle(g_iconThread);
    CloseHandle(g_iconEvent);
    DeleteCriticalSection(&g_iconLock);
    DestroyWindow(g_hWnd);
    DeleteFileW(testIni);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    g_hInst = GetModuleHandleW(NULL);
    SetProcessDPIAware();
    GetCurrentDirectoryW(MAX_PATH, g_exeDir);
    lstrcpyW(testIni, g_exeDir);
    lstrcatW(testIni, L"\\tests\\dashboard-test.ini");
    if (argc > 1 && !strcmp(argv[1], "--preview")) return Preview();
    WriteLegacy();
    Config_SetDefaults();
    Config_Load();
    Check(g_cfg.dashboardTheme == DASH_THEME_WHITE, "legacy configuration defaults to White theme");
    Check(g_cfg.uiLanguage == UI_LANG_ENGLISH, "legacy configuration defaults to English when UILanguage is absent");
    Check(!Dash_ValidateDockConfig(), "legacy 24-slot config needs no layout repair");
    int legacy = g_cfg.dockCount == 24 && g_cfg.dockPageCount == 1;
    for (int i = 0; i < 24; i++)
        legacy &= g_dockPos[i] == 23 - i && !g_cfg.dockItems[i].parentFolderId;
    Check(legacy, "legacy shortcut order and empty slots remain on home page one");
    for (int i = 24; i < 50; i++) AppendCommand(i + 1, 0, 0);
    Check(g_cfg.dockCount == 50 && Dock_PageCount(0) == 3,
          "adding more than 24 shortcuts automatically expands pages");
    int folderIndex = Dock_CreateFolder("AI tools");
    int folderId = g_cfg.dockItems[folderIndex].folderId;
    for (int i = 0; i < 30; i++) Dock_MoveItem(i, folderId, 0);
    Dock_MoveItem(folderIndex, 0, 0);
    Check(Dock_PageCount(folderId) == 2, "folder contents independently span multiple pages");
    Check(!Dock_MoveItem(folderIndex, folderId, 0), "folders cannot nest or move into themselves");
    int secondFolder = Dock_CreateFolder("Other tools");
    Check(secondFolder >= 0 && !Dock_MoveItem(folderIndex, g_cfg.dockItems[secondFolder].folderId, 0),
          "folder cannot be dragged into another folder");
    int emptyPage = Dock_AddPage(folderId);
    Check(emptyPage == 2 && Dock_PageCount(folderId) == 3, "explicit empty pages are retained");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0};
    wc.hInstance = g_hInst;
    wc.lpfnWndProc = SinkProc;
    wc.lpszClassName = L"SC_FolderTestSink";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    Dashboard_Init();
    Dashboard_Show();
    wchar_t addLabel[16];
    TestSmoothEdges();
    TestThemes();
    TestQuickInputSettings();
    TestLanguageSettings();
    TestSettingsLayout();
    GetWindowTextW(g_hDockAdd, addLabel, 16);
    Check(wcscmp(addLabel, L"Add") == 0, "Add button has no leading plus sign");
    SearchBox_SetText(L"input");
    Check(Dash_CommandIdByName("input") == CMDID_NONE &&
          SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) == 0,
          "retired input command is absent from search and name resolution");
    SearchBox_SetText(L"popup");
    Check(Dash_CommandIdByName("popup") == CMDID_NONE &&
          SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) == 0,
          "retired popup alias is absent from search and name resolution");
    Dashboard_ClearHist();
    Dashboard_AddHistItem("input");
    Dashboard_AddHistItem("popup");
    Dashboard_AddHistItem("Test 01");
    SearchBox_SetText(L"");
    Check(SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) == 1 &&
          (int)SendMessageW(g_hHistList, LB_GETITEMDATA, 0, 0) == 1,
          "legacy history hides retired aliases while preserving usable command history");
    Check(Dash_CommandIdByName("db") == CMDID_DASHBOARD &&
          Dash_CommandIdByName("settings") == CMDID_SETTINGS,
          "Dashboard and Settings builtin commands remain available");
    Dashboard_ClearHist();
    SearchBox_SetText(L"");
    TestMissingCommand();
    SetFocus(g_hDockAdd);
    ClickItem(folderIndex);
    Check(g_dockFolder == folderId && g_dockPage == 0 && Dashboard_IsVisible() && !executed,
          "clicking virtual folder opens it without executing a command or hiding Dashboard");
    Check(GetFocus() == g_hSearchEdit, "folder click restores search focus only after mouse-up");
    ClickButton(g_hDockNext);
    Check(g_dockPage == 1 && g_dockFolder == folderId, "Next responds to one mouse click despite queued refocus");
    ClickButton(g_hDockPrev);
    Check(g_dockPage == 0 && g_dockFolder == folderId, "Prev responds to one mouse click despite queued refocus");
    ClickButton(g_hDockBack);
    Check(!g_dockFolder && !g_dockPage, "Home responds to one mouse click despite queued refocus");
    UINT_PTR addTimer = SetTimer(NULL, 0, 30, ObserveAddMenu);
    ClickButton(g_hDockAdd);
    KillTimer(NULL, addTimer);
    Check(addOpened, "Add opens its menu on the first mouse click");
    TestNameDialog();
    Dash_SetDockView(folderId, 0);
    SendMessageW(g_hDockPane, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    Check(g_dockPage == 1, "mouse wheel pages inside dock");
    int found = DockPane_HitTest(DockPane_ColX(0) + 5, 5);
    Check(found >= 0 && Dock_ItemVisible(found), "hit testing excludes other pages and containers");
    SendMessageW(g_hSearchEdit, WM_KEYDOWN, VK_ESCAPE, 0);
    Check(!g_dockFolder && !g_dockPage && Dashboard_IsVisible(), "Escape returns from folder to home");
    Dash_SetDockView(folderId, 1);
    Dashboard_Close();
    Dashboard_Show();
    Check(!g_dockFolder && !g_dockPage, "every reopen returns to home page one");
    Dash_SetDockView(folderId, 2);
    Dashboard_Show();
    Check(!g_dockFolder && !g_dockPage, "calling Show while visible also returns home");
    SearchBox_SetText(L"Test 01");
    int matched = 0;
    int matchCount = (int)SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < matchCount; i++)
        if ((int)SendMessageW(g_hHistList, LB_GETITEMDATA, i, 0) == 1) matched = 1;
    Check(matched, "global search finds commands inside other folders");
    Dash_SetDockView(folderId, 2);
    SearchBox_SetText(L"Test 70");
    SendMessageW(g_hHistList, LB_SETCURSEL, 0, 0);
    int added = g_cfg.dockCount;
    Dash_AddDockItem(g_hDash);
    Check(g_cfg.dockCount == added + 1 && g_cfg.dockItems[added].parentFolderId == folderId &&
          g_dockPos[added] / DOCK_PAGE_SIZE == 2,
          "Add selected command inserts directly into current folder page");
    Dash_SetDockView(0, 0);

    int dragged = 30;
    Dock_MoveItem(dragged, 0, 0);
    DragItem(dragged, folderIndex);
    Check(g_cfg.dockItems[dragged].parentFolderId == folderId, "real mouse drag drops shortcut into folder");
    int a = 31, b = 32;
    Dock_MoveItem(a, 0, 1);
    Dock_MoveItem(b, 0, 1);
    Dash_SetDockView(0, 1);
    int oldA = g_dockPos[a], oldB = g_dockPos[b];
    DragItem(a, b);
    Check(g_dockPos[a] == oldB && g_dockPos[b] == oldA,
          "drag swapping on page two preserves absolute page positions");
    Dash_SetDockView(0, 0);
    SetTimer(NULL, 0, 30, FillName);
    Dash_RenameFolder(g_hDash, folderIndex);
    Check(strcmp(g_cfg.dockItems[folderIndex].folderName, "开发工具") == 0,
          "real rename dialog accepts and preserves Chinese text");

    Config_Save();
    static DockItem saved[MAX_DOCK_ITEMS];
    static int savedPos[MAX_DOCK_ITEMS];
    int savedCount = g_cfg.dockCount, savedPages = g_cfg.dockPageCount;
    memcpy(saved, g_cfg.dockItems, sizeof(saved));
    memcpy(savedPos, g_dockPos, sizeof(savedPos));
    Config_SetDefaults();
    Config_Load();
    Dash_ValidateDockConfig();
    int roundtrip = g_cfg.dockCount == savedCount && g_cfg.dockPageCount == savedPages;
    for (int i = 0; i < savedCount; i++)
        roundtrip &= memcmp(&saved[i], &g_cfg.dockItems[i], sizeof(DockItem)) == 0 && savedPos[i] == g_dockPos[i];
    Check(roundtrip, "actual INI save/load preserves folders, names, page counts and positions");

    ULONGLONG deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        Pump();
        int ready = 1;
        for (int i = 0; i < g_cfg.dockCount; i++) if (!g_hDockIcons[i]) ready = 0;
        if (ready) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
    Dashboard_Show();
    Capture(L"tests\\dashboard-home.bmp");
    DWORD iconGeneration = g_iconRequest.generation;
    HICON firstIcon = g_hDockIcons[0];
    Dash_SetDockView(folderId, 1);
    Capture(L"tests\\dashboard-folder.bmp");
    Check(g_iconRequest.generation == iconGeneration && g_hDockIcons[0] == firstIcon,
          "page and folder navigation reuses cached icons without re-extraction");
    RECT pane;
    GetClientRect(g_hDockPane, &pane);
    Check((DASH_MAX_ROWS - 1) * g_dpStepY + g_dpItemH <= pane.bottom,
          "fourth grid row is fully inside shortcut pane");
    RECT nav, add, paneScreen;
    GetWindowRect(g_hDockPrev, &nav);
    GetWindowRect(g_hDockAdd, &add);
    GetWindowRect(g_hDockPane, &paneScreen);
    Check(nav.top == add.top && nav.bottom < paneScreen.top,
          "pagination shares top toolbar with Add and clears the shortcut grid");
    int before = g_cfg.dockCount;
    Dock_DissolveFolder(Dock_FindFolder(folderId));
    int intact = g_cfg.dockCount == before - 1 && Dock_FindFolder(folderId) < 0;
    for (int i = 0; i < g_cfg.dockCount; i++) intact &= g_cfg.dockItems[i].parentFolderId != folderId;
    Check(intact && !g_dockFolder && !g_dockPage,
          "dissolving an open folder preserves every child shortcut and returns home");
    Check(!Dash_ValidateDockConfig(), "dissolving produces valid, collision-free layout");
    g_cfg.dockItems[0].parentFolderId = 99999;
    g_dockPos[0] = -10;
    Check(Dash_ValidateDockConfig() && !g_cfg.dockItems[0].parentFolderId && g_dockPos[0] >= 0,
          "orphaned parents and invalid positions recover to home safely");
    Dashboard_Shutdown();
    WaitForSingleObject(g_iconThread, 5000);
    CloseHandle(g_iconThread);
    CloseHandle(g_iconEvent);
    DeleteCriticalSection(&g_iconLock);
    DestroyWindow(g_hWnd);
    TestPageDeletion();
    while (g_cfg.dockCount < MAX_DOCK_ITEMS)
        if (Dock_CreateFolder("Capacity check") < 0) break;
    Check(g_cfg.dockCount == MAX_DOCK_ITEMS && Dock_CreateFolder("One too many") < 0,
          "256-item boundary is enforced without overwriting existing items");
    Config_Save();
    Config_SetDefaults();
    Config_Load();
    Check(g_cfg.dockCount == MAX_DOCK_ITEMS && !Dash_ValidateDockConfig(),
          "full-capacity folders and pages survive configuration round-trip");
    DeleteFileW(testIni);
    printf("Result: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}

void Gesture_Cancel(void){}
void QuickInput_Cancel(void){}
const wchar_t *SC_GetInputStatusText(void){return SC_Text(L"Listening",L"监听中");}
void SC_ReportError(const wchar_t *op,DWORD error){(void)op;(void)error;}
void Action_FormatKeySeq(const char *seq,wchar_t *out,size_t count){SC_WideFromUtf8(out,count,seq);}
int Action_ValidateKeySeq(const char *seq,wchar_t *error,size_t cap){(void)seq;(void)error;(void)cap;return 1;}
