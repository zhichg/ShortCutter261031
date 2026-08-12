#include "main.h"
#include "resource.h"
#include <stdlib.h>

/* MinGW-w64 中部分版本缺少 IImageList 声明。 */
#define SHIL_JUMBO 4
typedef struct IImageListVtbl IImageListVtbl;
typedef struct { IImageListVtbl *lpVtbl; } IImageList;
struct IImageListVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IImageList*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IImageList*);
    ULONG   (STDMETHODCALLTYPE *Release)(IImageList*);
    HRESULT (STDMETHODCALLTYPE *Add)(IImageList*, HICON, int*);
    HRESULT (STDMETHODCALLTYPE *ReplaceIcon)(IImageList*, int, HICON, int*);
    HRESULT (STDMETHODCALLTYPE *SetOverlayImage)(IImageList*, int, int);
    HRESULT (STDMETHODCALLTYPE *Replace)(IImageList*, int, HICON, HICON);
    HRESULT (STDMETHODCALLTYPE *AddMasked)(IImageList*, HICON, COLORREF, int*);
    HRESULT (STDMETHODCALLTYPE *Draw)(IImageList*, void*);
    HRESULT (STDMETHODCALLTYPE *Remove)(IImageList*, int);
    HRESULT (STDMETHODCALLTYPE *GetIcon)(IImageList*, int, UINT, HICON*);
    HRESULT (STDMETHODCALLTYPE *GetImageRect)(IImageList*, int, RECT*);
    HRESULT (STDMETHODCALLTYPE *GetIconSize)(IImageList*, int*, int*);
    HRESULT (STDMETHODCALLTYPE *SetIconSize)(IImageList*, int, int);
    HRESULT (STDMETHODCALLTYPE *GetImageCount)(IImageList*, int*);
    HRESULT (STDMETHODCALLTYPE *SetImageCount)(IImageList*, UINT);
    HRESULT (STDMETHODCALLTYPE *SetBkColor)(IImageList*, COLORREF, COLORREF*);
    HRESULT (STDMETHODCALLTYPE *GetBkColor)(IImageList*, COLORREF*);
    HRESULT (STDMETHODCALLTYPE *BeginDrag)(IImageList*, int, int, int);
    HRESULT (STDMETHODCALLTYPE *EndDrag)(IImageList*);
};

#define DASH_BASE_W       860
#define DASH_MAX_ROWS       4
#define DASH_COLS           6
#define DASH_MAX_HIST      20
#define DASH_MAX_MATCHES  254
#define DASH_VISIBLE_HISTORY 4

#define DID_SEARCH_EDIT  8001
#define DID_HIST_LIST    8002
#define DID_MOST_LIST    8003
#define DID_DOCK_PANE    8010
#define WM_SC_REFOCUS    (WM_APP + 401)
#define DASH_CONTENT_TIMER 7000
#define DASH_ICON_TIMER  7001

#define POS(row,col) ((row) * DASH_COLS + (col))
#define POS_ROW(pos) ((pos) / DASH_COLS)
#define POS_COL(pos) ((pos) % DASH_COLS)
#define GET_X_LPARAM_SC(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM_SC(lp) ((int)(short)HIWORD(lp))

/* macOS-inspired light palette. */
#define CLR_WINDOW    RGB(245,245,247)
#define CLR_CARD      RGB(255,255,255)
#define CLR_TEXT      RGB(29,29,31)
#define CLR_SECONDARY RGB(110,110,115)
#define CLR_BORDER    RGB(226,226,231)
#define CLR_SEPARATOR RGB(238,238,242)
#define CLR_ACCENT    RGB(0,122,255)
#define CLR_HOVER     RGB(239,246,255)
#define CLR_SHADOW    RGB(226,226,231)
#define CLR_SEARCH    RGB(242,242,247)

static HWND g_hDash = NULL;
static HWND g_hSearchEdit = NULL;
static HWND g_hHistList = NULL;
static HWND g_hMostList = NULL;
static HWND g_hDockPane = NULL;

static HFONT g_hSectionFont = NULL;
static HFONT g_hBodyFont = NULL;
static HFONT g_hSearchFont = NULL;
static HFONT g_hSmallFont = NULL;
static HBRUSH g_hWindowBrush = NULL;
static HBRUSH g_hCardBrush = NULL;

static UINT g_dashDpi = 96;
static RECT g_rcSearchCard;
static RECT g_rcMostCard;
static RECT g_rcDockCard;
static RECT g_rcDockTitle;

static wchar_t g_sbText[512];
static int g_sbLen = 0;
static int g_sbCursor = 0;
static int g_sbFocus = 0;
static int g_sbCaretOn = 0;

static char g_dashHist[DASH_MAX_HIST][128];
static int g_dashHistCount = 0;

static HICON g_hDockIcons[MAX_DOCK_ITEMS];
/* A bounded cache (at most 24 icons) survives Dashboard close so reopening on
   the same monitor is instantaneous.  It is released when the process exits. */
static int g_iconLoaded = 0;
static int g_iconLoadNext = 0;
static int g_dockPos[MAX_DOCK_ITEMS];
static int g_mostCounts[8];
static int g_histVisualSelection = -1;

static int g_dpStepX = 120;
static int g_dpStepY = 76;
static int g_dpItemW = 112;
static int g_dpItemH = 70;
static int g_dpHoverIdx = -1;
static int g_dragIdx = -1;
static int g_dragOffX = 0;
static int g_dragOffY = 0;
static int g_dragCurX = 0;
static int g_dragCurY = 0;
static int g_dragStartX = 0;
static int g_dragStartY = 0;

static void Dash_FillHistory(const char *filter);
static void Dash_FillMostUsed(void);
static void Dash_RefreshDockPane(void);
static void Dash_BeginDeferredIconLoad(HWND hWnd);
static void Dash_AddDockItem(HWND hParent);
static void Dash_Layout(HWND hWnd);
static void Dash_ResizeForDock(HWND hWnd, const RECT *suggested);

static int DS(int value)
{
    return MulDiv(value, (int)g_dashDpi, 96);
}

static UINT Dash_GetWindowDpi(HWND hWnd)
{
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn fn = user32 ?
        (GetDpiForWindowFn)(void*)GetProcAddress(user32, "GetDpiForWindow") : NULL;
    if (fn) {
        UINT dpi = fn(hWnd);
        if (dpi >= 96 && dpi <= 768) return dpi;
    }
    return g_dpiScale >= 96 ? (UINT)g_dpiScale : 96;
}

static int CALLBACK Dash_FontEnumProc(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                      DWORD type, LPARAM data)
{
    (void)lf; (void)tm; (void)type;
    *(int*)data = 1;
    return 0;
}

static const wchar_t *Dash_FontFace(void)
{
    static int checked = 0;
    static int hasVariable = 0;
    if (!checked) {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            LOGFONTW lf = {0};
            lf.lfCharSet = DEFAULT_CHARSET;
            lstrcpyW(lf.lfFaceName, L"Segoe UI Variable Display");
            EnumFontFamiliesExW(hdc, &lf, (FONTENUMPROCW)Dash_FontEnumProc,
                                (LPARAM)&hasVariable, 0);
            ReleaseDC(NULL, hdc);
        }
        checked = 1;
    }
    return hasVariable ? L"Segoe UI Variable Display" : L"Segoe UI";
}

static HFONT Dash_CreateFont(int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, (int)g_dashDpi, 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY,
        FF_DONTCARE, Dash_FontFace());
}

static void Dash_DeleteFonts(void)
{
    HFONT *fonts[] = {&g_hSectionFont, &g_hBodyFont,
                      &g_hSearchFont, &g_hSmallFont};
    for (int i = 0; i < (int)(sizeof(fonts)/sizeof(fonts[0])); i++) {
        if (*fonts[i]) {
            DeleteObject(*fonts[i]);
            *fonts[i] = NULL;
        }
    }
}

