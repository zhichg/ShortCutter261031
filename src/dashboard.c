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
#define DID_DOCK_BACK    8011
#define DID_DOCK_ADD     8012
#define DID_DOCK_PREV    8013
#define DID_DOCK_NEXT    8014
#define DID_DOCK_PAGE    8015
#define DID_SEARCH_NOTICE 8020
#define DASH_NOTICE_TIMER 2
#define DASH_NOTICE_MS    2600
#define DASH_MULTI_TIMER  3
#define DASH_MULTI_MS     30
#define DASH_FOCUS_TIMER   4
#define DASH_FOCUS_MS     40
#define WM_SC_REFOCUS    (WM_APP + 401)

#define POS(row,col) ((row) * g_dockColumns + (col))
#define POS_ROW(pos) ((pos) / g_dockColumns)
#define POS_COL(pos) ((pos) % g_dockColumns)
#define GET_X_LPARAM_SC(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM_SC(lp) ((int)(short)HIWORD(lp))

/* The same palette covers the Dashboard, inline notice and folder dialogs. */
typedef struct {
    COLORREF window, card, text, secondary, border, separator, accent, hover;
    COLORREF shadow, search, disabled, buttonDisabled, buttonHot, buttonPressed, error;
} DashPalette;
static const DashPalette g_dashPalettes[DASH_THEME_COUNT] = {
    /* White preserves the original appearance. */
    {RGB(245,245,247), RGB(255,255,255), RGB(29,29,31), RGB(110,110,115),
     RGB(226,226,231), RGB(238,238,242), RGB(0,122,255), RGB(239,246,255),
     RGB(226,226,231), RGB(242,242,247), RGB(155,155,163), RGB(248,248,250),
     RGB(232,232,238), RGB(226,226,232), RGB(190,55,55)},
    /* Black uses distinct surfaces and light text rather than pure inversion. */
    {RGB(20,21,24), RGB(30,32,36), RGB(240,242,245), RGB(168,174,184),
     RGB(66,69,76), RGB(47,50,56), RGB(0,112,232), RGB(38,55,77),
     RGB(12,13,15), RGB(42,45,50), RGB(113,119,129), RGB(35,37,42),
     RGB(53,57,64), RGB(64,69,77), RGB(255,122,122)},
    /* Gray stays neutral while keeping the existing blue accent. */
    {RGB(190,193,198), RGB(207,210,214), RGB(30,33,38), RGB(79,84,92),
     RGB(159,165,173), RGB(181,186,193), RGB(0,99,211), RGB(179,199,225),
     RGB(163,168,176), RGB(192,196,201), RGB(119,124,131), RGB(204,207,212),
     RGB(182,187,194), RGB(170,177,186), RGB(163,34,34)}
};
static const DashPalette *g_dashPalette = &g_dashPalettes[DASH_THEME_WHITE];
#define CLR_WINDOW    (g_dashPalette->window)
#define CLR_CARD      (g_dashPalette->card)
#define CLR_TEXT      (g_dashPalette->text)
#define CLR_SECONDARY (g_dashPalette->secondary)
#define CLR_BORDER    (g_dashPalette->border)
#define CLR_SEPARATOR (g_dashPalette->separator)
#define CLR_ACCENT    (g_dashPalette->accent)
#define CLR_HOVER     (g_dashPalette->hover)
#define CLR_SHADOW    (g_dashPalette->shadow)
#define CLR_SEARCH    (g_dashPalette->search)
#define CLR_DISABLED  (g_dashPalette->disabled)
#define CLR_ON_ACCENT RGB(255,255,255)

static HWND g_hDash = NULL;
static HWND g_hSearchEdit = NULL;
static HWND g_hHistList = NULL;
static HWND g_hMostList = NULL;
static HWND g_hSearchNotice = NULL;
static HWND g_hDockPane = NULL;
static HWND g_hDockBack, g_hDockAdd, g_hDockPrev, g_hDockNext, g_hDockPage;
static int g_dockFolder = 0;
static int g_dockPage = 0;
static int g_dockWheel = 0;
static int g_dockDialog = 0;
static int g_dockRowsVisible = DASH_MAX_ROWS;
static int g_dockColumns = DASH_COLS;
static int g_dockTotalRows = DASH_MAX_ROWS;
static int g_dockRowOffset = 0;
static int g_dockKeyCell = 0;
static HWND g_dashOrigin;
static BYTE g_dashPhysicalKeys[256];
static int g_dashOpening, g_dashFocusAttempts;
static int Dash_TakeSearchFocus(void);

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
static int g_sbAnchor = 0;
static wchar_t g_sbUndo[512];
static int g_sbUndoCursor, g_sbUndoAnchor, g_sbHasUndo;

static char g_dashHist[DASH_MAX_HIST][SC_NAME_CAP];
static int g_dashHistCount = 0;

static HICON g_hDockIcons[MAX_DOCK_ITEMS];
/* Only the UI thread touches windows/configuration and the displayed cache.
   One worker reads immutable path/size snapshots, including while hidden. */
typedef struct {
    int commandId;
    int isFolder;
    char iconSource[SC_PATH_CAP];
    char runPath[SC_PATH_CAP];
} DashIconSource;
typedef struct {
    DWORD generation;
    int count;
    int size;
    DashIconSource sources[MAX_DOCK_ITEMS];
    int needed[MAX_DOCK_ITEMS];
} DashIconRequest;
static DashIconRequest g_iconRequest;
static HICON g_readyIcons[MAX_DOCK_ITEMS];
static CRITICAL_SECTION g_iconLock;
static HANDLE g_iconEvent = NULL;
static HANDLE g_iconThread = NULL;
static int g_iconWorkerStop = 0;
static int g_iconRequestValid = 0;
static int g_dockPos[MAX_DOCK_ITEMS];
static int g_mostCounts[8];
static int g_histVisualSelection = -1;

static int g_dpStepX = 120;
static int g_dpStepY = 76;
static int g_dpItemW = 112;
static int g_dpItemH = 70;
static int g_dpHoverIdx = -1;
static int g_dragIdx = -1;
static int g_ctrlClickIdx = -1;
/* Command IDs survive folder/page changes and shortcut reordering. */
static int g_pendingCommands[DASH_MAX_MATCHES];
static int g_pendingCount = 0;
static int g_dragOffX = 0;
static int g_dragOffY = 0;
static int g_dragCurX = 0;
static int g_dragCurY = 0;
static int g_dragStartX = 0;
static int g_dragStartY = 0;

static void Dash_FillHistory(const char *filter);
static void Dash_FillMostUsed(void);
static void Dash_RefreshDockPane(void);
static void Dash_RequestIcons(void);
static void Dash_AddDockItem(HWND hParent);
static void Dash_Layout(HWND hWnd);
static void Dash_ResizeForDock(HWND hWnd, const RECT *suggested);
static void Dash_UpdateDockNavigation(void);
static void Dash_SetDockView(int folder, int page);
static void Dash_ShowAddMenu(HWND hParent, POINT point);
static void Dash_RenameFolder(HWND hParent, int index);
static void Dash_HideSearchNotice(void);
static LRESULT CALLBACK Dash_ButtonHoverProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR data);

static int DS(int value)
{
    return MulDiv(value, (int)g_dashDpi, 96);
}

#include "dashboard_draw.h"

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

static HANDLE Dash_SetThreadDpiContext(HANDLE context)
{
    typedef HANDLE (WINAPI *SetThreadDpiAwarenessContextFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetThreadDpiAwarenessContextFn fn = user32 ?
        (SetThreadDpiAwarenessContextFn)(void*)
        GetProcAddress(user32, "SetThreadDpiAwarenessContext") : NULL;
    return fn ? fn(context) : NULL;
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
    HWND nav[] = {g_hDockBack, g_hDockAdd, g_hDockPrev, g_hDockNext, g_hDockPage};
    for (int i = 0; i < 5; i++)
        if (nav[i]) SendMessageW(nav[i], WM_SETFONT, (WPARAM)g_hSmallFont, FALSE);
}

static void Dash_RoundFill(HDC hdc, const RECT *rc, int radius, COLORREF color)
{
    if (Dash_RoundAA(hdc, rc, radius, 0, color)) return;
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

    if (Dash_RoundAA(hdc, rc, DS(16), 1, CLR_BORDER)) return;
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    HPEN oldPen = SelectObject(hdc, pen);
    HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, DS(16), DS(16));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void Dash_RoundStroke(HDC hdc, const RECT *rc, int radius, int width, COLORREF color)
{
    if (Dash_RoundAA(hdc, rc, radius, width, color)) return;
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN oldPen = SelectObject(hdc, pen);
    HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void Dash_RoundOutline(HDC hdc, const RECT *rc, int radius, COLORREF color)
{
    Dash_RoundStroke(hdc, rc, radius, 1, color);
}

/* Small vector arrows stay crisp at the current monitor's DPI. */
static void Dash_DrawChevron(HDC hdc, int x, int y, int direction, COLORREF color)
{
    if (!direction) {
        Dash_LineAA(hdc, x - DS(3), y - DS(1), x, y + DS(2), DS(1), color);
        Dash_LineAA(hdc, x, y + DS(2), x + DS(3), y - DS(1), DS(1), color);
    } else {
        Dash_LineAA(hdc, x - direction * DS(2), y - DS(4), x + direction * DS(2), y, DS(1), color);
        Dash_LineAA(hdc, x + direction * DS(2), y, x - direction * DS(2), y + DS(4), DS(1), color);
    }
}

static void Dash_DrawButton(const DRAWITEMSTRUCT *draw, int primary, int chevron)
{
    int disabled = (draw->itemState & ODS_DISABLED) != 0;
    int hot = !disabled && GetPropW(draw->hwndItem, L"SC_ButtonHot") != NULL;
    RECT rect = draw->rcItem;
    FillRect(draw->hDC, &rect, g_hCardBrush);
    InflateRect(&rect, -DS(1), -DS(1));
    COLORREF color = primary ? CLR_ACCENT : CLR_SEARCH;
    if (disabled) color = g_dashPalette->buttonDisabled;
    else if (draw->itemState & ODS_SELECTED) color = primary ? RGB(0,100,220) : g_dashPalette->buttonPressed;
    else if (hot) color = primary ? RGB(0,112,242) : g_dashPalette->buttonHot;
    Dash_RoundFill(draw->hDC, &rect, DS(10), color);
    if (!primary) Dash_RoundOutline(draw->hDC, &rect, DS(10), disabled ? CLR_SEPARATOR : CLR_BORDER);
    wchar_t text[32];
    GetWindowTextW(draw->hwndItem, text, 32);
    SetBkMode(draw->hDC, TRANSPARENT);
    COLORREF foreground = disabled ? CLR_DISABLED : primary ? CLR_ON_ACCENT : CLR_TEXT;
    SetTextColor(draw->hDC, foreground);
    HFONT oldFont = SelectObject(draw->hDC, g_hSmallFont);
    RECT textRc = rect;
    if (chevron > 0) {
        textRc.left += DS(5);
        textRc.right -= DS(18);
        Dash_DrawChevron(draw->hDC, rect.right - DS(12), (rect.top + rect.bottom) / 2, 1, foreground);
    } else if (chevron < 0) {
        textRc.left += DS(18);
        textRc.right -= DS(5);
        Dash_DrawChevron(draw->hDC, rect.left + DS(12), (rect.top + rect.bottom) / 2, -1, foreground);
    }
    DrawTextW(draw->hDC, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw->hDC, oldFont);
    if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&rect, -DS(3), -DS(3));
        Dash_RoundOutline(draw->hDC, &rect, DS(6), primary ? CLR_ON_ACCENT : CLR_ACCENT);
    }
}

/* Alpha-composited corners preserve opacity without the binary staircase
   of a window region. Retain the original region as an allocation fallback. */
static void Dash_UpdateWindowShape(HWND hWnd)
{
    RECT client = {0};
    GetClientRect(hWnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width > 0 && height > 0) {
        int diameter = DS(28);
        HRGN region = NULL;
        if (!Dash_SetSmoothFrame(hWnd, width, height, diameter / 2))
            region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
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
    if (g_hDash) {
        Dash_ApplyWindowOpacity(g_hDash);
        InvalidateRect(g_hDash, NULL, FALSE);
    }
}

void Dashboard_ApplyTheme(void)
{
    int theme = g_cfg.dashboardTheme;
    if (theme < 0 || theme >= DASH_THEME_COUNT) theme = DASH_THEME_WHITE;
    const DashPalette *palette = &g_dashPalettes[theme];
    if (g_dashPalette == palette) return;
    if (g_hDash) {
        HBRUSH windowBrush = CreateSolidBrush(palette->window);
        HBRUSH cardBrush = CreateSolidBrush(palette->card);
        if (!windowBrush || !cardBrush) {
            if (windowBrush) DeleteObject(windowBrush);
            if (cardBrush) DeleteObject(cardBrush);
            return;
        }
        DeleteObject(g_hWindowBrush);
        DeleteObject(g_hCardBrush);
        g_hWindowBrush = windowBrush;
        g_hCardBrush = cardBrush;
    }
    g_dashPalette = palette;
    if (g_hDash) {
        /* Preserve search, navigation and icon caches; only repaint surfaces. */
        RedrawWindow(g_hDash, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        HWND popup = GetWindow(g_hDash, GW_ENABLEDPOPUP);
        if (popup && popup != g_hDash)
            RedrawWindow(popup, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
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
static HICON Dash_ExtractIconForDock(const wchar_t *path, int iconSize)
{
    if (!path || !path[0]) return NULL;

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

static HICON Dash_GetIconFromPath(const wchar_t *path, int iconSize)
{
    HICON icon = Dash_ExtractIconForDock(path, iconSize);
    if (icon) return icon;

    /* Shell handles shortcut IconLocation, folders and file associations. */
    icon = Dash_GetShellLargeIcon(path);
    if (icon) return icon;

    /* Keep the high-resolution Shell cache as a final fallback. */
    if (SUCCEEDED(Dash_GetJumboIcon(path, &icon)) && icon) return icon;
    return NULL;
}

static HICON Dash_GetIconFromUtf8Path(const char *utf8Path, int iconSize)
{
    if (!utf8Path || !utf8Path[0]) return NULL;

    wchar_t path[SC_PATH_CAP];
    wchar_t expanded[SC_PATH_CAP];
    if (!SC_WideFromUtf8(path, SC_PATH_CAP, utf8Path)) return NULL;
    DWORD length = ExpandEnvironmentStringsW(path, expanded, SC_PATH_CAP);
    if (length && length <= SC_PATH_CAP) lstrcpynW(path, expanded, SC_PATH_CAP);
    return Dash_GetIconFromPath(path, iconSize);
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

static HICON Dash_GetCmdIcon(const DashIconSource *source, int iconSize)
{
    HICON icon = Dash_GetIconFromUtf8Path(source->iconSource, iconSize);
    if (icon) return icon;
    if (source->isFolder) {
        SHFILEINFOW info = {0};
        if (SHGetFileInfoW(L"virtual-folder", FILE_ATTRIBUTE_DIRECTORY, &info,
            sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
            return info.hIcon;
    }
    if (source->runPath[0]) {
        icon = Dash_GetIconFromUtf8Path(source->runPath, iconSize);
        if (icon) return icon;
    }

    return Dash_GetDefaultProcessIcon();
}

static void Dash_FreeIcons(void)
{
    for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
        if (g_hDockIcons[i]) {
            DestroyIcon(g_hDockIcons[i]);
            g_hDockIcons[i] = NULL;
        }
    }
}

static DWORD WINAPI Dash_IconWorker(void *unused)
{
    (void)unused;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    DashIconRequest *request = malloc(sizeof(*request));
    if (!request) { if (SUCCEEDED(hr)) CoUninitialize(); return 0; }
    for (;;) {
        WaitForSingleObject(g_iconEvent, INFINITE);
        EnterCriticalSection(&g_iconLock);
        int stop = g_iconWorkerStop;
        *request = g_iconRequest;
        LeaveCriticalSection(&g_iconLock);
        if (stop) break;

        for (int i = 0; i < request->count; i++) {
            if (!request->needed[i]) continue;
            EnterCriticalSection(&g_iconLock);
            int stale = g_iconWorkerStop ||
                        request->generation != g_iconRequest.generation;
            LeaveCriticalSection(&g_iconLock);
            if (stale) break;
            /* This can block on a slow disk or Shell extension. Never hold
               the lock across extraction, and never send a synchronous UI message. */
            HICON icon = Dash_GetCmdIcon(&request->sources[i], request->size);
            EnterCriticalSection(&g_iconLock);
            if (!g_iconWorkerStop &&
                request->generation == g_iconRequest.generation) {
                if (g_readyIcons[i]) DestroyIcon(g_readyIcons[i]);
                g_readyIcons[i] = icon;
                icon = NULL;
                PostMessageW(g_hWnd, WM_SC_DASHICONS, 0, 0);
            }
            LeaveCriticalSection(&g_iconLock);
            if (icon) DestroyIcon(icon);
        }
    }
    free(request);
    if (SUCCEEDED(hr)) CoUninitialize();
    return 0;
}

static void Dash_RequestIcons(void)
{
    DashIconRequest *request = calloc(1, sizeof(*request));
    if (!request) return;
    request->count = g_cfg.dockCount;
    if (request->count < 0) request->count = 0;
    if (request->count > MAX_DOCK_ITEMS) request->count = MAX_DOCK_ITEMS;
    request->size = DS(g_cfg.dashboardCompact ? 32 : 42);
    for (int i = 0; i < request->count; i++) {
        Command *command = Dash_FindCommand(g_cfg.dockItems[i].commandId);
        request->sources[i].commandId = g_cfg.dockItems[i].commandId;
        request->sources[i].isFolder = g_cfg.dockItems[i].folderId > 0;
        SC_CopyUtf8(request->sources[i].iconSource, SC_PATH_CAP, g_cfg.dockItems[i].iconSource);
        if (command && command->type == CMD_RUN)
            SC_CopyUtf8(request->sources[i].runPath, SC_PATH_CAP, command->runPath);
    }

    if (!g_iconEvent) {
        InitializeCriticalSection(&g_iconLock);
        g_iconEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!g_iconEvent) {
            DeleteCriticalSection(&g_iconLock);
            free(request);
            return;
        }
    }
    if (!g_iconThread) {
        g_iconThread = CreateThread(NULL, 0, Dash_IconWorker, NULL, 0, NULL);
        if (!g_iconThread) { free(request); return; } /* Keep responsive on failure. */
    }

    EnterCriticalSection(&g_iconLock);
    if (g_iconRequestValid && request->count == g_iconRequest.count &&
        request->size == g_iconRequest.size &&
        memcmp(request->sources, g_iconRequest.sources, sizeof(request->sources)) == 0) {
        LeaveCriticalSection(&g_iconLock);
        free(request);
        return;
    }
    for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
        int keep = g_iconRequestValid && request->size == g_iconRequest.size &&
            i < request->count && i < g_iconRequest.count &&
            memcmp(&request->sources[i], &g_iconRequest.sources[i], sizeof(DashIconSource)) == 0;
        if (!keep && g_hDockIcons[i]) {
            DestroyIcon(g_hDockIcons[i]);
            g_hDockIcons[i] = NULL;
        }
        if (keep && g_readyIcons[i]) {
            if (g_hDockIcons[i]) DestroyIcon(g_hDockIcons[i]);
            g_hDockIcons[i] = g_readyIcons[i];
            g_readyIcons[i] = NULL;
        }
        if (g_readyIcons[i]) DestroyIcon(g_readyIcons[i]);
        g_readyIcons[i] = NULL;
        request->needed[i] = i < request->count && !g_hDockIcons[i];
    }
    request->generation = g_iconRequest.generation + 1;
    g_iconRequest = *request;
    g_iconRequestValid = 1;
    LeaveCriticalSection(&g_iconLock);
    free(request);
    SetEvent(g_iconEvent);
}

void Dashboard_AcceptIcons(void)
{
    if (!g_iconEvent) return;
    EnterCriticalSection(&g_iconLock);
    for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
        if (!g_readyIcons[i]) continue;
        if (g_hDockIcons[i]) DestroyIcon(g_hDockIcons[i]);
        g_hDockIcons[i] = g_readyIcons[i];
        g_readyIcons[i] = NULL;
    }
    LeaveCriticalSection(&g_iconLock);
    if (g_hDockPane) InvalidateRect(g_hDockPane, NULL, FALSE);
}

static int Dock_FindFolder(int id)
{
    if (id <= 0) return -1;
    for (int i = 0; i < g_cfg.dockCount; i++)
        if (g_cfg.dockItems[i].folderId == id) return i;
    return -1;
}

static int *Dock_PageCountPtr(int folder)
{
    int index = Dock_FindFolder(folder);
    return index >= 0 ? &g_cfg.dockItems[index].pageCount : &g_cfg.dockPageCount;
}

static int Dock_PageCount(int folder)
{
    int count = *Dock_PageCountPtr(folder);
    return count < 1 ? 1 : count > MAX_DOCK_PAGES ? MAX_DOCK_PAGES : count;
}

static int Dock_CellUsed(int folder, int pos, int exclude)
{
    for (int i = 0; i < g_cfg.dockCount; i++)
        if (i != exclude && g_cfg.dockItems[i].parentFolderId == folder &&
            g_dockPos[i] == pos) return i;
    return -1;
}

static int Dock_AllocCell(int folder, int startPage, int exclude)
{
    int start = startPage * DOCK_PAGE_SIZE;
    if (start < 0 || start >= MAX_DOCK_PAGES * DOCK_PAGE_SIZE) start = 0;
    for (int n = 0; n < MAX_DOCK_PAGES * DOCK_PAGE_SIZE; n++) {
        int pos = (start + n) % (MAX_DOCK_PAGES * DOCK_PAGE_SIZE);
        if (Dock_CellUsed(folder, pos, exclude) < 0) return pos;
    }
    return -1;
}

static void Dock_EnsurePage(int folder, int pos)
{
    int *count = Dock_PageCountPtr(folder);
    int needed = pos / DOCK_PAGE_SIZE + 1;
    if (*count < needed) *count = needed;
}

static int Dock_ItemVisible(int index)
{
    int row = POS_ROW(g_dockPos[index] % DOCK_PAGE_SIZE);
    return g_cfg.dockItems[index].parentFolderId == g_dockFolder &&
           g_dockPos[index] / DOCK_PAGE_SIZE == g_dockPage &&
           row >= g_dockRowOffset && row < g_dockRowOffset + g_dockRowsVisible;
}

static int Dock_MoveItem(int index, int folder, int page)
{
    if (index < 0 || index >= g_cfg.dockCount || page < 0 || page >= MAX_DOCK_PAGES)
        return 0;
    if (folder && (Dock_FindFolder(folder) < 0 || g_cfg.dockItems[index].folderId))
        return 0; /* no nested folders */
    int pos = Dock_AllocCell(folder, page, index);
    if (pos < 0) return 0;
    g_cfg.dockItems[index].parentFolderId = folder;
    g_dockPos[index] = pos;
    Dock_EnsurePage(folder, pos);
    return 1;
}

static void Dock_RemoveItem(int index)
{
    for (int i = index; i < g_cfg.dockCount - 1; i++) {
        g_cfg.dockItems[i] = g_cfg.dockItems[i + 1];
        g_dockPos[i] = g_dockPos[i + 1];
    }
    g_cfg.dockCount--;
    memset(&g_cfg.dockItems[g_cfg.dockCount], 0, sizeof(DockItem));
    g_dockPos[g_cfg.dockCount] = 0;
}

static void Dock_DissolveFolder(int index)
{
    int id = g_cfg.dockItems[index].folderId;
    if (!id) return;
    Dock_RemoveItem(index);
    for (int i = 0; i < g_cfg.dockCount; i++)
        if (g_cfg.dockItems[i].parentFolderId == id) Dock_MoveItem(i, 0, 0);
    if (g_dockFolder == id) g_dockFolder = g_dockPage = 0;
}

static int Dock_CreateFolder(const char *name)
{
    if (!name || !name[0] || g_dockFolder || g_cfg.dockCount >= MAX_DOCK_ITEMS) return -1;
    int id = 1;
    while (Dock_FindFolder(id) >= 0) id++;
    int pos = Dock_AllocCell(0, g_dockPage, -1);
    if (pos < 0) return -1;
    int index = g_cfg.dockCount++;
    DockItem *item = &g_cfg.dockItems[index];
    memset(item, 0, sizeof(*item));
    item->folderId = id;
    item->pageCount = 1;
    SC_CopyUtf8(item->folderName, SC_NAME_CAP, name);
    g_dockPos[index] = pos;
    Dock_EnsurePage(0, pos);
    return index;
}

static int Dock_AddPage(int folder)
{
    int count = Dock_PageCount(folder);
    if (count >= MAX_DOCK_PAGES) return -1;
    *Dock_PageCountPtr(folder) = count + 1;
    return count;
}

static int Dock_CanDeletePage(int folder, int page)
{
    if (folder && Dock_FindFolder(folder) < 0) return 0;
    int count = Dock_PageCount(folder);
    return count > 1 && page >= 0 && page < count && (folder || page > 0);
}

static int Dock_DeletePage(int folder, int page)
{
    if (!Dock_CanDeletePage(folder, page)) return 0;
    int count = Dock_PageCount(folder), items = 0;
    for (int i = 0; i < g_cfg.dockCount; i++)
        if (g_cfg.dockItems[i].parentFolderId == folder) items++;
    /* Check capacity before changing anything; every shortcut must survive. */
    if (items > (count - 1) * DOCK_PAGE_SIZE) return 0;
    int displaced[MAX_DOCK_ITEMS], displacedCount = 0;
    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (g_cfg.dockItems[i].parentFolderId != folder) continue;
        int itemPage = g_dockPos[i] / DOCK_PAGE_SIZE;
        if (itemPage == page) {
            displaced[displacedCount++] = i;
            g_dockPos[i] = -1;
        } else if (itemPage > page) {
            g_dockPos[i] -= DOCK_PAGE_SIZE;
        }
    }
    *Dock_PageCountPtr(folder) = count - 1;
    int pos = 0;
    for (int n = 0; n < displacedCount; n++) {
        while (Dock_CellUsed(folder, pos, -1) >= 0) pos++;
        g_dockPos[displaced[n]] = pos++;
    }
    return 1;
}

static int Dash_ValidateDockConfig(void)
{
    int changed = 0;
    if (g_cfg.dockCount < 0) { g_cfg.dockCount = 0; changed = 1; }
    if (g_cfg.dockCount > MAX_DOCK_ITEMS) { g_cfg.dockCount = MAX_DOCK_ITEMS; changed = 1; }
    /* Normalize folder identities before checking command parents. */
    for (int i = 0; i < g_cfg.dockCount; i++) {
        DockItem *item = &g_cfg.dockItems[i];
        if (item->folderId < 0) { item->folderId = 0; changed = 1; }
        if (!item->folderId) continue;
        for (int j = 0; j < i; j++) {
            if (g_cfg.dockItems[j].folderId == item->folderId) {
                int id = 1;
                while (Dock_FindFolder(id) >= 0) id++;
                item->folderId = id;
                changed = 1;
                break;
            }
        }
        if (item->parentFolderId || item->commandId) changed = 1;
        item->parentFolderId = item->commandId = 0;
        if (!item->folderName[0]) { lstrcpyA(item->folderName, "New folder"); changed = 1; }
    }
    for (int i = 0; i < g_cfg.dockCount;) {
        if (!g_cfg.dockItems[i].folderId && !Dash_FindCommand(g_cfg.dockItems[i].commandId)) {
            Dock_RemoveItem(i);
            changed = 1;
        } else i++;
    }
    for (int i = 0; i < g_cfg.dockCount; i++) {
        DockItem *item = &g_cfg.dockItems[i];
        if (item->parentFolderId && Dock_FindFolder(item->parentFolderId) < 0) {
            item->parentFolderId = 0;
            changed = 1;
        }
        int pos = g_dockPos[i];
        int duplicate = 0;
        for (int j = 0; j < i; j++)
            if (g_cfg.dockItems[j].parentFolderId == item->parentFolderId && g_dockPos[j] == pos)
                duplicate = 1;
        if (pos < 0 || pos >= MAX_DOCK_PAGES * DOCK_PAGE_SIZE || duplicate) {
            g_dockPos[i] = Dock_AllocCell(item->parentFolderId, 0, i);
            changed = 1;
        }
    }
    for (int i = -1; i < g_cfg.dockCount; i++) {
        if (i >= 0 && !g_cfg.dockItems[i].folderId) continue;
        int folder = i < 0 ? 0 : g_cfg.dockItems[i].folderId;
        int *count = Dock_PageCountPtr(folder);
        int valid = Dock_PageCount(folder);
        if (*count != valid) { *count = valid; changed = 1; }
        for (int j = 0; j < g_cfg.dockCount; j++)
            if (g_cfg.dockItems[j].parentFolderId == folder && g_dockPos[j] / DOCK_PAGE_SIZE >= *count) {
                Dock_EnsurePage(folder, g_dockPos[j]);
                changed = 1;
            }
    }
    if (g_dockFolder && Dock_FindFolder(g_dockFolder) < 0) g_dockFolder = g_dockPage = 0;
    if (g_dockPage >= Dock_PageCount(g_dockFolder)) g_dockPage = 0;
    return changed;
}

static int Dash_DesiredHeight(void)
{
    return DS(g_cfg.dashboardCompact ? 288 + 64 * 3 : 322 + 76 * DASH_MAX_ROWS);
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
    int topH = DS(g_cfg.dashboardCompact ? 188 : 222);
    int topY = pad;
    int available = width - 2 * pad - gap;
    int leftW = available * 63 / 100;

    SetRect(&g_rcSearchCard, pad, topY, pad + leftW, topY + topH);
    SetRect(&g_rcMostCard, g_rcSearchCard.right + gap, topY,
            width - pad, topY + topH);
    SetRect(&g_rcDockCard, pad, g_rcSearchCard.bottom + gap,
            width - pad, height - pad);
    int navWidth = DS(156) + DS(72);
    int navX = (g_rcDockCard.left + g_rcDockCard.right - navWidth) / 2;
    SetRect(&g_rcDockTitle, g_rcDockCard.left + DS(16),
            g_rcDockCard.top + DS(10), navX - DS(12),
            g_rcDockCard.top + DS(42));
    if (g_dockFolder) g_rcDockTitle.left += DS(65);
    SetWindowPos(g_hDockBack, NULL, g_rcDockCard.left + DS(16),
        g_rcDockCard.top + DS(12), DS(60), DS(28), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hDockAdd, NULL, g_rcDockCard.right - DS(90),
        g_rcDockCard.top + DS(12), DS(74), DS(28), SWP_NOZORDER | SWP_NOACTIVATE);

    int searchX = g_rcSearchCard.left + DS(16);
    int searchY = g_rcSearchCard.top + DS(14);
    int searchW = g_rcSearchCard.right - g_rcSearchCard.left - DS(32);
    int searchH = DS(42);
    int histY = searchY + searchH + DS(10);
    int histH = DS(34) * (g_cfg.dashboardCompact ? 3 : DASH_VISIBLE_HISTORY);

    SetWindowPos(g_hSearchEdit, NULL, searchX, searchY, searchW, searchH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hHistList, NULL, searchX, histY, searchW, histH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hSearchNotice, HWND_TOP, searchX + DS(3), histY + DS(2),
                 searchW - DS(6), DS(32), SWP_NOACTIVATE);

    int mostX = g_rcMostCard.left + DS(16);
    int mostY = g_rcMostCard.top + DS(44);
    SetWindowPos(g_hMostList, NULL, mostX, mostY,
        g_rcMostCard.right - g_rcMostCard.left - DS(32),
        g_rcMostCard.bottom - DS(14) - mostY,
        SWP_NOZORDER | SWP_NOACTIVATE);

    int paneX = g_rcDockCard.left + DS(16);
    int paneY = g_rcDockCard.top + DS(52);
    int paneW = g_rcDockCard.right - g_rcDockCard.left - DS(32);
    int paneH = g_rcDockCard.bottom - DS(16) - paneY;
    SetWindowPos(g_hDockPane, NULL, paneX, paneY, paneW, paneH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    int navY = g_rcDockCard.top + DS(12);
    SetWindowPos(g_hDockPrev, NULL, navX, navY, DS(72), DS(28), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hDockPage, NULL, navX + DS(80), navY, DS(68), DS(28), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hDockNext, NULL, navX + DS(156), navY, DS(72), DS(28), SWP_NOZORDER | SWP_NOACTIVATE);

    g_dockColumns = paneW / DS(88);
    if (g_dockColumns < 1) g_dockColumns = 1;
    if (g_dockColumns > DASH_COLS) g_dockColumns = DASH_COLS;
    g_dockTotalRows = (DOCK_PAGE_SIZE + g_dockColumns - 1) / g_dockColumns;
    g_dpStepX = paneW / g_dockColumns;
    g_dpStepY = DS(g_cfg.dashboardCompact ? 64 : 76);
    g_dockRowsVisible = paneH / g_dpStepY;
    if (g_dockRowsVisible < 1) g_dockRowsVisible = 1;
    if (g_dockRowsVisible > g_dockTotalRows) g_dockRowsVisible = g_dockTotalRows;
    int maxOffset = g_dockTotalRows - g_dockRowsVisible;
    if (g_dockRowOffset > maxOffset) g_dockRowOffset = maxOffset;
    SCROLLINFO scroll = {sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                         g_dockTotalRows - 1, (UINT)g_dockRowsVisible, g_dockRowOffset, 0};
    SetScrollInfo(g_hDockPane, SB_VERT, &scroll, TRUE);
    ShowScrollBar(g_hDockPane, SB_VERT, maxOffset > 0);
    RECT paneClient;
    if (GetClientRect(g_hDockPane, &paneClient)) g_dpStepX = paneClient.right / g_dockColumns;
    g_dpItemW = g_dpStepX - DS(8);
    g_dpItemH = g_dpStepY - DS(6);

    SendMessageW(g_hHistList, LB_SETITEMHEIGHT, 0, DS(34));
    SendMessageW(g_hMostList, LB_SETITEMHEIGHT, 0, DS(34));
    InvalidateRect(hWnd, NULL, FALSE);
    InvalidateRect(g_hSearchEdit, NULL, FALSE);
    InvalidateRect(g_hHistList, NULL, FALSE);
    InvalidateRect(g_hMostList, NULL, FALSE);
    InvalidateRect(g_hDockPane, NULL, FALSE);
}

static void Dash_UpdateDockNavigation(void)
{
    wchar_t text[32];
    int count = Dock_PageCount(g_dockFolder);
    swprintf(text, 32, L"%d / %d", g_dockPage + 1, count);
    SetWindowTextW(g_hDockPage, text);
    EnableWindow(g_hDockPrev, g_dockPage > 0);
    EnableWindow(g_hDockNext, g_dockPage + 1 < count);
    ShowWindow(g_hDockBack, g_dockFolder ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (g_hDash) InvalidateRect(g_hDash, NULL, FALSE);
    if (g_hDockPane) InvalidateRect(g_hDockPane, NULL, FALSE);
}

static void Dash_SetDockView(int folder, int page)
{
    if (folder && Dock_FindFolder(folder) < 0) return;
    if (page < 0 || page >= Dock_PageCount(folder)) return;
    g_dragIdx = g_dpHoverIdx = -1;
    if (GetCapture() == g_hDockPane) ReleaseCapture();
    g_dockFolder = folder;
    g_dockPage = page;
    g_dockRowOffset = 0;
    g_dockKeyCell = 0;
    g_dockWheel = 0;
    Dash_Layout(g_hDash);
    Dash_UpdateDockNavigation();
}

typedef struct { char text[SC_NAME_CAP]; int error; } DockNameDialog;

static RECT Dock_NameFieldRect(HWND hDlg)
{
    RECT client;
    GetClientRect(hDlg, &client);
    RECT field = {DS(20), DS(106), client.right - DS(20), DS(144)};
    return field;
}

static INT_PTR CALLBACK Dock_NameDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    DockNameDialog *name = (DockNameDialog*)GetWindowLongPtrW(hDlg, DWLP_USER);
    if (msg == WM_MOVE || msg == WM_SHOWWINDOW)
        Dash_SyncSmoothFrame(hDlg, msg == WM_SHOWWINDOW ? (int)wp : IsWindowVisible(hDlg));
    if (msg == WM_DESTROY) Dash_DestroySmoothFrame(hDlg);
    if (msg == WM_INITDIALOG) {
        name = (DockNameDialog*)lp;
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);
        name->error = 0;
        SetWindowTextW(hDlg, name->text[0] ? SC_Text(L"Rename folder", L"重命名文件夹") :
                                           SC_Text(L"New folder", L"新建文件夹"));
        RECT owner, work;
        GetWindowRect(GetParent(hDlg), &owner);
        MONITORINFO monitor = {0};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST), &monitor);
        work = monitor.rcWork;
        int width = DS(360), height = DS(220);
        int x = (owner.left + owner.right - width) / 2;
        int y = (owner.top + owner.bottom - height) / 2;
        if (x + width > work.right) x = work.right - width;
        if (y + height > work.bottom) y = work.bottom - height;
        if (x < work.left) x = work.left;
        if (y < work.top) y = work.top;
        SetWindowPos(hDlg, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        Dash_UpdateWindowShape(hDlg);
        RECT client;
        GetClientRect(hDlg, &client);
        RECT field = Dock_NameFieldRect(hDlg);
        HWND edit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            field.left + DS(12), field.top + DS(8), field.right - field.left - DS(24), DS(22),
            hDlg, (HMENU)100, g_hInst, NULL);
        HWND cancel = CreateWindowW(L"BUTTON", SC_Text(L"Cancel", L"取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            client.right - DS(196), client.bottom - DS(52), DS(80), DS(32),
            hDlg, (HMENU)IDCANCEL, g_hInst, NULL);
        HWND ok = CreateWindowW(L"BUTTON", name->text[0] ? SC_Text(L"Save", L"保存") : SC_Text(L"Create", L"创建"),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            client.right - DS(108), client.bottom - DS(52), DS(88), DS(32),
            hDlg, (HMENU)IDOK, g_hInst, NULL);
        HWND close = CreateWindowW(L"BUTTON", SC_Text(L"Close", L"关闭"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            client.right - DS(44), DS(14), DS(24), DS(24), hDlg, (HMENU)101, g_hInst, NULL);
        HWND controls[] = {edit, cancel, ok, close};
        for (int i = 0; i < 4; i++) SendMessageW(controls[i], WM_SETFONT, (WPARAM)g_hBodyFont, TRUE);
        for (int i = 1; i < 4; i++) SetWindowSubclass(controls[i], Dash_ButtonHoverProc, 1, 0);
        SendMessageW(hDlg, DM_SETDEFID, IDOK, 0);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        wchar_t text[SC_NAME_CAP] = L"";
        SC_WideFromUtf8(text, SC_NAME_CAP, name->text);
        SetWindowTextW(edit, text);
        SendMessageW(edit, EM_LIMITTEXT, SC_NAME_CAP - 1, 0);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == 100) {
        if (HIWORD(wp) == EN_CHANGE && name) name->error = 0;
        if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)
            InvalidateRect(hDlg, NULL, FALSE);
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetBkColor((HDC)wp, CLR_SEARCH);
        SetTextColor((HDC)wp, CLR_TEXT);
        SetDCBrushColor((HDC)wp, CLR_SEARCH);
        return (INT_PTR)GetStockObject(DC_BRUSH);
    }
    if (msg == WM_ERASEBKGND) return TRUE;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hDlg, &ps);
        RECT client;
        GetClientRect(hDlg, &client);
        HDC mem = CreateCompatibleDC(paint);
        HBITMAP bitmap = CreateCompatibleBitmap(paint, client.right, client.bottom);
        HBITMAP oldBitmap = SelectObject(mem, bitmap);
        FillRect(mem, &client, g_hCardBrush);
        RECT edge = client;
        Dash_RoundOutline(mem, &edge, DS(24), CLR_BORDER);
        SetBkMode(mem, TRANSPARENT);
        HFONT oldFont = SelectObject(mem, g_hSectionFont);
        SetTextColor(mem, CLR_TEXT);
        wchar_t title[32];
        GetWindowTextW(hDlg, title, 32);
        RECT text = {DS(20), DS(16), client.right - DS(56), DS(42)};
        DrawTextW(mem, title, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(mem, g_hSmallFont);
        SetTextColor(mem, CLR_SECONDARY);
        text.top = DS(44); text.bottom = DS(66); text.right = client.right - DS(20);
        DrawTextW(mem, SC_Text(L"Keep related shortcuts together.", L"将相关快捷方式放在一起。"), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(mem, g_hBodyFont);
        SetTextColor(mem, CLR_TEXT);
        text.top = DS(78); text.bottom = DS(102);
        DrawTextW(mem, SC_Text(L"Folder name", L"文件夹名称"), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT field = Dock_NameFieldRect(hDlg);
        Dash_RoundFill(mem, &field, DS(10), CLR_SEARCH);
        Dash_RoundOutline(mem, &field, DS(10), GetFocus() == GetDlgItem(hDlg, 100) ? CLR_ACCENT : CLR_BORDER);
        if (name && name->error) {
            SelectObject(mem, g_hSmallFont);
            SetTextColor(mem, g_dashPalette->error);
            text.top = DS(146); text.bottom = DS(164);
            DrawTextW(mem, name->error == 1 ? SC_Text(L"Enter a folder name.", L"请输入文件夹名称。") :
                                             SC_Text(L"Use a shorter folder name.", L"请缩短文件夹名称。"),
                      -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        SelectObject(mem, oldFont);
        Dash_PaintSmoothFrame(hDlg, mem, 255);
        BitBlt(paint, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hDlg, &ps);
        return TRUE;
    }
    if (msg == WM_DRAWITEM) {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT*)lp;
        if (draw->CtlType != ODT_BUTTON) return FALSE;
        if (draw->CtlID != 101) Dash_DrawButton(draw, draw->CtlID == IDOK, 0);
        else {
            FillRect(draw->hDC, &draw->rcItem, g_hCardBrush);
            if (GetPropW(draw->hwndItem, L"SC_ButtonHot"))
                Dash_RoundFill(draw->hDC, &draw->rcItem, DS(8), CLR_SEARCH);
            int x = (draw->rcItem.left + draw->rcItem.right) / 2;
            int y = (draw->rcItem.top + draw->rcItem.bottom) / 2;
            Dash_LineAA(draw->hDC, x - DS(4), y - DS(4), x + DS(4), y + DS(4), DS(1), CLR_SECONDARY);
            Dash_LineAA(draw->hDC, x + DS(4), y - DS(4), x - DS(4), y + DS(4), DS(1), CLR_SECONDARY);
        }
        return TRUE;
    }
    if (msg == WM_NCHITTEST) {
        POINT point = {GET_X_LPARAM_SC(lp), GET_Y_LPARAM_SC(lp)};
        ScreenToClient(hDlg, &point);
        if (point.y >= 0 && point.y < DS(70)) {
            SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, HTCAPTION);
            return TRUE;
        }
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        wchar_t text[SC_NAME_CAP];
        GetDlgItemTextW(hDlg, 100, text, SC_NAME_CAP);
        wchar_t *first = text;
        while (*first == L' ') first++;
        int length = (int)wcslen(first);
        while (length && first[length - 1] == L' ') first[--length] = 0;
        if (!length || wcschr(first, L'\r') || wcschr(first, L'\n') ||
            !SC_Utf8FromWide(name->text, SC_NAME_CAP, first)) {
            name->error = length ? 2 : 1;
            InvalidateRect(hDlg, NULL, FALSE);
            SetFocus(GetDlgItem(hDlg, 100));
            return TRUE;
        }
        EndDialog(hDlg, IDOK);
        return TRUE;
    }
    if (msg == WM_CLOSE || (msg == WM_COMMAND && (LOWORD(wp) == IDCANCEL || LOWORD(wp) == 101))) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static int Dock_PromptName(HWND hParent, DockNameDialog *name)
{
    struct { DLGTEMPLATE dialog; WORD menu, windowClass; wchar_t title[32]; } tmpl = {0};
    tmpl.dialog.style = WS_POPUP | WS_SYSMENU;
    tmpl.dialog.dwExtendedStyle = WS_EX_TOOLWINDOW;
    tmpl.dialog.cx = 240;
    tmpl.dialog.cy = 95;
    lstrcpyW(tmpl.title, name->text[0] ? SC_Text(L"Rename folder", L"重命名文件夹") :
                                      SC_Text(L"New folder", L"新建文件夹"));
    g_dockDialog++;
    INT_PTR result = DialogBoxIndirectParamW(g_hInst, &tmpl.dialog, hParent,
                                            Dock_NameDialogProc, (LPARAM)name);
    g_dockDialog--;
    if (Dashboard_IsVisible()) SetFocus(g_hSearchEdit);
    return result == IDOK;
}

static int Dash_TrackMenu(HMENU menu, UINT flags, POINT point, HWND parent)
{
    g_dockDialog++;
    int selected = (int)TrackPopupMenu(menu, flags, point.x, point.y, 0, parent, NULL);
    g_dockDialog--;
    return selected;
}

static void Dash_RenameFolder(HWND hParent, int index)
{
    DockNameDialog name = {0};
    SC_CopyUtf8(name.text, SC_NAME_CAP, g_cfg.dockItems[index].folderName);
    if (!Dock_PromptName(hParent, &name)) return;
    SC_CopyUtf8(g_cfg.dockItems[index].folderName, SC_NAME_CAP, name.text);
    if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
    Dash_UpdateDockNavigation();
}

static void Dash_ShowAddMenu(HWND hParent, POINT point)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, SC_Text(L"Add selected command", L"添加选中的命令"));
    AppendMenuW(menu, MF_STRING | (g_dockFolder ? MF_GRAYED : 0), 2, SC_Text(L"New folder...", L"新建文件夹..."));
    AppendMenuW(menu, MF_STRING | (Dock_PageCount(g_dockFolder) >= MAX_DOCK_PAGES ? MF_GRAYED : 0),
                3, SC_Text(L"New page", L"新建页面"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (Dock_CanDeletePage(g_dockFolder, g_dockPage) ? 0 : MF_GRAYED),
                4, SC_Text(L"Delete current page", L"删除当前页面"));
    int selected = Dash_TrackMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point, hParent);
    DestroyMenu(menu);
    if (selected == 1) Dash_AddDockItem(hParent);
    if (selected == 2) {
        DockNameDialog name = {0};
        if (Dock_PromptName(hParent, &name)) {
            int index = Dock_CreateFolder(name.text);
            if (index < 0) {
                MessageBoxW(hParent, SC_Text(L"The shortcut limit has been reached.", L"快捷方式数量已达到上限。"), L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
                return;
            }
            g_dockPage = g_dockPos[index] / DOCK_PAGE_SIZE;
            if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            Dash_RefreshDockPane();
            Dash_UpdateDockNavigation();
        }
    }
    if (selected == 3) {
        int page = Dock_AddPage(g_dockFolder);
        if (page >= 0) {
            if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            Dash_SetDockView(g_dockFolder, page);
        }
    }
    if (selected == 4) {
        if (Dock_DeletePage(g_dockFolder, g_dockPage)) {
            int page = g_dockPage;
            if (page >= Dock_PageCount(g_dockFolder)) page = Dock_PageCount(g_dockFolder) - 1;
            if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            Dash_SetDockView(g_dockFolder, page);
        } else {
            MessageBoxW(hParent,
                SC_Text(L"The other pages do not have enough free slots.\n"
                        L"Move some shortcuts to another folder before deleting this page.",
                        L"其他页面的空位不足。\n请先将部分快捷方式移到其他文件夹，再删除此页面。"),
                L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        }
    }
    if (Dashboard_IsVisible()) SetFocus(g_hSearchEdit);
}

static int FuzzyScore(const char *filter, const char *target)
{
    wchar_t wideFilter[512], wideTarget[SC_NAME_CAP];
    if (!SC_WideFromUtf8(wideFilter, 512, filter) || !SC_WideFromUtf8(wideTarget, SC_NAME_CAP, target)) return -1;
    const wchar_t *f = wideFilter;
    const wchar_t *t = wideTarget;
    int lastPos = -1;
    while (*f && *t) {
        if (CompareStringOrdinal(f, 1, t, 1, TRUE) == CSTR_EQUAL) {
            lastPos = (int)(t - wideTarget);
            f++;
        }
        t++;
    }
    return *f ? -1 : lastPos;
}

typedef struct {
    wchar_t name[SC_NAME_CAP];
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

    wchar_t wide[SC_NAME_CAP];
    SC_WideFromUtf8(wide, SC_NAME_CAP, name);
    for (int i = 0; i < *count; i++) {
        if (lstrcmpiW(matches[i].name, wide) == 0) return;
    }
    lstrcpynW(matches[*count].name, wide, SC_NAME_CAP);
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
    if (SC_CopyUtf8(g_dashHist[g_dashHistCount], SC_NAME_CAP, name)) g_dashHistCount++;
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
    if (SC_CopyUtf8(g_dashHist[g_dashHistCount], SC_NAME_CAP, name)) g_dashHistCount++;
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
            int cmdId = Dash_CommandIdByName(g_dashHist[i]);
            if (cmdId == CMDID_NONE) continue; /* skip retired or removed commands */
            wchar_t wide[SC_NAME_CAP];
            SC_WideFromUtf8(wide, SC_NAME_CAP, g_dashHist[i]);
            LRESULT row = SendMessageW(g_hHistList, LB_ADDSTRING, 0, (LPARAM)wide);
            SendMessageW(g_hHistList, LB_SETITEMDATA, row,
                         (LPARAM)cmdId);
        }
    } else {
        MatchEntry matches[DASH_MAX_MATCHES];
        int count = 0;
        const struct { const char *name; int id; } aliases[] = {
            {"sc", CMDID_SETTINGS}, {"settings", CMDID_SETTINGS},
            {"cmd", CMDID_CMDPROMPT}, {"powershell", CMDID_POWERSHELL},
            {"ps", CMDID_POWERSHELL}, {"dashboard", CMDID_DASHBOARD},
            {"db", CMDID_DASHBOARD}
        };
        for (int i = 0; i < (int)(sizeof(aliases)/sizeof(aliases[0])); i++)
            MatchAdd(matches, &count, aliases[i].name, aliases[i].id, filter);
        for (int i = 0; i < g_cfg.commandCount; i++) {
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
        wchar_t name[SC_NAME_CAP];
        SC_WideFromUtf8(name, SC_NAME_CAP, command->name);
        LRESULT row = SendMessageW(g_hMostList, LB_ADDSTRING, 0, (LPARAM)name);
        SendMessageW(g_hMostList, LB_SETITEMDATA, row, (LPARAM)sorted[i].cmdId);
        g_mostCounts[i] = sorted[i].count;
    }
}

static void Dash_HideSearchNotice(void)
{
    if (g_hDash) KillTimer(g_hDash, DASH_NOTICE_TIMER);
    if (g_hSearchNotice) ShowWindow(g_hSearchNotice, SW_HIDE);
}

static void Dash_ShowMissingCommand(void)
{
    if (!Dashboard_IsVisible() || !g_hSearchNotice) return;
    SetWindowPos(g_hSearchNotice, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(g_hDash, DASH_NOTICE_TIMER, DASH_NOTICE_MS, NULL);
    InvalidateRect(g_hSearchNotice, NULL, FALSE);
}

static int Dash_CommandValid(int cmdId)
{
    return cmdId > 0 ? Dash_FindCommand(cmdId) != NULL :
        cmdId == CMDID_SETTINGS || cmdId == CMDID_TOGGLE ||
        cmdId == CMDID_CMDPROMPT || cmdId == CMDID_POWERSHELL ||
        cmdId == CMDID_DASHBOARD;
}

static int Dash_PendingIndex(int cmdId)
{
    for (int i = 0; i < g_pendingCount; i++)
        if (g_pendingCommands[i] == cmdId) return i;
    return -1;
}

static void Dash_ResetPending(void)
{
    g_pendingCount = 0;
    if (g_hDash) KillTimer(g_hDash, DASH_MULTI_TIMER);
}

static void Dash_ActivateCommand(int cmdId, int ctrl)
{
    if (!Dash_CommandValid(cmdId)) {
        Dash_ShowMissingCommand();
        return;
    }
    if (ctrl) {
        int selected = Dash_PendingIndex(cmdId);
        if (selected >= 0) {
            for (int i = selected; i + 1 < g_pendingCount; i++)
                g_pendingCommands[i] = g_pendingCommands[i + 1];
            g_pendingCount--;
        } else if (g_pendingCount < DASH_MAX_MATCHES) {
            g_pendingCommands[g_pendingCount++] = cmdId;
        }
        if (g_pendingCount) SetTimer(g_hDash, DASH_MULTI_TIMER, DASH_MULTI_MS, NULL);
        else KillTimer(g_hDash, DASH_MULTI_TIMER);
        InvalidateRect(g_hDockPane, NULL, FALSE);
        InvalidateRect(g_hHistList, NULL, FALSE);
        InvalidateRect(g_hMostList, NULL, FALSE);
        return;
    }
    Dashboard_Close();
    Action_ExecuteCommand(cmdId);
}

static void Dash_OpenPending(void)
{
    /* Poll physical state on the UI thread, regardless of which child has
       focus. Both Ctrl keys must be released, and a click must finish first. */
    if (!g_pendingCount || !Dashboard_IsVisible() || g_dockDialog || GetCapture() ||
        (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_RCONTROL) & 0x8000)) return;

    int commands[DASH_MAX_MATCHES];
    int count = g_pendingCount;
    memcpy(commands, g_pendingCommands, count * sizeof(commands[0]));
    /* Hide and clear first: application activation cannot cancel the batch
       through WM_ACTIVATE, and key commands target the underlying window. */
    Dashboard_Close();
    for (int i = 0; i < count; i++)
        if (Dash_CommandValid(commands[i])) Action_ExecuteCommand(commands[i]);
}

static void Dash_ExecuteSel(HWND list)
{
    LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        if (list == g_hHistList && g_sbLen > 0) Dash_ShowMissingCommand();
        return;
    }
    int cmdId = (int)SendMessageW(list, LB_GETITEMDATA, selected, 0);
    Dash_ActivateCommand(cmdId, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

static int DockPane_ColX(int col)
{
    return col * g_dpStepX + (g_dpStepX - g_dpItemW) / 2;
}

static int DockPane_RowY(int row) { return (row - g_dockRowOffset) * g_dpStepY; }

static int DockPane_HitTest(int x, int y)
{
    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (!Dock_ItemVisible(i)) continue;
        int local = g_dockPos[i] % DOCK_PAGE_SIZE;
        int col = POS_COL(local);
        int row = POS_ROW(local);
        RECT item = {DockPane_ColX(col), DockPane_RowY(row),
                     DockPane_ColX(col) + g_dpItemW,
                      DockPane_RowY(row) + g_dpItemH};
        if (PtInRect(&item, (POINT){x, y})) return i;
    }
    return -1;
}

static int DockPane_CellUsed(int pos, int exclude)
{
    return Dock_CellUsed(g_dockFolder, pos, exclude);
}

static int DockPane_SnapCol(int x)
{
    int col = g_dpStepX > 0 ? x / g_dpStepX : 0;
    if (col < 0) col = 0;
    if (col >= g_dockColumns) col = g_dockColumns - 1;
    return col;
}

static void Dash_RefreshDockPane(void)
{
    Dash_RequestIcons();
    if (g_hDockPane) InvalidateRect(g_hDockPane, NULL, FALSE);
}

static void DockPane_ChooseCustomIcon(HWND hParent, int idx)
{
    if (idx < 0 || idx >= g_cfg.dockCount) return;

    wchar_t path[SC_PATH_CAP] = L"";
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hParent;
    dialog.lpstrFilter = SC_Text(
        L"Icon sources (*.ico;*.exe;*.dll;*.lnk)\0*.ico;*.exe;*.dll;*.lnk\0"
        L"All files (*.*)\0*.*\0\0",
        L"图标来源 (*.ico;*.exe;*.dll;*.lnk)\0*.ico;*.exe;*.dll;*.lnk\0"
        L"所有文件 (*.*)\0*.*\0\0");
    dialog.lpstrTitle = SC_Text(L"Choose custom icon", L"选择自定义图标");
    dialog.lpstrFile = path;
    dialog.nMaxFile = SC_PATH_CAP;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    g_dockDialog++;
    BOOL chosen = GetOpenFileNameW(&dialog);
    g_dockDialog--;
    if (!chosen) return;

    char source[SC_PATH_CAP];
    if(!SC_Utf8FromWide(source,sizeof(source),path)){
        SC_ReportError(SC_Text(L"Icon path is too long",L"图标路径过长"),ERROR_BUFFER_OVERFLOW);return;
    }
    SC_CopyUtf8(g_cfg.dockItems[idx].iconSource,sizeof(g_cfg.dockItems[idx].iconSource),source);
    if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
    Dash_RefreshDockPane();
}

static void DockPane_ResetCustomIcon(int idx)
{
    if (idx < 0 || idx >= g_cfg.dockCount) return;
    g_cfg.dockItems[idx].iconSource[0] = 0;
    if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
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
    int pending = g_cfg.dockItems[idx].folderId ? -1 :
        Dash_PendingIndex(g_cfg.dockItems[idx].commandId);
    if (pending >= 0) {
        RECT selected = {x, y, x + g_dpItemW, y + g_dpItemH};
        Dash_RoundFill(hdc, &selected, DS(12), CLR_HOVER);
        Dash_RoundStroke(hdc, &selected, DS(12), DS(2), CLR_ACCENT);
    }

    int iconX = x + (g_dpItemW - iconSize) / 2;
    int textHeight = DS(18), gap = DS(5);
    int inset = (g_dpItemH - iconSize - gap - textHeight) / 2;
    if (inset < DS(3)) inset = DS(3);
    int iconY = y + inset;
    if (g_hDockIcons[idx])
        DrawIconEx(hdc, iconX, iconY, g_hDockIcons[idx],
                   iconSize, iconSize, 0, NULL, DI_NORMAL);

    Command *command = Dash_FindCommand(g_cfg.dockItems[idx].commandId);
    if (command || g_cfg.dockItems[idx].folderId) {
        wchar_t name[SC_NAME_CAP];
        const char *label = g_cfg.dockItems[idx].folderId ? g_cfg.dockItems[idx].folderName : command->name;
        SC_WideFromUtf8(name, SC_NAME_CAP, label);
        RECT textRc = {x + DS(5), iconY + iconSize + gap,
                       x + g_dpItemW - DS(5), y + g_dpItemH - DS(2)};
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        HFONT oldFont = SelectObject(hdc, g_hSmallFont);
        DrawTextW(hdc, name, -1, &textRc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
    if (pending >= 0) {
        RECT badge = {x + g_dpItemW - DS(31), y + DS(4),
                      x + g_dpItemW - DS(4), y + DS(24)};
        Dash_RoundFill(hdc, &badge, DS(10), CLR_ACCENT);
        wchar_t order[16];
        swprintf(order, 16, L"%d", pending + 1);
        SetTextColor(hdc, CLR_ON_ACCENT);
        HFONT oldFont = SelectObject(hdc, g_hSmallFont);
        DrawTextW(hdc, order, -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
}

static void Dash_DockWheel(int delta)
{
    if (g_dragIdx >= 0 || g_dockDialog) return;
    int total = g_dockWheel + delta;
    int steps = total / WHEEL_DELTA;
    int rest = total % WHEEL_DELTA;
    if (g_dockRowsVisible < g_dockTotalRows) {
        g_dockRowOffset -= steps;
        if (g_dockRowOffset < 0) g_dockRowOffset = 0;
        if (g_dockRowOffset > g_dockTotalRows - g_dockRowsVisible)
            g_dockRowOffset = g_dockTotalRows - g_dockRowsVisible;
        g_dockWheel = rest;
        SetScrollPos(g_hDockPane, SB_VERT, g_dockRowOffset, TRUE);
        InvalidateRect(g_hDockPane, NULL, FALSE);
        return;
    }
    int page = g_dockPage - steps;
    if (page < 0) page = 0;
    if (page >= Dock_PageCount(g_dockFolder)) page = Dock_PageCount(g_dockFolder) - 1;
    if (page != g_dockPage) Dash_SetDockView(g_dockFolder, page);
    g_dockWheel = rest;
}

static LRESULT CALLBACK DockPaneProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO scroll = {sizeof(scroll), SIF_ALL, 0, 0, 0, 0, 0};
        GetScrollInfo(hWnd, SB_VERT, &scroll);
        int offset = g_dockRowOffset;
        switch (LOWORD(wp)) {
        case SB_LINEUP: offset--; break;
        case SB_LINEDOWN: offset++; break;
        case SB_PAGEUP: offset -= g_dockRowsVisible; break;
        case SB_PAGEDOWN: offset += g_dockRowsVisible; break;
        case SB_THUMBTRACK: offset = scroll.nTrackPos; break;
        default: return 0;
        }
        if (offset < 0) offset = 0;
        if (offset > g_dockTotalRows - g_dockRowsVisible) offset = g_dockTotalRows - g_dockRowsVisible;
        g_dockRowOffset = offset;
        SetScrollPos(hWnd, SB_VERT, offset, TRUE);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        int cell = g_dockKeyCell;
        switch (wp) {
        case VK_LEFT: cell--; break;
        case VK_RIGHT: cell++; break;
        case VK_UP: cell -= g_dockColumns; break;
        case VK_DOWN: cell += g_dockColumns; break;
        case VK_HOME: cell = 0; break;
        case VK_END: cell = DOCK_PAGE_SIZE - 1; break;
        case VK_PRIOR: Dash_SetDockView(g_dockFolder, g_dockPage > 0 ? g_dockPage - 1 : 0); return 0;
        case VK_NEXT: if (g_dockPage + 1 < Dock_PageCount(g_dockFolder)) Dash_SetDockView(g_dockFolder, g_dockPage + 1); return 0;
        case VK_ESCAPE: if (g_pendingCount || !g_dockFolder) Dashboard_Close(); else Dash_SetDockView(0, 0); return 0;
        case VK_F6:
        case VK_TAB: SetFocus(g_hSearchEdit); return 0;
        case VK_RETURN:
        case VK_SPACE: {
            int index = DockPane_CellUsed(g_dockPage * DOCK_PAGE_SIZE + cell, -1);
            if (index >= 0) {
                if (g_cfg.dockItems[index].folderId) Dash_SetDockView(g_cfg.dockItems[index].folderId, 0);
                else Dash_ActivateCommand(g_cfg.dockItems[index].commandId, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
            }
            return 0;
        }
        default: return DefWindowProcW(hWnd, msg, wp, lp);
        }
        if (cell < 0) cell = 0;
        if (cell >= DOCK_PAGE_SIZE) cell = DOCK_PAGE_SIZE - 1;
        g_dockKeyCell = cell;
        int row = POS_ROW(cell);
        if (row < g_dockRowOffset) g_dockRowOffset = row;
        if (row >= g_dockRowOffset + g_dockRowsVisible) g_dockRowOffset = row - g_dockRowsVisible + 1;
        SetScrollPos(hWnd, SB_VERT, g_dockRowOffset, TRUE);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        Dash_DockWheel((short)HIWORD(wp));
        return 0;

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
        int iconSize = DS(g_cfg.dashboardCompact ? 32 : 42);

        int visibleCount = 0;
        for (int i = 0; i < g_cfg.dockCount; i++) {
            if (i == g_dragIdx || !Dock_ItemVisible(i)) continue;
            visibleCount++;
            int local = g_dockPos[i] % DOCK_PAGE_SIZE;
            int x = DockPane_ColX(POS_COL(local));
            int y = DockPane_RowY(POS_ROW(local));
            DockPane_DrawItem(mem, i, x, y, iconSize, 0);
        }
        if (GetFocus() == hWnd) {
            int row = POS_ROW(g_dockKeyCell);
            RECT focus = {DockPane_ColX(POS_COL(g_dockKeyCell)), DockPane_RowY(row),
                          DockPane_ColX(POS_COL(g_dockKeyCell)) + g_dpItemW, DockPane_RowY(row) + g_dpItemH};
            Dash_RoundStroke(mem, &focus, DS(10), DS(2), CLR_ACCENT);
        }
        if (!visibleCount && g_dragIdx < 0) {
            RECT hint = {DS(16), client.bottom / 2 - DS(28),
                         client.right - DS(16), client.bottom / 2};
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, CLR_SECONDARY);
            HFONT oldFont = SelectObject(mem, g_hBodyFont);
            DrawTextW(mem, SC_Text(L"No shortcuts on this page", L"此页面暂无快捷方式"), -1, &hint,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            hint.top = hint.bottom + DS(4);
            hint.bottom = hint.top + DS(22);
            SelectObject(mem, g_hSmallFont);
            DrawTextW(mem, SC_Text(L"Add a command or move shortcuts here", L"添加命令或将快捷方式移到这里"), -1, &hint,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(mem, oldFont);
        }

        if (g_dragIdx >= 0 && g_dragIdx < g_cfg.dockCount) {
            int col = DockPane_SnapCol(g_dragCurX);
            int row = g_dpStepY > 0 ? g_dragCurY / g_dpStepY + g_dockRowOffset : 0;
            if (row < 0) row = 0;
            if (row >= g_dockTotalRows) row = g_dockTotalRows - 1;
            RECT target = {DockPane_ColX(col), DockPane_RowY(row),
                           DockPane_ColX(col) + g_dpItemW,
                            DockPane_RowY(row) + g_dpItemH};
            Dash_RoundFill(mem, &target, DS(12), CLR_HOVER);
            Dash_RoundStroke(mem, &target, DS(12), DS(2), CLR_ACCENT);

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
            if (wp & MK_CONTROL) {
                g_ctrlClickIdx = idx;
                SetCapture(hWnd);
                return 0;
            }
            int local = g_dockPos[idx] % DOCK_PAGE_SIZE;
            int itemX = DockPane_ColX(POS_COL(local));
            int itemY = DockPane_RowY(POS_ROW(local));
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
        if (g_ctrlClickIdx >= 0) {
            int idx = g_ctrlClickIdx;
            g_ctrlClickIdx = -1;
            ReleaseCapture();
            PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
            if (idx == DockPane_HitTest(GET_X_LPARAM_SC(lp), GET_Y_LPARAM_SC(lp))) {
                if (g_cfg.dockItems[idx].folderId)
                    Dash_SetDockView(g_cfg.dockItems[idx].folderId, 0);
                else Dash_ActivateCommand(g_cfg.dockItems[idx].commandId, 1);
            }
            return 0;
        }
        if (g_dragIdx >= 0) {
            int idx = g_dragIdx;
            g_dragIdx = -1;
            /* ReleaseCapture sends WM_CAPTURECHANGED synchronously.  Clear the
               drag state first, otherwise that message invalidates idx before
               this drop is committed. */
            g_dragCurX = GET_X_LPARAM_SC(lp);
            g_dragCurY = GET_Y_LPARAM_SC(lp);
            ReleaseCapture();
            PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
            int dx = g_dragCurX - g_dragStartX;
            int dy = g_dragCurY - g_dragStartY;
            if (dx * dx + dy * dy < DS(5) * DS(5)) {
                if (g_cfg.dockItems[idx].folderId)
                    Dash_SetDockView(g_cfg.dockItems[idx].folderId, 0);
                else {
                    Dash_ActivateCommand(g_cfg.dockItems[idx].commandId,
                                        (wp & MK_CONTROL) != 0);
                }
                return 0;
            }

            RECT client;
            GetClientRect(hWnd, &client);
            if (!PtInRect(&client, (POINT){g_dragCurX, g_dragCurY})) {
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            int col = DockPane_SnapCol(g_dragCurX);
            int row = g_dpStepY > 0 ? g_dragCurY / g_dpStepY + g_dockRowOffset : 0;
            if (row < 0) row = 0;
            if (row >= g_dockTotalRows) row = g_dockTotalRows - 1;
            int target = g_dockPage * DOCK_PAGE_SIZE + POS(row, col);
            if (POS(row, col) >= DOCK_PAGE_SIZE) return 0;
            if (target != g_dockPos[idx]) {
                int occupied = DockPane_CellUsed(target, idx);
                if (occupied >= 0 && g_cfg.dockItems[occupied].folderId && !g_cfg.dockItems[idx].folderId) {
                    Dock_MoveItem(idx, g_cfg.dockItems[occupied].folderId, 0);
                } else if (occupied >= 0) {
                    int old = g_dockPos[idx];
                    g_dockPos[idx] = g_dockPos[occupied];
                    g_dockPos[occupied] = old;
                } else {
                    g_dockPos[idx] = target;
                }
                if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
                Dash_ResizeForDock(GetParent(hWnd), NULL);
                Dash_Layout(GetParent(hWnd));
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_ctrlClickIdx = -1;
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
            int isFolder = g_cfg.dockItems[idx].folderId > 0;
            AppendMenuW(menu, MF_STRING, 1, isFolder ? SC_Text(L"Dissolve folder", L"解散文件夹") :
                                                     SC_Text(L"Remove shortcut", L"移除快捷方式"));
            if (isFolder) {
                AppendMenuW(menu, MF_STRING, 4, SC_Text(L"Open folder", L"打开文件夹"));
                AppendMenuW(menu, MF_STRING, 5, SC_Text(L"Rename folder...", L"重命名文件夹..."));
            }
            AppendMenuW(menu, MF_STRING, 2, SC_Text(L"Choose custom icon...", L"选择自定义图标..."));
            AppendMenuW(menu, MF_STRING |
                        (g_cfg.dockItems[idx].iconSource[0] ? MF_ENABLED : MF_GRAYED),
                        3, SC_Text(L"Reset icon", L"恢复默认图标"));
            HMENU move = CreatePopupMenu();
            HMENU home = CreatePopupMenu();
            for (int p = 0; p < Dock_PageCount(0); p++) {
                wchar_t label[32];
                swprintf(label, 32, SC_Text(L"Page %d", L"第 %d 页"), p + 1);
                UINT flags = MF_STRING;
                if (!g_cfg.dockItems[idx].parentFolderId && g_dockPos[idx] / DOCK_PAGE_SIZE == p) flags |= MF_GRAYED;
                AppendMenuW(home, flags, 1000 + p, label);
            }
            if (Dock_PageCount(0) < MAX_DOCK_PAGES)
                AppendMenuW(home, MF_STRING, 1000 + Dock_PageCount(0), SC_Text(L"New page", L"新建页面"));
            AppendMenuW(move, MF_POPUP, (UINT_PTR)home, SC_Text(L"Home", L"主页"));
            if (g_dockFolder) {
                HMENU pages = CreatePopupMenu();
                for (int p = 0; p < Dock_PageCount(g_dockFolder); p++) {
                    wchar_t label[32];
                    swprintf(label, 32, SC_Text(L"Page %d", L"第 %d 页"), p + 1);
                    AppendMenuW(pages, MF_STRING | (p == g_dockPage ? MF_GRAYED : 0), 3000 + p, label);
                }
                if (Dock_PageCount(g_dockFolder) < MAX_DOCK_PAGES)
                    AppendMenuW(pages, MF_STRING, 3000 + Dock_PageCount(g_dockFolder), SC_Text(L"New page", L"新建页面"));
                AppendMenuW(move, MF_POPUP, (UINT_PTR)pages, SC_Text(L"Current folder", L"当前文件夹"));
            }
            if (!isFolder) {
                for (int i = 0; i < g_cfg.dockCount; i++) {
                    if (!g_cfg.dockItems[i].folderId) continue;
                    wchar_t label[SC_NAME_CAP];
                    SC_WideFromUtf8(label, SC_NAME_CAP, g_cfg.dockItems[i].folderName);
                    AppendMenuW(move, MF_STRING | (g_cfg.dockItems[i].folderId == g_dockFolder ? MF_GRAYED : 0),
                                2000 + i, label);
                }
            }
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)move, SC_Text(L"Move to", L"移动到"));
            int selected = Dash_TrackMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point, GetParent(hWnd));
            DestroyMenu(menu);
            if (selected == 1) {
                if (isFolder) Dock_DissolveFolder(idx);
                else Dock_RemoveItem(idx);
                if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
                Dash_RefreshDockPane();
                Dash_ResizeForDock(GetParent(hWnd), NULL);
                Dash_Layout(GetParent(hWnd));
            } else if (selected == 2) {
                DockPane_ChooseCustomIcon(GetParent(hWnd), idx);
            } else if (selected == 3) {
                DockPane_ResetCustomIcon(idx);
            } else if (selected == 4) {
                Dash_SetDockView(g_cfg.dockItems[idx].folderId, 0);
            } else if (selected == 5) {
                Dash_RenameFolder(GetParent(hWnd), idx);
            } else if (selected >= 1000 && selected < 1000 + MAX_DOCK_PAGES) {
                if (Dock_MoveItem(idx, 0, selected - 1000)) if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            } else if (selected >= 2000 && selected < 2000 + g_cfg.dockCount) {
                if (Dock_MoveItem(idx, g_cfg.dockItems[selected - 2000].folderId, 0)) if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            } else if (selected >= 3000 && selected < 3000 + MAX_DOCK_PAGES) {
                if (Dock_MoveItem(idx, g_dockFolder, selected - 3000)) if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
            }
            Dash_UpdateDockNavigation();
            if (Dashboard_IsVisible()) SetFocus(g_hSearchEdit);
        } else {
            POINT point;
            GetCursorPos(&point);
            Dash_ShowAddMenu(GetParent(hWnd), point);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void SearchBox_Update(void)
{
    Dash_HideSearchNotice();
    char text[sizeof(g_sbText) * 2];
    if (!SC_Utf8FromWide(text, sizeof(text), g_sbText)) text[0] = 0;
    Dash_FillHistory(g_sbLen > 0 ? text : NULL);
}

static void SearchBox_SetText(const wchar_t *text)
{
    lstrcpynW(g_sbText, text, 512);
    g_sbLen = (int)wcslen(g_sbText);
    g_sbCursor = g_sbLen;
    g_sbAnchor = g_sbCursor;
    g_sbHasUndo = 0;
    SearchBox_Update();
    InvalidateRect(g_hSearchEdit, NULL, FALSE);
}

static void SearchBox_SaveUndo(void)
{
    memcpy(g_sbUndo, g_sbText, sizeof(g_sbText));
    g_sbUndoCursor = g_sbCursor; g_sbUndoAnchor = g_sbAnchor; g_sbHasUndo = 1;
}

static int SearchBox_ReplaceSelection(const wchar_t *insert)
{
    int first = g_sbCursor < g_sbAnchor ? g_sbCursor : g_sbAnchor;
    int last = g_sbCursor > g_sbAnchor ? g_sbCursor : g_sbAnchor;
    size_t length = wcslen(insert), available = 510 - (g_sbLen - (last - first));
    if (length > available) length = available;
    if (length && insert[length - 1] >= 0xD800 && insert[length - 1] <= 0xDBFF) length--;
    if (first == last && !length) return 0;
    SearchBox_SaveUndo();
    memmove(g_sbText + first + length, g_sbText + last, (g_sbLen - last + 1) * sizeof(wchar_t));
    memcpy(g_sbText + first, insert, length * sizeof(wchar_t));
    g_sbLen += (int)length - (last - first);
    g_sbAnchor = g_sbCursor = first + (int)length;
    g_sbCaretOn = 1;
    SearchBox_Update();
    InvalidateRect(g_hSearchEdit, NULL, FALSE);
    return 1;
}

static int SearchBox_Prev(int pos)
{
    if (pos > 0) pos--;
    if (pos > 0 && g_sbText[pos] >= 0xDC00 && g_sbText[pos] <= 0xDFFF &&
        g_sbText[pos - 1] >= 0xD800 && g_sbText[pos - 1] <= 0xDBFF) pos--;
    return pos;
}

static int SearchBox_Next(int pos)
{
    if (pos < g_sbLen && g_sbText[pos] >= 0xD800 && g_sbText[pos] <= 0xDBFF &&
        g_sbText[pos + 1] >= 0xDC00 && g_sbText[pos + 1] <= 0xDFFF) pos++;
    if (pos < g_sbLen) pos++;
    return pos;
}

static int SearchBox_CopySelection(HWND window)
{
    int first = g_sbCursor < g_sbAnchor ? g_sbCursor : g_sbAnchor;
    int last = g_sbCursor > g_sbAnchor ? g_sbCursor : g_sbAnchor;
    if (first == last || !OpenClipboard(window)) return 0;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (last - first + 1) * sizeof(wchar_t));
    wchar_t *text = memory ? GlobalLock(memory) : NULL;
    int copied = 0;
    if (text) {
        memcpy(text, g_sbText + first, (last - first) * sizeof(wchar_t));
        text[last - first] = 0;
        GlobalUnlock(memory);
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) copied = 1;
    }
    if (!copied && memory) GlobalFree(memory);
    CloseClipboard();
    return copied;
}

static void SearchBox_Paste(HWND window)
{
    if (!OpenClipboard(window)) return;
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *source = memory ? GlobalLock(memory) : NULL;
    wchar_t insert[512]; int count = 0;
    if (source) {
        SIZE_T maxChars = GlobalSize(memory) / sizeof(wchar_t);
        for (SIZE_T i = 0; i < maxChars && source[i] && count < 510; i++) {
            wchar_t c = source[i];
            if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            if ((c>=L'a' && c<=L'z') || (c>=L'A' && c<=L'Z') || (c>=L'0' && c<=L'9')) insert[count++] = c;
        }
        insert[count] = 0;
        GlobalUnlock(memory);
    }
    CloseClipboard();
    if (source) SearchBox_ReplaceSelection(insert);
}

static int SearchBox_HitPosition(HWND window, int x)
{
    HDC dc = GetDC(window);
    if (!dc) return g_sbCursor;
    HFONT old = SelectObject(dc, g_hSearchFont);
    int position = 0;
    for (int i = 1; i <= g_sbLen; i++) {
        SIZE size;
        GetTextExtentPoint32W(dc, g_sbText, i, &size);
        if (DS(39) + size.cx > x) break;
        position = i;
    }
    SelectObject(dc, old); ReleaseDC(window, dc);
    if (position > 0 && g_sbText[position] >= 0xDC00 && g_sbText[position] <= 0xDFFF) position--;
    return position;
}

static LRESULT CALLBACK SearchBoxWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_sbText[0] = 0;
        g_sbLen = g_sbCursor = g_sbFocus = g_sbCaretOn = 0;
        ImmAssociateContext(hWnd, NULL);
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
        SetFocus(hWnd);g_sbCursor=SearchBox_HitPosition(hWnd,GET_X_LPARAM_SC(lp));
        if(!(GetKeyState(VK_SHIFT)&0x8000))g_sbAnchor=g_sbCursor;
        SetCapture(hWnd);InvalidateRect(hWnd,NULL,FALSE);return 0;
    case WM_MOUSEMOVE:
        if(GetCapture()==hWnd && (wp&MK_LBUTTON)){g_sbCursor=SearchBox_HitPosition(hWnd,GET_X_LPARAM_SC(lp));InvalidateRect(hWnd,NULL,FALSE);}return 0;
    case WM_LBUTTONUP:
        if(GetCapture()==hWnd)ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        g_sbAnchor=0;g_sbCursor=g_sbLen;InvalidateRect(hWnd,NULL,FALSE);return 0;
    case WM_COPY:SearchBox_CopySelection(hWnd);return 0;
    case WM_CUT:if(SearchBox_CopySelection(hWnd))SearchBox_ReplaceSelection(L"");return 0;
    case WM_PASTE:SearchBox_Paste(hWnd);return 0;
    case WM_UNDO:
        if(g_sbHasUndo){wchar_t text[512];memcpy(text,g_sbText,sizeof(text));int cursor=g_sbCursor,anchor=g_sbAnchor;memcpy(g_sbText,g_sbUndo,sizeof(g_sbText));g_sbLen=(int)wcslen(g_sbText);g_sbCursor=g_sbUndoCursor;g_sbAnchor=g_sbUndoAnchor;memcpy(g_sbUndo,text,sizeof(text));g_sbUndoCursor=cursor;g_sbUndoAnchor=anchor;SearchBox_Update();InvalidateRect(hWnd,NULL,FALSE);}return 0;
    case WM_CHAR:
        if((wp>='a' && wp<='z') || (wp>='A' && wp<='Z') || (wp>='0' && wp<='9')){wchar_t text[2]={(wchar_t)wp,0};SearchBox_ReplaceSelection(text);}return 0;
    case WM_IME_CHAR:
    case WM_IME_COMPOSITION:return 0;
    case WM_KEYDOWN: {
        int shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(ctrl){
            if(wp=='A'){g_sbAnchor=0;g_sbCursor=g_sbLen;InvalidateRect(hWnd,NULL,FALSE);return 0;}
            if(wp=='C'){SendMessageW(hWnd,WM_COPY,0,0);return 0;}
            if(wp=='X'){SendMessageW(hWnd,WM_CUT,0,0);return 0;}
            if(wp=='V'){SendMessageW(hWnd,WM_PASTE,0,0);return 0;}
            if(wp=='Z'){SendMessageW(hWnd,WM_UNDO,0,0);return 0;}
        }
        switch(wp){
        case VK_BACK:
        case VK_DELETE:
            if(g_sbAnchor==g_sbCursor){if(wp==VK_BACK)g_sbAnchor=SearchBox_Prev(g_sbCursor);else g_sbAnchor=SearchBox_Next(g_sbCursor);}
            SearchBox_ReplaceSelection(L"");return 0;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
            if(wp==VK_HOME)g_sbCursor=0;else if(wp==VK_END)g_sbCursor=g_sbLen;
            else if(!shift && g_sbAnchor!=g_sbCursor)g_sbCursor=wp==VK_LEFT?(g_sbAnchor<g_sbCursor?g_sbAnchor:g_sbCursor):(g_sbAnchor>g_sbCursor?g_sbAnchor:g_sbCursor);
            else g_sbCursor=wp==VK_LEFT?SearchBox_Prev(g_sbCursor):SearchBox_Next(g_sbCursor);
            if(!shift)g_sbAnchor=g_sbCursor;
            InvalidateRect(hWnd,NULL,FALSE);return 0;
        case VK_F6:SetFocus(g_hDockPane);return 0;
        case VK_RETURN:
            Dash_ExecuteSel(g_hHistList);
            return 0;
        case VK_SPACE:
            if (g_sbLen > 0 || SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0)
                Dash_ExecuteSel(g_hHistList);
            else
                Dashboard_Close();
            return 0;
        case VK_ESCAPE:
            if (g_pendingCount) Dashboard_Close();
            else if (g_dockFolder) Dash_SetDockView(0, 0);
            else Dashboard_Close();
            return 0;
        case VK_TAB:
            if (SendMessageW(g_hHistList, LB_GETCOUNT, 0, 0) > 0) {
                int len = (int)SendMessageW(g_hHistList, LB_GETTEXTLEN, 0, 0);
                if (len > 0 && len < 128) {
                    wchar_t text[SC_NAME_CAP];
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
            RECT focus = {0, 0, client.right, client.bottom};
            Dash_RoundStroke(mem, &focus, client.bottom, DS(2), CLR_ACCENT);
        }

        int iconX = DS(15);
        int iconY = client.bottom / 2 - DS(6);
        RECT circle = {iconX, iconY, iconX + DS(10), iconY + DS(10)};
        Dash_RoundStroke(mem, &circle, DS(10), DS(2), CLR_SECONDARY);
        Dash_LineAA(mem, iconX + DS(8), iconY + DS(8), iconX + DS(14), iconY + DS(14), DS(2), CLR_SECONDARY);

        RECT textRc = {DS(39), 0, client.right - DS(14), client.bottom};
        SetBkMode(mem, TRANSPARENT);
        HFONT oldFont = SelectObject(mem, g_hSearchFont);
        if (g_sbLen == 0) {
            SetTextColor(mem, CLR_SECONDARY);
            DrawTextW(mem, SC_Text(L"Search commands", L"搜索命令"), -1, &textRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            int first=g_sbCursor<g_sbAnchor?g_sbCursor:g_sbAnchor,last=g_sbCursor>g_sbAnchor?g_sbCursor:g_sbAnchor;
            if(first!=last){SIZE a={0},b={0};GetTextExtentPoint32W(mem,g_sbText,first,&a);GetTextExtentPoint32W(mem,g_sbText,last,&b);RECT selected={textRc.left+a.cx,DS(8),textRc.left+b.cx,client.bottom-DS(8)};HBRUSH brush=CreateSolidBrush(CLR_HOVER);FillRect(mem,&selected,brush);DeleteObject(brush);}
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
    /* Handle Ctrl clicks ourselves so clicking the same already-selected
       LISTBOX row can toggle it off (LBN_SELCHANGE would not fire again). */
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && (wp & MK_CONTROL)) {
        LRESULT hit = SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(hit) && (int)LOWORD(hit) < (int)SendMessageW(hWnd, LB_GETCOUNT, 0, 0)) {
            SetPropW(hWnd, L"SC_CtrlClick", (HANDLE)(LONG_PTR)(LOWORD(hit) + 1));
            SetCapture(hWnd);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP && GetPropW(hWnd, L"SC_CtrlClick")) {
        int row = (int)(LONG_PTR)GetPropW(hWnd, L"SC_CtrlClick") - 1;
        RemovePropW(hWnd, L"SC_CtrlClick");
        ReleaseCapture();
        LRESULT hit = SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(hit) && (int)LOWORD(hit) == row) {
            SendMessageW(hWnd, LB_SETCURSEL, row, 0);
            int cmdId = (int)SendMessageW(hWnd, LB_GETITEMDATA, row, 0);
            Dash_ActivateCommand(cmdId, 1);
        }
        PostMessageW(GetParent(hWnd), WM_SC_REFOCUS, 0, 0);
        return 0;
    }
    if (msg == WM_CAPTURECHANGED) RemovePropW(hWnd, L"SC_CtrlClick");
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
        RemovePropW(hWnd, L"SC_CtrlClick");
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

static LRESULT CALLBACK Dash_ButtonHoverProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    /* Dialog keyboard navigation may change the default-button style. Keep
       our rendering while letting the dialog manager track the default ID. */
    if (msg == BM_SETSTYLE)
        return DefSubclassProc(hWnd, msg, (wp & ~BS_TYPEMASK) | BS_OWNERDRAW, lp);
    if (msg == WM_MOUSEMOVE) {
        if (!GetPropW(hWnd, L"SC_ButtonHot")) {
            SetPropW(hWnd, L"SC_ButtonHot", (HANDLE)(INT_PTR)1);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hWnd, 0};
        TrackMouseEvent(&track);
    } else if (msg == WM_MOUSELEAVE || msg == WM_ENABLE) {
        RemovePropW(hWnd, L"SC_ButtonHot");
        InvalidateRect(hWnd, NULL, FALSE);
    } else if (msg == WM_SETCURSOR && IsWindowEnabled(hWnd) && LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    } else if (msg == WM_NCDESTROY) {
        RemovePropW(hWnd, L"SC_ButtonHot");
        RemoveWindowSubclass(hWnd, Dash_ButtonHoverProc, id);
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

static void Dash_AddDockItem(HWND hParent)
{
    if (g_cfg.dockCount >= MAX_DOCK_ITEMS) {
        MessageBoxW(hParent, SC_Text(L"The shortcuts grid is full.", L"快捷方式网格已满。"),
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    LRESULT selected = SendMessageW(g_hHistList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        MessageBoxW(hParent, SC_Text(L"Select a command first.", L"请先选择一个命令。"),
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int cmdId = (int)SendMessageW(g_hHistList, LB_GETITEMDATA, selected, 0);
    if (cmdId <= 0 || !Dash_FindCommand(cmdId)) {
        MessageBoxW(hParent, SC_Text(L"Only configured commands can be added.", L"只能添加已配置的命令。"),
                    L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (int i = 0; i < g_cfg.dockCount; i++) {
        if (g_cfg.dockItems[i].commandId == cmdId) {
            MessageBoxW(hParent, SC_Text(L"This command is already in Custom shortcuts.", L"该命令已在自定义快捷方式中。"),
                        L"SC.zhichg", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    int pos = Dock_AllocCell(g_dockFolder, g_dockPage, -1);
    if (pos < 0) return;
    memset(&g_cfg.dockItems[g_cfg.dockCount], 0, sizeof(DockItem));
    g_cfg.dockItems[g_cfg.dockCount].commandId = cmdId;
    g_cfg.dockItems[g_cfg.dockCount].parentFolderId = g_dockFolder;
    g_dockPos[g_cfg.dockCount] = pos;
    Dock_EnsurePage(g_dockFolder, pos);
    g_dockPage = pos / DOCK_PAGE_SIZE;
    g_cfg.dockCount++;
    if(!Config_Save())SC_ReportError(SC_Text(L"Save Dashboard",L"保存仪表盘"),Config_GetLastError());
    Dash_RefreshDockPane();
    Dash_ResizeForDock(hParent, NULL);
    Dash_Layout(hParent);
    SearchBox_SetText(L"");
    Dash_UpdateDockNavigation();
}

static LRESULT CALLBACK DashboardWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_dashDpi = Dash_GetWindowDpi(hWnd);
        g_hWindowBrush = CreateSolidBrush(CLR_WINDOW);
        g_hCardBrush = CreateSolidBrush(CLR_CARD);
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
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LBS_NOTIFY |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED,
            0, 0, 10, 10, hWnd, (HMENU)DID_HIST_LIST, g_hInst, NULL);
        g_hMostList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED,
            0, 0, 10, 10, hWnd, (HMENU)DID_MOST_LIST, g_hInst, NULL);
        g_hSearchNotice = CreateWindowW(L"STATIC", SC_Text(L"Command not found", L"未找到命令"), WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_SEARCH_NOTICE, g_hInst, NULL);
        g_hDockPane = CreateWindowExW(0, L"SC_DockPane", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_PANE, g_hInst, NULL);
        g_hDockBack = CreateWindowW(L"BUTTON", SC_Text(L"Home", L"主页"), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_BACK, g_hInst, NULL);
        g_hDockAdd = CreateWindowW(L"BUTTON", SC_Text(L"Add", L"添加"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_ADD, g_hInst, NULL);
        g_hDockPrev = CreateWindowW(L"BUTTON", SC_Text(L"Prev", L"上一页"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_PREV, g_hInst, NULL);
        g_hDockNext = CreateWindowW(L"BUTTON", SC_Text(L"Next", L"下一页"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_NEXT, g_hInst, NULL);
        g_hDockPage = CreateWindowW(L"STATIC", L"1 / 1", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 10, 10, hWnd, (HMENU)DID_DOCK_PAGE, g_hInst, NULL);
        HWND buttons[] = {g_hDockBack, g_hDockAdd, g_hDockPrev, g_hDockNext};
        for (int i = 0; i < 4; i++) SetWindowSubclass(buttons[i], Dash_ButtonHoverProc, 1, 0);

        Dash_SubclassList(g_hHistList);
        Dash_SubclassList(g_hMostList);
        Dash_CreateFonts();
        /* Size owner-draw lists before inserting items so their initial
           top index cannot scroll past the first history entry. */
        Dash_Layout(hWnd);
        Dash_FillMostUsed();
        Dash_FillHistory(NULL);
        return 0;
    }

    case WM_SIZE:
        Dash_UpdateWindowShape(hWnd);
        Dash_Layout(hWnd);
        return 0;

    case WM_MOVE:
        Dash_SyncSmoothFrame(hWnd, IsWindowVisible(hWnd));
        return 0;

    case WM_SHOWWINDOW:
        Dash_SyncSmoothFrame(hWnd, (int)wp);
        break;

    case WM_MOUSEACTIVATE:
        PostMessageW(hWnd, WM_SC_REFOCUS, 0, 0);
        return MA_ACTIVATE;

    case WM_SETFOCUS:
        if (!g_dockDialog && !GetCapture() && g_hSearchEdit) SetFocus(g_hSearchEdit);
        return 0;

    case WM_SC_REFOCUS:
        /* A button/dock click owns mouse capture until mouse-up. Moving focus
           now can make the previous BUTTON release capture and cancel the
           pending click (or dock drag). Commands refocus after activation. */
        if (!g_dockDialog && !GetCapture() && Dashboard_IsVisible() && g_hDash == hWnd && GetForegroundWindow()==hWnd &&
            g_hSearchEdit && IsWindow(g_hSearchEdit))
            SetFocus(g_hSearchEdit);
        return 0;

    case WM_MOUSEWHEEL: {
        POINT point = {GET_X_LPARAM_SC(lp), GET_Y_LPARAM_SC(lp)};
        RECT dock;
        GetWindowRect(g_hDockPane, &dock);
        if (PtInRect(&dock, point)) {
            Dash_DockWheel((short)HIWORD(wp));
            return 0;
        }
        break;
    }

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wp);
        if (newDpi < 96) newDpi = 96;
        g_dashDpi = newDpi;
        Dash_CreateFonts();
        Dash_RequestIcons();
        Dash_ResizeForDock(hWnd, (const RECT*)lp);
        Dash_Layout(hWnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC:
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
        if (draw->CtlType == ODT_STATIC && draw->CtlID == DID_SEARCH_NOTICE) {
            FillRect(draw->hDC, &draw->rcItem, g_hCardBrush);
            Dash_RoundFill(draw->hDC, &draw->rcItem, DS(12), CLR_SEARCH);
            Dash_RoundOutline(draw->hDC, &draw->rcItem, DS(12), CLR_BORDER);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, CLR_SECONDARY);
            HFONT oldFont = SelectObject(draw->hDC, g_hSmallFont);
            RECT text = draw->rcItem;
            InflateRect(&text, -DS(12), 0);
            DrawTextW(draw->hDC, SC_Text(L"Command not found", L"未找到命令"), -1, &text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(draw->hDC, oldFont);
            return TRUE;
        }
        if (draw->CtlType == ODT_STATIC && draw->CtlID == DID_DOCK_PAGE) {
            FillRect(draw->hDC, &draw->rcItem, g_hCardBrush);
            RECT rect = draw->rcItem;
            InflateRect(&rect, -DS(1), -DS(1));
            Dash_RoundFill(draw->hDC, &rect, DS(10), CLR_HOVER);
            wchar_t text[32];
            GetWindowTextW(draw->hwndItem, text, 32);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, CLR_ACCENT);
            HFONT oldFont = SelectObject(draw->hDC, g_hSmallFont);
            DrawTextW(draw->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(draw->hDC, oldFont);
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON && draw->CtlID >= DID_DOCK_BACK && draw->CtlID <= DID_DOCK_NEXT) {
            int chevron = draw->CtlID == DID_DOCK_ADD ? 0 : draw->CtlID == DID_DOCK_NEXT ? 1 : -1;
            Dash_DrawButton(draw, 0, chevron);
            return TRUE;
        }
        if ((draw->CtlID != DID_HIST_LIST && draw->CtlID != DID_MOST_LIST) ||
            (int)draw->itemID < 0) break;

        int hover = (int)(LONG_PTR)GetPropW(draw->hwndItem, L"SC_Hover") ==
                    (int)draw->itemID;
        int selected = (draw->itemState & ODS_SELECTED) ||
            (draw->hwndItem == g_hHistList &&
             (int)draw->itemID == g_histVisualSelection);
        int pending = Dash_PendingIndex((int)draw->itemData);
        COLORREF background = CLR_CARD;
        COLORREF foreground = CLR_TEXT;
        if (selected) {
            background = CLR_ACCENT;
            foreground = CLR_ON_ACCENT;
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
        if (pending >= 0) {
            RECT badge = {textRc.left, textRc.top + DS(7),
                          textRc.left + DS(27), textRc.bottom - DS(7)};
            Dash_RoundFill(draw->hDC, &badge, DS(10), selected ? CLR_ON_ACCENT : CLR_ACCENT);
            wchar_t order[16];
            swprintf(order, 16, L"%d", pending + 1);
            SetTextColor(draw->hDC, selected ? CLR_ACCENT : CLR_ON_ACCENT);
            SelectObject(draw->hDC, g_hSmallFont);
            DrawTextW(draw->hDC, order, -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(draw->hDC, foreground);
            SelectObject(draw->hDC, g_hBodyFont);
            textRc.left = badge.right + DS(8);
        }
        if (draw->CtlID == DID_MOST_LIST && (int)draw->itemID < 8) {
            RECT countRc = textRc;
            countRc.left = countRc.right - DS(58);
            textRc.right = countRc.left - DS(6);
            wchar_t count[24];
            swprintf(count, 24, L"%d×", g_mostCounts[draw->itemID]);
            countRc.top += DS(6);
            countRc.bottom -= DS(6);
            if (!selected) Dash_RoundFill(draw->hDC, &countRc, DS(8), CLR_SEARCH);
            SetTextColor(draw->hDC, selected ? CLR_ON_ACCENT : CLR_SECONDARY);
            SelectObject(draw->hDC, g_hSmallFont);
            DrawTextW(draw->hDC, count, -1, &countRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(draw->hDC, foreground);
            SelectObject(draw->hDC, g_hBodyFont);
        }
        DrawTextW(draw->hDC, text, -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(draw->hDC, oldFont);

        if (!selected && !hover && (int)draw->itemID + 1 < (int)SendMessageW(draw->hwndItem, LB_GETCOUNT, 0, 0)) {
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
        if (LOWORD(wp) == DID_DOCK_BACK) {
            Dash_SetDockView(0, 0);
            SetFocus(g_hSearchEdit);
            return 0;
        }
        if (LOWORD(wp) == DID_DOCK_PREV || LOWORD(wp) == DID_DOCK_NEXT) {
            Dash_SetDockView(g_dockFolder, g_dockPage + (LOWORD(wp) == DID_DOCK_NEXT ? 1 : -1));
            SetFocus(g_hSearchEdit);
            return 0;
        }
        if (LOWORD(wp) == DID_DOCK_ADD) {
            RECT button;
            GetWindowRect(g_hDockAdd, &button);
            Dash_ShowAddMenu(hWnd, (POINT){button.left, button.bottom});
            return 0;
        }
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
        HPEN separator = CreatePen(PS_SOLID, DS(1), CLR_SEPARATOR);
        HPEN oldPen = SelectObject(mem, separator);
        MoveToEx(mem, g_rcDockCard.left + DS(16), g_rcDockCard.top + DS(46), NULL);
        LineTo(mem, g_rcDockCard.right - DS(16), g_rcDockCard.top + DS(46));
        SelectObject(mem, oldPen);
        DeleteObject(separator);

        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, CLR_TEXT);
        HFONT oldFont = SelectObject(mem, g_hSectionFont);
        RECT mostTitle = {g_rcMostCard.left + DS(16),
                          g_rcMostCard.top + DS(12),
                          g_rcMostCard.right - DS(16),
                          g_rcMostCard.top + DS(40)};
        DrawTextW(mem, SC_Text(L"Most Used", L"常用命令"), -1, &mostTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        wchar_t dockTitle[SC_NAME_CAP];
        lstrcpyW(dockTitle, SC_Text(L"Custom shortcuts", L"自定义快捷方式"));
        int folderIndex = Dock_FindFolder(g_dockFolder);
        if (folderIndex >= 0)
            SC_WideFromUtf8(dockTitle, SC_NAME_CAP, g_cfg.dockItems[folderIndex].folderName);
        DrawTextW(mem, dockTitle, -1, &g_rcDockTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, oldFont);

        int opacity = g_cfg.dashboardOpacity;
        if (opacity < 20) opacity = 20;
        if (opacity > 100) opacity = 100;
        Dash_PaintSmoothFrame(hWnd, mem, (BYTE)((opacity * 255 + 50) / 100));
        BitBlt(paint, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ACTIVATE:
        if (g_dockDialog) return 0;
        if (LOWORD(wp) == WA_INACTIVE) {
            if(g_dashOpening)return 0;
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

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *position=(WINDOWPOS*)lp;
        if(Dashboard_IsVisible() && !g_dockDialog && !(position->flags&(SWP_NOZORDER|SWP_HIDEWINDOW)))
            position->hwndInsertAfter=HWND_TOPMOST;
        break;
    }

    case WM_TIMER:
        if(wp==DASH_FOCUS_TIMER) {
            if(!Dashboard_IsVisible()){KillTimer(hWnd,DASH_FOCUS_TIMER);g_dashOpening=0;return 0;}
            if(g_dockDialog || GetCapture())return 0;
            if(Dash_TakeSearchFocus()) {
                KillTimer(hWnd,DASH_FOCUS_TIMER);g_dashOpening=0;
            }else if(--g_dashFocusAttempts<=0) {
                Dashboard_Close();
                SC_ReportError(SC_Text(L"Focus Dashboard search",L"聚焦仪表盘搜索框"),ERROR_ACCESS_DENIED);
            }
            return 0;
        }
        if (wp == DASH_MULTI_TIMER) {
            Dash_OpenPending();
            return 0;
        }
        if (wp == DASH_NOTICE_TIMER) {
            Dash_HideSearchNotice();
            return 0;
        }
        break;

    case WM_CLOSE:
        Dashboard_Close();
        return 0;

    case WM_DESTROY:
        g_dashOpening=0;
        KillTimer(hWnd,DASH_FOCUS_TIMER);
        Dash_ResetPending();
        g_ctrlClickIdx = -1;
        KillTimer(hWnd, DASH_NOTICE_TIMER);
        Dash_DestroySmoothFrame(hWnd);
        Dash_DeleteShapeSurface();
        Dash_DeleteFonts();
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
        g_hSearchNotice = NULL;
        g_hDockPane = NULL;
        g_hDockBack = g_hDockAdd = g_hDockPrev = g_hDockNext = g_hDockPage = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void Dashboard_ApplyLanguage(void)
{
    if (!g_hDash || !IsWindow(g_hDash)) return;
    SetWindowTextW(g_hDockBack, SC_Text(L"Home", L"主页"));
    SetWindowTextW(g_hDockAdd, SC_Text(L"Add", L"添加"));
    SetWindowTextW(g_hDockPrev, SC_Text(L"Prev", L"上一页"));
    SetWindowTextW(g_hDockNext, SC_Text(L"Next", L"下一页"));
    SetWindowTextW(g_hSearchNotice, SC_Text(L"Command not found", L"未找到命令"));
    /* Painted labels read the current language. Keep user text, selection,
       the open folder/page and the pending command queue intact. */
    RedrawWindow(g_hDash, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void Dashboard_Init(void)
{
    Dashboard_ApplyTheme();
    if (g_hDash) return;

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

    HANDLE oldContext = Dash_SetThreadDpiContext((HANDLE)(LONG_PTR)-4);

    g_hDash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        L"SC_Dashboard", L"SC.zhichg",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, MulDiv(DASH_BASE_W, g_dpiScale, 96),
        MulDiv(632, g_dpiScale, 96),
        NULL, NULL, g_hInst, NULL);

    if (!g_hDash) {
        if (oldContext) Dash_SetThreadDpiContext(oldContext);
        return;
    }

    Dash_ApplyWindowOpacity(g_hDash);
    Dash_ResizeForDock(g_hDash, NULL);
    Dash_Layout(g_hDash);
    Dash_ValidateDockConfig();
    Dash_RequestIcons();
    Dash_UpdateDockNavigation();
    if (oldContext) Dash_SetThreadDpiContext(oldContext);
}

void Dashboard_Show(void)
{
    /* An owned picker/menu keeps its input while the launcher is open. */
    if(Dashboard_IsVisible() && (g_dockDialog || !IsWindowEnabled(g_hDash)))return;
    if (!Dashboard_IsVisible()) g_dashOrigin = GetForegroundWindow();
    g_dashOpening=1;
    Dashboard_Init();
    if (!g_hDash) {g_dashOpening=0;return;}
    Dash_HideSearchNotice();
    if (Dashboard_IsVisible()) {
        Dash_SetDockView(0, 0);
        goto activate;
    }
    HANDLE oldContext = Dash_SetThreadDpiContext((HANDLE)(LONG_PTR)-4);
    g_dockFolder = g_dockPage = g_dockWheel = g_dockRowOffset = g_dockKeyCell = 0;
    Dash_ResetPending();
    g_ctrlClickIdx = -1;

    /* Refresh only in-memory data. Scripts are synchronized at SC startup
       and in Settings; opening never scans files, saves INI or extracts icons. */
    Dash_ValidateDockConfig();
    Dash_ResizeForDock(g_hDash, NULL);
    Dash_RequestIcons();
    SearchBox_SetText(L"");
    Dash_FillMostUsed();
    Dash_UpdateDockNavigation();
    g_histVisualSelection = -1;
    g_dpHoverIdx = g_dragIdx = -1;
    SetPropW(g_hHistList, L"SC_Hover", (HANDLE)(LONG_PTR)-1);
    SetPropW(g_hMostList, L"SC_Hover", (HANDLE)(LONG_PTR)-1);
    Dash_ApplyWindowOpacity(g_hDash);
    SetTimer(g_hSearchEdit, 1, 530, NULL);
    if (oldContext) Dash_SetThreadDpiContext(oldContext);

activate:
    KillTimer(g_hDash,DASH_FOCUS_TIMER);
    SetWindowPos(g_hDash, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if(Dash_TakeSearchFocus())g_dashOpening=0;
    else {
        g_dashFocusAttempts=8;
        if(!SetTimer(g_hDash,DASH_FOCUS_TIMER,DASH_FOCUS_MS,NULL)) {
            DWORD error=GetLastError();Dashboard_Close();
            SC_ReportError(SC_Text(L"Focus Dashboard search",L"聚焦仪表盘搜索框"),error?error:ERROR_ACCESS_DENIED);
        }
    }

    /* Paint children too so the first visible frame already contains lists. */
    RedrawWindow(g_hDash, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    UpdateWindow(g_hDash);
}

static int Dash_TakeSearchFocus(void)
{
    if(g_dockDialog || !g_hDash || !IsWindowEnabled(g_hDash))return 0;
    SetWindowPos(g_hDash,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    if(GetForegroundWindow()!=g_hDash)SetForegroundWindow(g_hDash);
    if(GetForegroundWindow()!=g_hDash) {
        DWORD other=GetWindowThreadProcessId(GetForegroundWindow(),NULL),current=GetCurrentThreadId();
        if(other && other!=current && AttachThreadInput(current,other,TRUE)) {
            SetForegroundWindow(g_hDash);SetActiveWindow(g_hDash);
            AttachThreadInput(current,other,FALSE);
        }
    }
    if(GetForegroundWindow()!=g_hDash)return 0;
    /* Focus belongs to our own input queue after detaching from the caller. */
    SetActiveWindow(g_hDash);SetFocus(g_hSearchEdit);
    return GetFocus()==g_hSearchEdit;
}

void Dashboard_Close(void)
{
    g_dashOpening=0;
    if(g_hDash)KillTimer(g_hDash,DASH_FOCUS_TIMER);
    Dash_HideSearchNotice();
    Dash_ResetPending();
    g_ctrlClickIdx = -1;
    if (!Dashboard_IsVisible()) return;
    g_dragIdx = g_dpHoverIdx = -1;
    HWND capture = GetCapture();
    if (capture && IsChild(g_hDash, capture)) ReleaseCapture();
    KillTimer(g_hSearchEdit, 1);
    int restore = GetForegroundWindow() == g_hDash;
    ShowWindow(g_hDash, SW_HIDE);
    if (restore && g_dashOrigin && IsWindow(g_dashOrigin) && IsWindowVisible(g_dashOrigin))
        SetForegroundWindow(g_dashOrigin);
}

void Dashboard_Shutdown(void)
{
    if (g_iconEvent) {
        EnterCriticalSection(&g_iconLock);
        g_iconWorkerStop = 1;
        for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
            if (g_readyIcons[i]) DestroyIcon(g_readyIcons[i]);
            g_readyIcons[i] = NULL;
        }
        LeaveCriticalSection(&g_iconLock);
        SetEvent(g_iconEvent);
        /* Do not wait for Shell extensions during exit. The event and lock
           remain alive until process teardown, allowing the worker to finish. */
    }
    if (g_hDash) DestroyWindow(g_hDash);
    Dash_FreeIcons();
}

int Dashboard_IsVisible(void)
{
    return g_hDash && IsWindowVisible(g_hDash);
}

int Dashboard_HasFocus(void)
{
    return Dashboard_IsVisible() && !g_dockDialog && !GetCapture() && IsWindowEnabled(g_hDash) && GetForegroundWindow()==g_hDash;
}
int Dashboard_HandleOutsideClick(POINT screenPoint)
{
    RECT bounds;
    if(!Dashboard_IsVisible() || g_dockDialog || !IsWindowEnabled(g_hDash) ||
       !GetWindowRect(g_hDash,&bounds) || PtInRect(&bounds,screenPoint))return 0;
    Dashboard_Close();return 1;
}
void Dashboard_InputChar(wchar_t c)
{if(Dashboard_IsVisible() && g_hSearchEdit)SendMessageW(g_hSearchEdit,WM_CHAR,c,1);}
void Dashboard_ForwardKey(DWORD vk,DWORD scan,DWORD flags)
{
    if(!Dashboard_HasFocus())return;
    LPARAM data=1|((LPARAM)scan<<16);if(flags&LLKHF_EXTENDED)data|=1u<<24;
    HWND focus=GetFocus();
    if(focus!=g_hSearchEdit && focus!=g_hDockPane){SetFocus(g_hSearchEdit);focus=g_hSearchEdit;}
    if(vk>='A' && vk<='Z' && !(GetAsyncKeyState(VK_CONTROL)&0x8000) && !(flags&LLKHF_ALTDOWN)) {
        if(focus!=g_hSearchEdit)SetFocus(g_hSearchEdit);
        int upper=((GetAsyncKeyState(VK_SHIFT)&0x8000)!=0)^((GetKeyState(VK_CAPITAL)&1)!=0);Dashboard_InputChar((wchar_t)(upper?vk:vk+('a'-'A')));
    }else if(((vk>='0' && vk<='9') || (vk>=VK_NUMPAD0 && vk<=VK_NUMPAD9)) &&
             !(GetAsyncKeyState(VK_CONTROL)&0x8000) && !(GetAsyncKeyState(VK_SHIFT)&0x8000) && !(flags&LLKHF_ALTDOWN)){
        if(focus!=g_hSearchEdit)SetFocus(g_hSearchEdit);
        Dashboard_InputChar((wchar_t)(vk>=VK_NUMPAD0?'0'+vk-VK_NUMPAD0:vk));
    }else SendMessageW(focus,WM_KEYDOWN,vk,data);
}
int Dashboard_HandlePhysicalKey(UINT message,DWORD vk,DWORD scan,DWORD flags)
{
    if(vk>=256)return 0;
    if(message==WM_KEYUP || message==WM_SYSKEYUP){int consumed=g_dashPhysicalKeys[vk];g_dashPhysicalKeys[vk]=0;return consumed;}
    if(!Dashboard_HasFocus() || (message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN))return 0;
    if(vk==VK_CONTROL || vk==VK_LCONTROL || vk==VK_RCONTROL || vk==VK_SHIFT || vk==VK_LSHIFT || vk==VK_RSHIFT || vk==VK_MENU || vk==VK_LMENU || vk==VK_RMENU || vk==VK_LWIN || vk==VK_RWIN)return 0;
    if(flags&LLKHF_ALTDOWN || (GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000)return 0;
    Dashboard_ForwardKey(vk,scan,flags);g_dashPhysicalKeys[vk]=1;return 1;
}
void Dashboard_ApplyLayout(void)
{
    if(!g_hDash)return;
    Dash_ResizeForDock(g_hDash,NULL);Dash_Layout(g_hDash);Dash_RequestIcons();RedrawWindow(g_hDash,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