static void Dash_CreateFonts(void)
{
    Dash_DeleteFonts();
    g_hSectionFont = Dash_CreateFont(11, FW_SEMIBOLD);
    g_hBodyFont = Dash_CreateFont(11, FW_NORMAL);
    g_hSearchFont = Dash_CreateFont(12, FW_NORMAL);
    g_hSmallFont = Dash_CreateFont(9, FW_NORMAL);

    if (g_hSearchEdit) SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_hSearchFont, FALSE);
    if (g_hHistList) SendMessageW(g_hHistList, WM_SETFONT, (WPARAM)g_hBodyFont, FALSE);
    if (g_hMostList) SendMessageW(g_hMostList, WM_SETFONT, (WPARAM)g_hBodyFont, FALSE);
}

static void Dash_RoundFill(HDC hdc, const RECT *rc, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBrush = SelectObject(hdc, brush);
    HPEN oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void Dash_DrawCard(HDC hdc, const RECT *rc)
{
    RECT shadow = *rc;
    OffsetRect(&shadow, 0, DS(2));
    Dash_RoundFill(hdc, &shadow, DS(16), CLR_SHADOW);
    Dash_RoundFill(hdc, rc, DS(16), CLR_CARD);

    HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    HPEN oldPen = SelectObject(hdc, pen);
    HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, DS(16), DS(16));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* WS_POPUP has no non-client frame for DWM to round reliably.  Keep the
   DWM preference for Windows 11 and apply a window region as the visual
   fallback, so the actual outer silhouette is rounded on every supported OS. */
static void Dash_UpdateWindowShape(HWND hWnd)
{
    RECT client = {0};
    GetClientRect(hWnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width > 0 && height > 0) {
        int diameter = DS(28);
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                         diameter, diameter);
        if (region) {
            /* SetWindowRgn takes ownership of region after a successful call. */
            if (!SetWindowRgn(hWnd, region, TRUE)) DeleteObject(region);
        }
    }

    typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, const void*, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        DwmSetWindowAttributeFn fn =
            (DwmSetWindowAttributeFn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (fn) {
            const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_SC = 33;
            const int DWMWCP_ROUND_SC = 2;
            int pref = DWMWCP_ROUND_SC;
            fn(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE_SC, &pref, sizeof(pref));
        }
        FreeLibrary(dwm);
    }
}

static void Dash_ApplyWindowOpacity(HWND hWnd)
{
    int opacity = g_cfg.dashboardOpacity;
    if (opacity < 20) opacity = 20;
    if (opacity > 100) opacity = 100;
    BYTE alpha = (BYTE)((opacity * 255 + 50) / 100);
    SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
}

void Dashboard_ApplyOpacity(void)
{
    if (g_hDash) Dash_ApplyWindowOpacity(g_hDash);
}

static HRESULT Dash_GetJumboIcon(const wchar_t *path, HICON *icon)
{
    static HMODULE shell = NULL;
    static HRESULT (WINAPI *getImageList)(int, REFIID, void**) = NULL;
    static const GUID IID_IIL =
        {0x46eb5926,0x582e,0x4017,{0x9f,0xdf,0xe8,0x99,0x8d,0xaa,0x09,0x50}};

    if (!shell) {
        shell = GetModuleHandleW(L"shell32.dll");
        if (shell) getImageList =
            (HRESULT (WINAPI*)(int,REFIID,void**))(void*)
            GetProcAddress(shell, "SHGetImageList");
    }
    if (!getImageList) return E_FAIL;

    IImageList *list = NULL;
    HRESULT hr = getImageList(SHIL_JUMBO, &IID_IIL, (void**)&list);
    if (FAILED(hr) || !list) return E_FAIL;

    SHFILEINFOW info = {0};
    if (!SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_SYSICONINDEX)) {
        list->lpVtbl->Release(list);
        return E_FAIL;
    }
    hr = list->lpVtbl->GetIcon(list, info.iIcon, ILD_TRANSPARENT, icon);
    list->lpVtbl->Release(list);
    return hr;
}

/* Request the icon at the physical size used by the current Dock.  This lets
   Windows choose a 32/48/64/256px resource that is actually close to what we
   draw, instead of always shrinking a 256px Shell canvas. */
static HICON Dash_ExtractIconForDock(const wchar_t *path)
{
    if (!path || !path[0]) return NULL;

    int iconSize = DS(42);
    if (iconSize < 16) iconSize = 16;
    if (iconSize > 256) iconSize = 256;

    HICON icon = NULL;
    UINT extracted = PrivateExtractIconsW(path, 0, iconSize, iconSize,
                                          &icon, NULL, 1, 0);
    return (extracted == 1 && icon) ? icon : NULL;
}

static HICON Dash_GetShellLargeIcon(const wchar_t *path)
{
    SHFILEINFOW info = {0};
    if (SHGetFileInfoW(path, 0, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_LARGEICON) && info.hIcon)
        return info.hIcon;
    return NULL;
}

static HICON Dash_GetIconFromPath(const wchar_t *path)
{
    HICON icon = Dash_ExtractIconForDock(path);
    if (icon) return icon;

    /* Shell handles shortcut IconLocation, folders and file associations. */
    icon = Dash_GetShellLargeIcon(path);
    if (icon) return icon;

    /* Keep the high-resolution Shell cache as a final fallback. */
    if (SUCCEEDED(Dash_GetJumboIcon(path, &icon)) && icon) return icon;
    return NULL;
}

static HICON Dash_GetIconFromUtf8Path(const char *utf8Path)
{
    if (!utf8Path || !utf8Path[0]) return NULL;

    wchar_t path[512];
    wchar_t expanded[512];
    MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, path, 512);
    if (ExpandEnvironmentStringsW(path, expanded, 512))
        lstrcpynW(path, expanded, 512);
    return Dash_GetIconFromPath(path);
}

static Command *Dash_FindCommand(int cmdId)
{
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (g_cfg.commands[i].id == cmdId) return &g_cfg.commands[i];
    return NULL;
}

static int Dash_CommandIdByName(const char *name)
{
    if (!name || !name[0]) return CMDID_NONE;
    if (lstrcmpiA(name, "sc") == 0 || lstrcmpiA(name, "settings") == 0)
        return CMDID_SETTINGS;
    if (lstrcmpiA(name, "cmd") == 0) return CMDID_CMDPROMPT;
    if (lstrcmpiA(name, "powershell") == 0 || lstrcmpiA(name, "ps") == 0)
        return CMDID_POWERSHELL;
    if (lstrcmpiA(name, "dashboard") == 0 || lstrcmpiA(name, "db") == 0)
        return CMDID_DASHBOARD;
    if (lstrcmpiA(name, "input") == 0 || lstrcmpiA(name, "popup") == 0)
        return CMDID_INPUT;
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (lstrcmpiA(name, g_cfg.commands[i].name) == 0)
            return g_cfg.commands[i].id;
    return CMDID_NONE;
}

static HICON Dash_GetDefaultProcessIcon(void)
{
    /* Match the generic executable/process icon used by Windows surfaces
       such as Task Manager when an application exposes no usable icon. */
    SHFILEINFOW info = {0};
    if (SHGetFileInfoW(L"unknown.exe", FILE_ATTRIBUTE_NORMAL,
                       &info, sizeof(info),
                       SHGFI_ICON | SHGFI_LARGEICON |
                       SHGFI_USEFILEATTRIBUTES) && info.hIcon)
        return info.hIcon;

    HICON shared = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
    return shared ? CopyIcon(shared) : NULL;
}

static HICON Dash_GetCmdIcon(Command *command, const char *iconSource)
{
    HICON icon = Dash_GetIconFromUtf8Path(iconSource);
    if (icon) return icon;
    if (command && command->type == CMD_RUN && command->runPath[0]) {
        icon = Dash_GetIconFromUtf8Path(command->runPath);
        if (icon) return icon;
    }

    return Dash_GetDefaultProcessIcon();
}

static void Dash_FreeIcons(void)
{
    if (g_hDash) KillTimer(g_hDash, DASH_ICON_TIMER);
    for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
        if (g_hDockIcons[i]) {
            DestroyIcon(g_hDockIcons[i]);
            g_hDockIcons[i] = NULL;
        }
    }
    g_iconLoaded = 0;
    g_iconLoadNext = 0;
}

static void Dash_LoadIcons(void)
{
    if (g_iconLoaded) return;
    for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
        g_hDockIcons[i] = NULL;
        if (i < g_cfg.dockCount)
            g_hDockIcons[i] = Dash_GetCmdIcon(
                Dash_FindCommand(g_cfg.dockItems[i].commandId),
                g_cfg.dockItems[i].iconSource);
    }
    g_iconLoaded = 1;
}

/* Shell 图标提取在图标缓存未预热时可能阻塞。首次打开时每次只取一个，
   保持窗口消息循环可响应；手动改 Dock 图标仍走即时刷新路径。 */
static void Dash_BeginDeferredIconLoad(HWND hWnd)
{
    Dash_FreeIcons();
    if (g_cfg.dockCount <= 0) {
        g_iconLoaded = 1;
        return;
    }
    SetTimer(hWnd, DASH_ICON_TIMER, 1, NULL);
}

static void Dash_LoadNextDeferredIcon(HWND hWnd)
{
    if (g_iconLoadNext >= g_cfg.dockCount) {
        g_iconLoaded = 1;
        KillTimer(hWnd, DASH_ICON_TIMER);
        return;
    }

    int index = g_iconLoadNext++;
    g_hDockIcons[index] = Dash_GetCmdIcon(
        Dash_FindCommand(g_cfg.dockItems[index].commandId),
        g_cfg.dockItems[index].iconSource);
    if (g_hDockPane) InvalidateRect(g_hDockPane, NULL, FALSE);
}

static int Dash_ValidateDockConfig(void)
{
    int changed = 0;
    int used[MAX_DOCK_ITEMS] = {0};
    int write = 0;

    if (g_cfg.dockCount < 0) {
        g_cfg.dockCount = 0;
        changed = 1;
    }
    if (g_cfg.dockCount > MAX_DOCK_ITEMS) {
        g_cfg.dockCount = MAX_DOCK_ITEMS;
        changed = 1;
    }

    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (!Dash_FindCommand(g_cfg.dockItems[i].commandId)) {
            changed = 1;
            continue;
        }

        int pos = g_dockPos[i];
        if (pos < 0 || pos >= MAX_DOCK_ITEMS || used[pos]) {
            pos = -1;
            for (int p = 0; p < MAX_DOCK_ITEMS; p++) {
                if (!used[p]) { pos = p; break; }
            }
            changed = 1;
        }
        if (pos < 0) break;

        if (write != i) {
            g_cfg.dockItems[write] = g_cfg.dockItems[i];
            changed = 1;
        }
        g_dockPos[write] = pos;
        used[pos] = 1;
        write++;
    }

    if (write != g_cfg.dockCount) {
        g_cfg.dockCount = write;
        changed = 1;
    }
    return changed;
}

static int Dash_DesiredHeight(void)
{
    /* 始终保留完整 6x4 网格，让任意空白格都可作为拖放目标。 */
    return DS(328 + 76 * DASH_MAX_ROWS);
}

static void Dash_ClampRectToMonitor(RECT *rect, HMONITOR monitor)
{
    MONITORINFO info = {0};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    int width = rect->right - rect->left;
    int height = rect->bottom - rect->top;
    int workW = info.rcWork.right - info.rcWork.left;
    int workH = info.rcWork.bottom - info.rcWork.top;
    if (width > workW) width = workW;
    if (height > workH) height = workH;
    if (rect->left < info.rcWork.left) rect->left = info.rcWork.left;
    if (rect->top < info.rcWork.top) rect->top = info.rcWork.top;
    if (rect->left + width > info.rcWork.right) rect->left = info.rcWork.right - width;
    if (rect->top + height > info.rcWork.bottom) rect->top = info.rcWork.bottom - height;
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
}

static void Dash_ResizeForDock(HWND hWnd, const RECT *suggested)
{
    int width = DS(DASH_BASE_W);
    int height = Dash_DesiredHeight();
    RECT target;

    if (suggested) {
        target = *suggested;
        target.right = target.left + width;
        target.bottom = target.top + height;
    } else if (IsWindowVisible(hWnd)) {
        GetWindowRect(hWnd, &target);
        target.right = target.left + width;
        target.bottom = target.top + height;
    } else {
        POINT cursor;
        GetCursorPos(&cursor);
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {0};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        target.left = info.rcWork.left +
            (info.rcWork.right - info.rcWork.left - width) / 2;
        target.top = info.rcWork.top +
            (info.rcWork.bottom - info.rcWork.top - height) / 2;
        target.right = target.left + width;
        target.bottom = target.top + height;
    }

    HMONITOR monitor = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
    Dash_ClampRectToMonitor(&target, monitor);
    SetWindowPos(hWnd, NULL, target.left, target.top,
        target.right - target.left, target.bottom - target.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

static void Dash_Layout(HWND hWnd)
{
    if (!hWnd) return;
    RECT client;
    GetClientRect(hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    /* Keep the content cards close to the rounded outer edge without
       changing their own internal breathing room. */
    int pad = DS(10);
    int gap = DS(12);
    int topH = DS(222);
    int topY = pad;
    int available = width - 2 * pad - gap;
    int leftW = available * 63 / 100;

    SetRect(&g_rcSearchCard, pad, topY, pad + leftW, topY + topH);
    SetRect(&g_rcMostCard, g_rcSearchCard.right + gap, topY,
            width - pad, topY + topH);
    SetRect(&g_rcDockCard, pad, g_rcSearchCard.bottom + gap,
            width - pad, height - pad);
    SetRect(&g_rcDockTitle, g_rcDockCard.left + DS(16),
            g_rcDockCard.top + DS(10), g_rcDockCard.right - DS(16),
            g_rcDockCard.top + DS(42));

    int searchX = g_rcSearchCard.left + DS(16);
    int searchY = g_rcSearchCard.top + DS(14);
    int searchW = g_rcSearchCard.right - g_rcSearchCard.left - DS(32);
    int searchH = DS(42);
    int histY = searchY + searchH + DS(10);
    int histH = DS(34) * DASH_VISIBLE_HISTORY;

    SetWindowPos(g_hSearchEdit, NULL, searchX, searchY, searchW, searchH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hHistList, NULL, searchX, histY, searchW, histH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int mostX = g_rcMostCard.left + DS(16);
    int mostY = g_rcMostCard.top + DS(44);
    SetWindowPos(g_hMostList, NULL, mostX, mostY,
        g_rcMostCard.right - g_rcMostCard.left - DS(32),
        g_rcMostCard.bottom - DS(14) - mostY,
        SWP_NOZORDER | SWP_NOACTIVATE);

    int paneX = g_rcDockCard.left + DS(12);
    int paneY = g_rcDockCard.top + DS(48);
    int paneW = g_rcDockCard.right - g_rcDockCard.left - DS(24);
    int paneH = g_rcDockCard.bottom - DS(10) - paneY;
    SetWindowPos(g_hDockPane, NULL, paneX, paneY, paneW, paneH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    g_dpStepX = paneW / DASH_COLS;
    g_dpStepY = DS(76);
    g_dpItemW = g_dpStepX - DS(8);
    g_dpItemH = DS(70);

    SendMessageW(g_hHistList, LB_SETITEMHEIGHT, 0, DS(34));
    SendMessageW(g_hMostList, LB_SETITEMHEIGHT, 0, DS(34));
    InvalidateRect(hWnd, NULL, FALSE);
    InvalidateRect(g_hSearchEdit, NULL, FALSE);
    InvalidateRect(g_hHistList, NULL, FALSE);
    InvalidateRect(g_hMostList, NULL, FALSE);
    InvalidateRect(g_hDockPane, NULL, FALSE);
}

static int FuzzyScore(const char *filter, const char *target)
{
    const char *f = filter;
    const char *t = target;
    int lastPos = -1;
    while (*f && *t) {
        char fc = *f;
        char tc = *t;
        if (fc >= 'A' && fc <= 'Z') fc += 32;
        if (tc >= 'A' && tc <= 'Z') tc += 32;
        if (fc == tc) {
            lastPos = (int)(t - target);
            f++;
        }
        t++;
    }
    return *f ? -1 : lastPos;
}

typedef struct {
    wchar_t name[128];
    int score;
    int cmdId;
} MatchEntry;

static int MatchCmp(const void *a, const void *b)
{
    const MatchEntry *ma = a;
    const MatchEntry *mb = b;
    if (ma->score != mb->score) return ma->score - mb->score;
    return lstrcmpiW(ma->name, mb->name);
}

static void MatchAdd(MatchEntry *matches, int *count, const char *name,
                     int cmdId, const char *filter)
{
    if (*count >= DASH_MAX_MATCHES) return;
    int score = FuzzyScore(filter, name);
    if (score < 0) return;

    wchar_t wide[128];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 128);
    for (int i = 0; i < *count; i++) {
        if (lstrcmpiW(matches[i].name, wide) == 0) return;
    }
    lstrcpynW(matches[*count].name, wide, 128);
    matches[*count].score = score;
    matches[*count].cmdId = cmdId;
    (*count)++;
}

static void DashHist_Add(const char *name)
{
    if (!name || !name[0]) return;
    int found = -1;
    for (int i = 0; i < g_dashHistCount; i++) {
        if (lstrcmpiA(g_dashHist[i], name) == 0) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        for (int i = found; i < g_dashHistCount - 1; i++)
            memcpy(g_dashHist[i], g_dashHist[i + 1], sizeof(g_dashHist[i]));
        g_dashHistCount--;
    }
    if (g_dashHistCount >= DASH_MAX_HIST) {
        for (int i = 0; i < DASH_MAX_HIST - 1; i++)
            memcpy(g_dashHist[i], g_dashHist[i + 1], sizeof(g_dashHist[i]));
        g_dashHistCount = DASH_MAX_HIST - 1;
    }
    strncpy(g_dashHist[g_dashHistCount], name, 127);
    g_dashHist[g_dashHistCount][127] = 0;
    g_dashHistCount++;
}

int Dashboard_GetHistCount(void)
{
    return g_dashHistCount;
}

const char *Dashboard_GetHistItem(int idx)
{
    return (idx >= 0 && idx < g_dashHistCount) ? g_dashHist[idx] : NULL;
}

void Dashboard_ClearHist(void)
{
    g_dashHistCount = 0;
    memset(g_dashHist, 0, sizeof(g_dashHist));
}

void Dashboard_AddHistItem(const char *name)
{
    if (!name || !name[0] || g_dashHistCount >= DASH_MAX_HIST) return;
    strncpy(g_dashHist[g_dashHistCount], name, 127);
    g_dashHist[g_dashHistCount][127] = 0;
    g_dashHistCount++;
}

int Dashboard_GetDockPos(int idx)
{
    return (idx >= 0 && idx < MAX_DOCK_ITEMS) ? g_dockPos[idx] : 0;
}

void Dashboard_SetDockPos(int idx, int pos)
{
    if (idx >= 0 && idx < MAX_DOCK_ITEMS) g_dockPos[idx] = pos;
}

void Dashboard_RecordUsage(int cmdId)
{
    if (cmdId <= 0) return;
    Command *command = Dash_FindCommand(cmdId);
    if (!command) return;

    DashHist_Add(command->name);
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (g_cfg.usage[i].cmdId == cmdId) {
            g_cfg.usage[i].useCount++;
            SYSTEMTIME st;
            GetLocalTime(&st);
            snprintf(g_cfg.usage[i].lastUsed, 20,
                "%04d-%02d-%02d %02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            return;
        }
    }
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (g_cfg.usage[i].cmdId == 0) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            g_cfg.usage[i].cmdId = cmdId;
            g_cfg.usage[i].useCount = 1;
            snprintf(g_cfg.usage[i].lastUsed, 20,
                "%04d-%02d-%02d %02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            return;
        }
    }
}

static void Dash_FillHistory(const char *filter)
{
    if (!g_hHistList) return;
    SendMessageW(g_hHistList, LB_RESETCONTENT, 0, 0);
    g_histVisualSelection = -1;

    if (!filter || !filter[0]) {
        for (int i = g_dashHistCount - 1; i >= 0; i--) {
            wchar_t wide[128];
            MultiByteToWideChar(CP_UTF8, 0, g_dashHist[i], -1, wide, 128);
            LRESULT row = SendMessageW(g_hHistList, LB_ADDSTRING, 0, (LPARAM)wide);
            SendMessageW(g_hHistList, LB_SETITEMDATA, row,
                         (LPARAM)Dash_CommandIdByName(g_dashHist[i]));
        }
    } else {
        MatchEntry matches[DASH_MAX_MATCHES];
        int count = 0;
        const struct { const char *name; int id; } aliases[] = {
            {"sc", CMDID_SETTINGS}, {"settings", CMDID_SETTINGS},
            {"cmd", CMDID_CMDPROMPT}, {"powershell", CMDID_POWERSHELL},
            {"ps", CMDID_POWERSHELL}, {"dashboard", CMDID_DASHBOARD},
            {"db", CMDID_DASHBOARD}, {"input", CMDID_INPUT},
            {"popup", CMDID_INPUT}
        };
        for (int i = 0; i < (int)(sizeof(aliases)/sizeof(aliases[0])); i++)
            MatchAdd(matches, &count, aliases[i].name, aliases[i].id, filter);
        for (int i = 0; i < g_cfg.commandCount; i++) {
            if (strchr(g_cfg.commands[i].name, '+')) continue;
            MatchAdd(matches, &count, g_cfg.commands[i].name,
                     g_cfg.commands[i].id, filter);
        }
        qsort(matches, count, sizeof(matches[0]), MatchCmp);
        for (int i = 0; i < count; i++) {
            LRESULT row = SendMessageW(g_hHistList, LB_ADDSTRING, 0,
                                       (LPARAM)matches[i].name);
            SendMessageW(g_hHistList, LB_SETITEMDATA, row,
                         (LPARAM)matches[i].cmdId);
        }
    }

    if (SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(g_hHistList, LB_SETCURSEL, 0, 0);
        SendMessageW(g_hHistList, LB_SETTOPINDEX, 0, 0);
        g_histVisualSelection = 0;
    }
    InvalidateRect(g_hHistList, NULL, FALSE);
}

static void Dash_FillMostUsed(void)
{
    if (!g_hMostList) return;
    SendMessageW(g_hMostList, LB_RESETCONTENT, 0, 0);

    typedef struct { int cmdId; int count; } UsageSort;
    UsageSort sorted[MAX_COMMANDS];
    int count = 0;
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (g_cfg.usage[i].cmdId > 0 && g_cfg.usage[i].useCount > 0 &&
            Dash_FindCommand(g_cfg.usage[i].cmdId)) {
            sorted[count].cmdId = g_cfg.usage[i].cmdId;
            sorted[count].count = g_cfg.usage[i].useCount;
            count++;
        }
    }
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[j].count > sorted[i].count) {
                UsageSort temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    int show = count < 8 ? count : 8;
    for (int i = 0; i < show; i++) {
        Command *command = Dash_FindCommand(sorted[i].cmdId);
        wchar_t name[128];
        MultiByteToWideChar(CP_UTF8, 0, command->name, -1, name, 128);
        LRESULT row = SendMessageW(g_hMostList, LB_ADDSTRING, 0, (LPARAM)name);
        SendMessageW(g_hMostList, LB_SETITEMDATA, row, (LPARAM)sorted[i].cmdId);
        g_mostCounts[i] = sorted[i].count;
    }
}

static void Dash_ExecuteSel(HWND list)
{
    LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) return;
    int cmdId = (int)SendMessageW(list, LB_GETITEMDATA, selected, 0);
    if (cmdId == LB_ERR || cmdId == CMDID_NONE) return;
    Action_ExecuteCommand(cmdId);
    Dashboard_Close();
}

static int DockPane_ColX(int col)
{
    return col * g_dpStepX + (g_dpStepX - g_dpItemW) / 2;
}

static int DockPane_HitTest(int x, int y)
{
    for (int i = 0; i < g_cfg.dockCount; i++) {
        int col = POS_COL(g_dockPos[i]);
        int row = POS_ROW(g_dockPos[i]);
        RECT item = {DockPane_ColX(col), row * g_dpStepY,
                     DockPane_ColX(col) + g_dpItemW,
                     row * g_dpStepY + g_dpItemH};
        if (PtInRect(&item, (POINT){x, y})) return i;
    }
    return -1;
}

static int DockPane_CellUsed(int pos, int exclude)
{
    for (int i = 0; i < g_cfg.dockCount; i++)
        if (i != exclude && g_dockPos[i] == pos) return i;
    return -1;
}

static int DockPane_AllocCell(void)
{
    for (int pos = 0; pos < MAX_DOCK_ITEMS; pos++)
        if (DockPane_CellUsed(pos, -1) < 0) return pos;
    return 0;
}

static int DockPane_SnapCol(int x)
{
    int col = g_dpStepX > 0 ? x / g_dpStepX : 0;
    if (col < 0) col = 0;
    if (col >= DASH_COLS) col = DASH_COLS - 1;
    return col;
}

static void Dash_RefreshDockPane(void)
{
    Dash_FreeIcons();
    Dash_LoadIcons();
    if (g_hDockPane) InvalidateRect(g_hDockPane, NULL, FALSE);
}

static void DockPane_ChooseCustomIcon(HWND hParent, int idx)
{
    if (idx < 0 || idx >= g_cfg.dockCount) return;

    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hParent;
    dialog.lpstrFilter =
        L"Icon sources (*.ico;*.exe;*.dll;*.lnk)\0*.ico;*.exe;*.dll;*.lnk\0"
        L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;

    WideCharToMultiByte(CP_UTF8, 0, path, -1,
                        g_cfg.dockItems[idx].iconSource,
                        (int)sizeof(g_cfg.dockItems[idx].iconSource), NULL, NULL);
    g_cfg.dockItems[idx].iconSource[sizeof(g_cfg.dockItems[idx].iconSource) - 1] = 0;
    Config_Save();
    Dash_RefreshDockPane();
}

static void DockPane_ResetCustomIcon(int idx)
{
    if (idx < 0 || idx >= g_cfg.dockCount) return;
    g_cfg.dockItems[idx].iconSource[0] = 0;
    Config_Save();
    Dash_RefreshDockPane();
}

static void DockPane_DrawItem(HDC hdc, int idx, int x, int y,
                              int iconSize, int dragging)
{
    if (idx < 0 || idx >= g_cfg.dockCount) return;
    if (!dragging && idx == g_dpHoverIdx) {
        RECT hover = {x, y, x + g_dpItemW, y + g_dpItemH};
        Dash_RoundFill(hdc, &hover, DS(12), CLR_HOVER);
    }

    int iconX = x + (g_dpItemW - iconSize) / 2;
    int iconY = y + DS(5);
    if (g_hDockIcons[idx])
        DrawIconEx(hdc, iconX, iconY, g_hDockIcons[idx],
                   iconSize, iconSize, 0, NULL, DI_NORMAL);

    Command *command = Dash_FindCommand(g_cfg.dockItems[idx].commandId);
    if (command) {
        wchar_t name[64];
        MultiByteToWideChar(CP_UTF8, 0, command->name, -1, name, 64);
        RECT textRc = {x + DS(3), iconY + iconSize + DS(3),
                       x + g_dpItemW - DS(3), y + g_dpItemH - DS(2)};
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        HFONT oldFont = SelectObject(hdc, g_hSmallFont);
        DrawTextW(hdc, name, -1, &textRc,
                  DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
}

static LRESULT CALLBACK DockPaneProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hWnd, &ps);
        RECT client;
        GetClientRect(hWnd, &client);
        HDC mem = CreateCompatibleDC(paint);
        HBITMAP bitmap = CreateCompatibleBitmap(paint, client.right, client.bottom);
        HBITMAP oldBitmap = SelectObject(mem, bitmap);
        FillRect(mem, &client, g_hCardBrush);
        SetStretchBltMode(mem, HALFTONE);
        SetBrushOrgEx(mem, 0, 0, NULL);
        int iconSize = DS(42);

        for (int i = 0; i < g_cfg.dockCount; i++) {
            if (i == g_dragIdx) continue;
            int x = DockPane_ColX(POS_COL(g_dockPos[i]));
            int y = POS_ROW(g_dockPos[i]) * g_dpStepY;
            DockPane_DrawItem(mem, i, x, y, iconSize, 0);
        }

        if (g_dragIdx >= 0 && g_dragIdx < g_cfg.dockCount) {
            int col = DockPane_SnapCol(g_dragCurX);
            int row = g_dpStepY > 0 ? g_dragCurY / g_dpStepY : 0;
            if (row < 0) row = 0;
            if (row >= DASH_MAX_ROWS) row = DASH_MAX_ROWS - 1;
            RECT target = {DockPane_ColX(col), row * g_dpStepY,
                           DockPane_ColX(col) + g_dpItemW,
                           row * g_dpStepY + g_dpItemH};
            Dash_RoundFill(mem, &target, DS(12), CLR_HOVER);
            HPEN pen = CreatePen(PS_SOLID, DS(2), CLR_ACCENT);
            HPEN oldPen = SelectObject(mem, pen);
            HBRUSH oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
            RoundRect(mem, target.left, target.top, target.right, target.bottom,
                      DS(12), DS(12));
            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(pen);

            int x = g_dragCurX - g_dragOffX;
            int y = g_dragCurY - g_dragOffY;
            DockPane_DrawItem(mem, g_dragIdx, x, y, iconSize, 1);
        }

        BitBlt(paint, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
        int x = GET_X_LPARAM_SC(lp);
        int y = GET_Y_LPARAM_SC(lp);
        int idx = DockPane_HitTest(x, y);
        if (idx >= 0) {
            int itemX = DockPane_ColX(POS_COL(g_dockPos[idx]));
            int itemY = POS_ROW(g_dockPos[idx]) * g_dpStepY;
            g_dragIdx = idx;
            g_dragOffX = x - itemX;
            g_dragOffY = y - itemY;
            g_dragCurX = g_dragStartX = x;
            g_dragCurY = g_dragStartY = y;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM_SC(lp);
        int y = GET_Y_LPARAM_SC(lp);
        if (g_dragIdx >= 0) {
            g_dragCurX = x;
            g_dragCurY = y;
            InvalidateRect(hWnd, NULL, FALSE);
        } else {
            int hover = DockPane_HitTest(x, y);
            if (hover != g_dpHoverIdx) {
                g_dpHoverIdx = hover;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hWnd, 0};
            TrackMouseEvent(&track);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_dpHoverIdx = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (g_dragIdx >= 0) {
            int idx = g_dragIdx;
            g_dragIdx = -1;
            /* ReleaseCapture sends WM_CAPTURECHANGED synchronously.  Clear the
               drag state first, otherwise that message invalidates idx before
               this drop is committed. */
            g_dragCurX = GET_X_LPARAM_SC(lp);
            g_dragCurY = GET_Y_LPARAM_SC(lp);
            ReleaseCapture();
            int dx = g_dragCurX - g_dragStartX;
            int dy = g_dragCurY - g_dragStartY;
            if (dx * dx + dy * dy < DS(5) * DS(5)) {
                Action_ExecuteCommand(g_cfg.dockItems[idx].commandId);
                Dashboard_Close();
                return 0;
            }

            int col = DockPane_SnapCol(g_dragCurX);
            int row = g_dpStepY > 0 ? g_dragCurY / g_dpStepY : 0;
            if (row < 0) row = 0;
            if (row >= DASH_MAX_ROWS) row = DASH_MAX_ROWS - 1;
            int target = POS(row, col);
            if (target != g_dockPos[idx]) {
                int occupied = DockPane_CellUsed(target, idx);
                if (occupied >= 0) {
                    int old = g_dockPos[idx];
                    g_dockPos[idx] = g_dockPos[occupied];
                    g_dockPos[occupied] = old;
                } else {
                    g_dockPos[idx] = target;
                }
                Config_Save();
                Dash_ResizeForDock(GetParent(hWnd), NULL);
                Dash_Layout(GetParent(hWnd));
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_dragIdx >= 0) {
            g_dragIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_RBUTTONUP: {
        PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
        int idx = DockPane_HitTest(GET_X_LPARAM_SC(lp), GET_Y_LPARAM_SC(lp));
        if (idx >= 0) {
            POINT point;
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Remove");
            AppendMenuW(menu, MF_STRING, 2, L"Choose custom icon...");
            AppendMenuW(menu, MF_STRING |
                        (g_cfg.dockItems[idx].iconSource[0] ? MF_ENABLED : MF_GRAYED),
                        3, L"Reset icon");
            int selected = (int)TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y,
                0, GetParent(hWnd), NULL);
            DestroyMenu(menu);
            if (selected == 1) {
                for (int i = idx; i < g_cfg.dockCount - 1; i++) {
                    g_cfg.dockItems[i] = g_cfg.dockItems[i + 1];
                    g_dockPos[i] = g_dockPos[i + 1];
                }
                g_cfg.dockCount--;
                Config_Save();
                Dash_RefreshDockPane();
                Dash_ResizeForDock(GetParent(hWnd), NULL);
                Dash_Layout(GetParent(hWnd));
            } else if (selected == 2) {
                DockPane_ChooseCustomIcon(GetParent(hWnd), idx);
            } else if (selected == 3) {
                DockPane_ResetCustomIcon(idx);
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void SearchBox_Update(void)
{
    char text[512];
    WideCharToMultiByte(CP_UTF8, 0, g_sbText, -1, text, 512, NULL, NULL);
    Dash_FillHistory(g_sbLen > 0 ? text : NULL);
}

static void SearchBox_SetText(const wchar_t *text)
{
    lstrcpynW(g_sbText, text, 512);
    g_sbLen = (int)wcslen(g_sbText);
    g_sbCursor = g_sbLen;
    SearchBox_Update();
    InvalidateRect(g_hSearchEdit, NULL, FALSE);
}

static LRESULT CALLBACK SearchBoxWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_sbText[0] = 0;
        g_sbLen = g_sbCursor = g_sbFocus = g_sbCaretOn = 0;
        ImmAssociateContext(hWnd, NULL);
        SetTimer(hWnd, 1, 530, NULL);
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;

    case WM_IME_SETCONTEXT:
        return 0;

    case WM_SETFOCUS:
        g_sbFocus = 1;
        g_sbCaretOn = 1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        g_sbFocus = 0;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        if (wp == 1 && g_sbFocus) {
            g_sbCaretOn = !g_sbCaretOn;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        return 0;

    case WM_CHAR:
    case WM_IME_CHAR:
        if (wp >= 32 && g_sbLen < 510) {
            for (int i = g_sbLen; i > g_sbCursor; i--)
                g_sbText[i] = g_sbText[i - 1];
            g_sbText[g_sbCursor++] = (wchar_t)wp;
            g_sbLen++;
            g_sbText[g_sbLen] = 0;
            g_sbCaretOn = 1;
            SearchBox_Update();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_KEYDOWN: {
        int update = 0;
        switch (wp) {
        case VK_BACK:
            if (g_sbCursor > 0) {
                for (int i = g_sbCursor - 1; i < g_sbLen; i++)
                    g_sbText[i] = g_sbText[i + 1];
                g_sbLen--;
                g_sbCursor--;
                update = 1;
            }
            break;
        case VK_DELETE:
            if (g_sbCursor < g_sbLen) {
                for (int i = g_sbCursor; i < g_sbLen; i++)
                    g_sbText[i] = g_sbText[i + 1];
                g_sbLen--;
                update = 1;
            }
            break;
        case VK_LEFT:
            if (g_sbCursor > 0) g_sbCursor--;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        case VK_RIGHT:
            if (g_sbCursor < g_sbLen) g_sbCursor++;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        case VK_HOME:
            g_sbCursor = 0;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        case VK_END:
            g_sbCursor = g_sbLen;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        case VK_RETURN:
            Dash_ExecuteSel(g_hHistList);
            return 0;
        case VK_SPACE:
            if (SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0)
                Dash_ExecuteSel(g_hHistList);
            else
                Dashboard_Close();
            return 0;
        case VK_ESCAPE:
            Dashboard_Close();
            return 0;
        case VK_TAB:
            if (SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0) {
                int len = (int)SendMessageW(g_hHistList, LB_GETTEXTLEN, 0, 0);
                if (len > 0 && len < 128) {
                    wchar_t text[128];
                    SendMessageW(g_hHistList, LB_GETTEXT, 0, (LPARAM)text);
                    SearchBox_SetText(text);
                }
            }
            return 0;
        case VK_UP:
        case VK_DOWN: {
            LRESULT count = SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0);
            LRESULT current = SendMessageW(g_hHistList, LB_GETCURSEL, 0, 0);
            if (count > 0) {
                if (current == LB_ERR) current = 0;
                else if (wp == VK_UP && current > 0) current--;
                else if (wp == VK_DOWN && current < count - 1) current++;
                SendMessageW(g_hHistList, LB_SETCURSEL, current, 0);
                g_histVisualSelection = (int)current;
                InvalidateRect(g_hHistList, NULL, FALSE);
            }
            return 0;
        }
        case VK_F4:
            if (GetKeyState(VK_MENU) & 0x8000) Dashboard_Close();
            return 0;
        }
        if (update) {
            g_sbCaretOn = 1;
            SearchBox_Update();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hWnd, &ps);
        RECT client;
        GetClientRect(hWnd, &client);
        HDC mem = CreateCompatibleDC(paint);
        HBITMAP bitmap = CreateCompatibleBitmap(paint, client.right, client.bottom);
        HBITMAP oldBitmap = SelectObject(mem, bitmap);
        FillRect(mem, &client, g_hCardBrush);

        RECT pill = {0, 0, client.right, client.bottom};
        Dash_RoundFill(mem, &pill, client.bottom, CLR_SEARCH);
        if (g_sbFocus) {
            HPEN pen = CreatePen(PS_SOLID, DS(2), CLR_ACCENT);
            HPEN oldPen = SelectObject(mem, pen);
            HBRUSH oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
            RoundRect(mem, 1, 1, client.right - 1, client.bottom - 1,
                      client.bottom, client.bottom);
            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(pen);
        }

        int iconX = DS(15);
        int iconY = client.bottom / 2 - DS(6);
        HPEN searchPen = CreatePen(PS_SOLID, DS(2), CLR_SECONDARY);
        HPEN oldPen = SelectObject(mem, searchPen);
        HBRUSH oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Ellipse(mem, iconX, iconY, iconX + DS(10), iconY + DS(10));
        MoveToEx(mem, iconX + DS(8), iconY + DS(8), NULL);
        LineTo(mem, iconX + DS(14), iconY + DS(14));
        SelectObject(mem, oldBrush);
        SelectObject(mem, oldPen);
        DeleteObject(searchPen);

        RECT textRc = {DS(39), 0, client.right - DS(14), client.bottom};
        SetBkMode(mem, TRANSPARENT);
        HFONT oldFont = SelectObject(mem, g_hSearchFont);
        if (g_sbLen == 0) {
            SetTextColor(mem, CLR_SECONDARY);
            DrawTextW(mem, L"Search commands", -1, &textRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            SetTextColor(mem, CLR_TEXT);
            DrawTextW(mem, g_sbText, g_sbLen, &textRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (g_sbFocus && g_sbCaretOn) {
            SIZE size = {0};
            if (g_sbCursor > 0)
                GetTextExtentPoint32W(mem, g_sbText, g_sbCursor, &size);
            HPEN caret = CreatePen(PS_SOLID, DS(1), CLR_TEXT);
            HPEN oldCaret = SelectObject(mem, caret);
            int x = textRc.left + size.cx;
            MoveToEx(mem, x, DS(10), NULL);
            LineTo(mem, x, client.bottom - DS(10));
            SelectObject(mem, oldCaret);
            DeleteObject(caret);
        }
        SelectObject(mem, oldFont);

        BitBlt(paint, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK ListHoverProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
        PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
    if (msg == WM_MOUSEMOVE) {
        int x = GET_X_LPARAM_SC(lp);
        int y = GET_Y_LPARAM_SC(lp);
        LRESULT result = SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
        int hover = HIWORD(result) == 0 ? (int)LOWORD(result) : -1;
        int oldHover = (int)(LONG_PTR)GetPropW(hWnd, L"SC_Hover");
        if (hover != oldHover) {
            SetPropW(hWnd, L"SC_Hover", (HANDLE)(LONG_PTR)hover);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hWnd, 0};
        TrackMouseEvent(&track);
    } else if (msg == WM_MOUSELEAVE) {
        SetPropW(hWnd, L"SC_Hover", (HANDLE)(LONG_PTR)-1);
        InvalidateRect(hWnd, NULL, FALSE);
    } else if (msg == WM_NCDESTROY) {
        RemovePropW(hWnd, L"SC_Hover");
    }
    return CallWindowProcW(oldProc, hWnd, msg, wp, lp);
}

static void Dash_SubclassList(HWND list)
{
    WNDPROC oldProc = (WNDPROC)SetWindowLongPtrW(
        list, GWLP_WNDPROC, (LONG_PTR)ListHoverProc);
    SetWindowLongPtrW(list, GWLP_USERDATA, (LONG_PTR)oldProc);
    SetPropW(list, L"SC_Hover", (HANDLE)(LONG_PTR)-1);
}

static void Dash_AddDockItem(HWND hParent)
{
    if (g_cfg.dockCount >= MAX_DOCK_ITEMS) {
        MessageBoxW(hParent, L"The shortcuts grid is full.",
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    LRESULT selected = SendMessageW(g_hHistList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        MessageBoxW(hParent, L"Select a command first.",
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int cmdId = (int)SendMessageW(g_hHistList, LB_GETITEMDATA, selected, 0);
    if (cmdId <= 0 || !Dash_FindCommand(cmdId)) {
        MessageBoxW(hParent, L"Only configured commands can be added.",
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (g_cfg.dockItems[i].commandId == cmdId) {
            MessageBoxW(hParent, L"This command is already in Custom shortcuts.",
                        L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    g_cfg.dockItems[g_cfg.dockCount].commandId = cmdId;
    g_cfg.dockItems[g_cfg.dockCount].iconSource[0] = 0;
    g_dockPos[g_cfg.dockCount] = DockPane_AllocCell();
    g_cfg.dockCount++;
    Config_Save();
    Dash_RefreshDockPane();
    Dash_ResizeForDock(hParent, NULL);
    Dash_Layout(hParent);
    SearchBox_SetText(L"");
}

static LRESULT CALLBACK DashboardWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_dashDpi = Dash_GetWindowDpi(hWnd);
        g_hWindowBrush = CreateSolidBrush(CLR_WINDOW);
        g_hCardBrush = CreateSolidBrush(CLR_CARD);
        Dash_CreateFonts();
        Dash_UpdateWindowShape(hWnd);

        static int searchRegistered = 0;
        if (!searchRegistered) {
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = SearchBoxWndProc;
            wc.hInstance = g_hInst;
            wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
            wc.lpszClassName = L"SC_SearchBox";
            RegisterClassExW(&wc);
            searchRegistered = 1;
        }
        static int dockRegistered = 0;
        if (!dockRegistered) {
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DockPaneProc;
            wc.hInstance = g_hInst;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = L"SC_DockPane";
            RegisterClassExW(&wc);
            dockRegistered = 1;
        }

        g_hSearchEdit = CreateWindowExW(0, L"SC_SearchBox", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 10, 10, hWnd, (HMENU)DID_SEARCH_EDIT, g_hInst, NULL);
        g_hHistList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED,
            0, 0, 10, 10, hWnd, (HMENU)DID_HIST_LIST, g_hInst, NULL);
        g_hMostList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED,
            0, 0, 10, 10, hWnd, (HMENU)DID_MOST_LIST, g_hInst, NULL);
        g_hDockPane = CreateWindowExW(0, L"SC_DockPane", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_PANE, g_hInst, NULL);

        Dash_SubclassList(g_hHistList);
        Dash_SubclassList(g_hMostList);
        Dash_CreateFonts();
        /* Size owner-draw lists before inserting items so their initial
           top index cannot scroll past the first history entry. */
        Dash_Layout(hWnd);
        Dash_FillMostUsed();
        Dash_FillHistory(NULL);
        SetFocus(g_hSearchEdit);
        return 0;
    }

    case WM_SIZE:
        Dash_UpdateWindowShape(hWnd);
        Dash_Layout(hWnd);
        return 0;

    case WM_MOUSEACTIVATE:
        PostMessageW(hWnd, WM_SC_REFOCUS, 0, 0);
        return MA_ACTIVATE;

    case WM_SETFOCUS:
        if (g_hSearchEdit) SetFocus(g_hSearchEdit);
        return 0;

    case WM_SC_REFOCUS:
        if (g_hDash == hWnd && g_hSearchEdit && IsWindow(g_hSearchEdit))
            SetFocus(g_hSearchEdit);
        return 0;

    case WM_TIMER:
        /* 留出一个消息循环周期给 DWM 提交首帧，之后才做可能触发磁盘
           访问的脚本扫描与 Shell 图标读取。 */
        if (wp == DASH_CONTENT_TIMER) {
            int dockChanged;
            KillTimer(hWnd, DASH_CONTENT_TIMER);
            Scripts_ScanAndSync();
            dockChanged = Dash_ValidateDockConfig();
            if (dockChanged) {
                Config_Save();
                Dash_FreeIcons();
            }
            Dash_FillMostUsed();
            SearchBox_Update();
            if (!g_iconLoaded) Dash_BeginDeferredIconLoad(hWnd);
            return 0;
        }
        if (wp == DASH_ICON_TIMER) {
            Dash_LoadNextDeferredIcon(hWnd);
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wp);
        if (newDpi < 96) newDpi = 96;
        g_dashDpi = newDpi;
        Dash_CreateFonts();
        Dash_FreeIcons();
        Dash_LoadIcons();
        Dash_ResizeForDock(hWnd, (const RECT*)lp);
        Dash_Layout(hWnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, CLR_CARD);
        SetTextColor((HDC)wp, CLR_TEXT);
        return (LRESULT)g_hCardBrush;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT*)lp;
        if (measure->CtlID == DID_HIST_LIST || measure->CtlID == DID_MOST_LIST) {
            measure->itemHeight = DS(34);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT*)lp;
        if ((draw->CtlID != DID_HIST_LIST && draw->CtlID != DID_MOST_LIST) ||
            (int)draw->itemID < 0) break;

        int hover = (int)(LONG_PTR)GetPropW(draw->hwndItem, L"SC_Hover") ==
                    (int)draw->itemID;
        int selected = (draw->itemState & ODS_SELECTED) ||
            (draw->hwndItem == g_hHistList &&
             (int)draw->itemID == g_histVisualSelection);
        COLORREF background = CLR_CARD;
        COLORREF foreground = CLR_TEXT;
        if (selected) {
            background = CLR_ACCENT;
            foreground = RGB(255,255,255);
        } else if (hover) {
            background = CLR_HOVER;
        }

        /* Draw the list base first, then keep automatic selection and hover
           inside a rounded inset instead of filling a square full-width row. */
        FillRect(draw->hDC, &draw->rcItem, g_hCardBrush);
        if (selected || hover) {
            RECT highlight = draw->rcItem;
            InflateRect(&highlight, -DS(3), -DS(2));
            Dash_RoundFill(draw->hDC, &highlight, DS(16), background);
        }
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, foreground);
        HFONT oldFont = SelectObject(draw->hDC, g_hBodyFont);

        wchar_t text[256] = L"";
        SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, (LPARAM)text);
        RECT textRc = draw->rcItem;
        textRc.left += DS(10);
        textRc.right -= DS(8);
        if (draw->CtlID == DID_MOST_LIST && (int)draw->itemID < 8) {
            RECT countRc = textRc;
            countRc.left = countRc.right - DS(58);
            textRc.right = countRc.left - DS(6);
            wchar_t count[24];
            swprintf(count, 24, L"%d×", g_mostCounts[draw->itemID]);
            DrawTextW(draw->hDC, count, -1, &countRc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        DrawTextW(draw->hDC, text, -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(draw->hDC, oldFont);

        if (!selected && !hover) {
            HPEN pen = CreatePen(PS_SOLID, 1, CLR_SEPARATOR);
            HPEN oldPen = SelectObject(draw->hDC, pen);
            MoveToEx(draw->hDC, draw->rcItem.left + DS(10),
                     draw->rcItem.bottom - 1, NULL);
            LineTo(draw->hDC, draw->rcItem.right - DS(8),
                   draw->rcItem.bottom - 1);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(pen);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (id == DID_HIST_LIST && code == LBN_SELCHANGE) {
            Dash_ExecuteSel(g_hHistList);
            return 0;
        }
        if (id == DID_MOST_LIST && code == LBN_SELCHANGE) {
            Dash_ExecuteSel(g_hMostList);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        POINT point = {GET_X_LPARAM_SC(lp), GET_Y_LPARAM_SC(lp)};
        if (PtInRect(&g_rcDockTitle, point)) {
            Dash_AddDockItem(hWnd);
            return 0;
        }
        break;
    }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(hWnd, &point);
            if (PtInRect(&g_rcDockTitle, point)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hWnd, &ps);
        RECT client;
        GetClientRect(hWnd, &client);
        HDC mem = CreateCompatibleDC(paint);
        HBITMAP bitmap = CreateCompatibleBitmap(paint, client.right, client.bottom);
        HBITMAP oldBitmap = SelectObject(mem, bitmap);
        FillRect(mem, &client, g_hWindowBrush);

        Dash_DrawCard(mem, &g_rcSearchCard);
        Dash_DrawCard(mem, &g_rcMostCard);
        Dash_DrawCard(mem, &g_rcDockCard);

        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, CLR_TEXT);
        HFONT oldFont = SelectObject(mem, g_hSectionFont);
        RECT mostTitle = {g_rcMostCard.left + DS(16),
                          g_rcMostCard.top + DS(12),
                          g_rcMostCard.right - DS(16),
                          g_rcMostCard.top + DS(40)};
        DrawTextW(mem, L"Most Used", -1, &mostTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(mem, L"Custom shortcuts", -1, &g_rcDockTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem, g_hSmallFont);
        SetTextColor(mem, CLR_SECONDARY);
        RECT helper = g_rcDockTitle;
        DrawTextW(mem, L"Double-click the title to add the selected command",
                  -1, &helper,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, oldFont);

        BitBlt(paint, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            HWND next = (HWND)lp;
            if (!next || (next != hWnd && !IsChild(hWnd, next) &&
                GetWindow(next, GW_OWNER) != hWnd)) {
                Dashboard_Close();
                return 0;
            }
        } else {
            PostMessageW(hWnd, WM_SC_REFOCUS, 0, 0);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, DASH_CONTENT_TIMER);
        KillTimer(hWnd, DASH_ICON_TIMER);
        Config_Save();
        Dash_DeleteFonts();
        /* Keep the bounded icon cache for an instant next Dashboard open. */
        if (g_hWindowBrush) {
            DeleteObject(g_hWindowBrush);
            g_hWindowBrush = NULL;
        }
        if (g_hCardBrush) {
            DeleteObject(g_hCardBrush);
            g_hCardBrush = NULL;
        }
        g_hDash = NULL;
        g_hSearchEdit = NULL;
        g_hHistList = NULL;
        g_hMostList = NULL;
        g_hDockPane = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void Dashboard_Show(void)
{
    static int cleanupRegistered = 0;
    if (g_hDash) {
        SetForegroundWindow(g_hDash);
        SetActiveWindow(g_hDash);
        SetFocus(g_hSearchEdit);
        return;
    }

    if (!cleanupRegistered) {
        atexit(Dash_FreeIcons);
        cleanupRegistered = 1;
    }

    static int classRegistered = 0;
    if (!classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = DashboardWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
        wc.lpszClassName = L"SC_Dashboard";
        RegisterClassExW(&wc);
        classRegistered = 1;
    }

    typedef HANDLE (WINAPI *SetThreadDpiAwarenessContextFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetThreadDpiAwarenessContextFn setThreadContext = user32 ?
        (SetThreadDpiAwarenessContextFn)(void*)
        GetProcAddress(user32, "SetThreadDpiAwarenessContext") : NULL;
    HANDLE oldContext = NULL;
    if (setThreadContext)
        oldContext = setThreadContext((HANDLE)(LONG_PTR)-4); /* PER_MONITOR_AWARE_V2 */

    g_hDash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        L"SC_Dashboard", L"SC.zhichg",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, MulDiv(DASH_BASE_W, g_dpiScale, 96),
        MulDiv(632, g_dpiScale, 96),
        NULL, NULL, g_hInst, NULL);

    if (!g_hDash) {
        if (setThreadContext && oldContext) setThreadContext(oldContext);
        return;
    }

    Dash_ApplyWindowOpacity(g_hDash);
    Dash_ResizeForDock(g_hDash, NULL);
    Dash_Layout(g_hDash);
    SetWindowPos(g_hDash, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = GetWindowThreadProcessId(foreground, NULL);
    DWORD currentThread = GetCurrentThreadId();
    if (foregroundThread != currentThread)
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    SetForegroundWindow(g_hDash);
    SetActiveWindow(g_hDash);
    SetFocus(g_hSearchEdit);
    if (foregroundThread != currentThread)
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    if (setThreadContext && oldContext) setThreadContext(oldContext);

    /* 先强制提交不含 Shell 图标的首帧，再延后补齐可变内容。 */
    UpdateWindow(g_hDash);
    SetTimer(g_hDash, DASH_CONTENT_TIMER, 50, NULL);
}

void Dashboard_Close(void)
{
    if (g_hDash) DestroyWindow(g_hDash);
}

int Dashboard_IsVisible(void)
{
    return g_hDash != NULL;
}

int Dashboard_HasFocus(void)
{
    return g_hDash && g_hSearchEdit && GetFocus() == g_hSearchEdit;
}

void Dashboard_ForwardKey(DWORD vkCode, DWORD scanCode, DWORD flags)
{
    if (!g_hDash || !g_hSearchEdit) return;
    LPARAM lParam = 1 | ((LPARAM)scanCode << 16);
    if (flags & LLKHF_EXTENDED) lParam |= (1u << 24);
    if (flags & LLKHF_ALTDOWN) lParam |= (1u << 29);

    if (vkCode == VK_RETURN || vkCode == VK_ESCAPE || vkCode == VK_TAB ||
        vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_BACK ||
        vkCode == VK_DELETE || vkCode == VK_SPACE || vkCode == VK_LEFT ||
        vkCode == VK_RIGHT || vkCode == VK_HOME || vkCode == VK_END) {
        SendMessageW(g_hSearchEdit, WM_KEYDOWN, (WPARAM)vkCode, lParam);
        return;
    }

    BYTE keyboard[256];
    GetKeyboardState(keyboard);
    keyboard[vkCode] = 0x80;
    WCHAR character;
    if (ToUnicodeEx((UINT)vkCode, (UINT)scanCode, keyboard,
                    &character, 1, 0, GetKeyboardLayout(0)) == 1)
        SendMessageW(g_hSearchEdit, WM_CHAR, character, lParam);
}
