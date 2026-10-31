#ifndef MAIN_H
#define MAIN_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <imm.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* ── 常量 ─────────────────────────────────── */
#define APP_NAME       L"ShortCutter"
#define APP_TITLE      L"ShortCutter"
#define WM_TRAYICON    (WM_APP + 1)
#define WM_SC_MATCH    (WM_APP + 4)
#define WM_SC_DASHICONS (WM_APP + 402)
#define WM_SC_QUICKINPUT (WM_APP + 403)
#define WM_SC_ACTIONRESULT (WM_APP + 404)
#define WM_SC_FOREGROUND (WM_APP + 405)
#define WM_SC_FOREGROUNDGRANT (WM_APP + 406)
#define SC_ACTION_TIMER 0xA110
#define ID_TRAYICON    1
#define IDM_SETTINGS   1001
#define IDM_TOGGLE     1002
#define IDM_EXIT       1003
#define IDM_DASHBOARD  1004

/* 热键 ID 段 */
#define HKID_HOTKEY_BASE   1000   /* HotkeyDef 快捷键 */
#define HKID_TOGGLE        1999   /* 开关快捷键 */
#define MAX_HOTKEYS        100
#define MAX_COMMANDS       100
#define MAX_GESTURES       100
#define MAX_DOCK_ITEMS    256
#define DOCK_PAGE_SIZE     24
#define MAX_DOCK_PAGES     32
#define MAX_USAGE_HIST     20
#define SC_CONFIG_VERSION 3
#define SC_SETTINGS_MIN_WIDTH 480
#define SC_SETTINGS_MIN_HEIGHT 372
#define SC_SETTINGS_MAX_SIZE 16384
#define SC_SETTINGS_LIST_COUNT 3
#define SC_SETTINGS_COLUMN_COUNT 6
#define SC_NAME_CAP        256
#define SC_GROUP_CAP       128
#define SC_PATH_CAP        2048
#define SC_KEYS_CAP        2048
#define SC_LABEL_CAP       128
#define SC_INPUT_KEYBOARD  0x01
#define SC_INPUT_MOUSE     0x02
#define SC_INPUT_TOGGLE    0x04

/* ── 枚举 ─────────────────────────────────── */
/* 8 方向（用于方向链编码） */
typedef enum {
    DIR_UP         = 0,
    DIR_DOWN       = 1,
    DIR_LEFT       = 2,
    DIR_RIGHT      = 3,
    DIR_UP_RIGHT   = 4,
    DIR_DOWN_RIGHT = 5,
    DIR_DOWN_LEFT  = 6,
    DIR_UP_LEFT    = 7,
    DIR_NONE       = -1
} GestureDir;

/* Command 执行类型 */
typedef enum { CMD_RUN=0, CMD_KEYS=1, CMD_SCRIPT=2 } CmdType;

/* Persisted Dashboard theme IDs; old configurations default to White. */
typedef enum { DASH_THEME_WHITE=0, DASH_THEME_BLACK=1, DASH_THEME_GRAY=2,
               DASH_THEME_COUNT=3 } DashboardTheme;

typedef enum { UI_LANG_ENGLISH=0, UI_LANG_CHINESE=1, UI_LANG_COUNT=2 } UiLanguage;

/* 内置 Command ID */
#define CMDID_NONE        0
#define CMDID_SETTINGS   -2
#define CMDID_TOGGLE     -3
#define CMDID_CMDPROMPT  -4
#define CMDID_POWERSHELL -5
#define CMDID_DASHBOARD  -6

/* 修饰键位掩码 */
#define SC_MOD_CTRL  0x01
#define SC_MOD_ALT   0x02
#define SC_MOD_SHIFT 0x04
#define SC_MOD_WIN   0x08
#define SC_MOD_X1    0x10
#define SC_MOD_X2    0x20

/* 手势区域 */
#define ZONE_MAIN  0
#define ZONE_TL    1
#define ZONE_TR    2
#define ZONE_BL    3
#define ZONE_BR    4
#define ZONE_ALL   5
#define ZONE_COUNT 6

/* ── 数据结构 ─────────────────────────────── */

/* Command — 独立指令 */
typedef struct {
    int     id;
    char    name[SC_NAME_CAP];
    CmdType type;
    char    runPath[SC_PATH_CAP];
    char    keySeq[SC_KEYS_CAP];
    char    arguments[SC_PATH_CAP];
    char    workingDir[SC_PATH_CAP];
    int     activateExisting; /* legacy commands default to 1 */
    int     confirmBeforeRun;
    int     runAsAdmin;
    int     locked;  /* 1=预置命令，不可删除 */
    char    group[SC_GROUP_CAP];
} Command;

/* GestureDef — 自定义手势 */
typedef struct {
    char    name[SC_NAME_CAP];
    char    chains[3][64];
    int     chainCount;
    int     modifier;
    int     commandId;
    int     enabled;
    int     zone;  /* 0=Main 1=TL 2=TR 3=BL 4=BR 5=All areas */
    char    ptsStr[3][1024]; /* 采样点: "x,y;x,y;..." per sample */
} GestureDef;

/* HotkeyDef — 独立快捷键 */
typedef struct {
    BYTE    mods;
    BYTE    vk;
    char    label[SC_LABEL_CAP];
    char    description[SC_NAME_CAP];
    int     commandId;
    int     enabled;
} HotkeyDef;

/* UsageEntry — 命令使用频率记录 */
typedef struct {
    int  cmdId;
    int  useCount;
    char lastUsed[20];
} UsageEntry;

/* DockItem — 仪表盘自定义命令快捷方式 */
typedef struct {
    int  commandId;
    char iconSource[SC_PATH_CAP];
    int  parentFolderId; /* 0 = home; folders themselves always live at home */
    int  folderId;       /* >0 = virtual folder, 0 = command shortcut */
    char folderName[SC_NAME_CAP];
    int  pageCount;      /* only used by folders */
} DockItem;

/* 全局配置 */
typedef struct {
    int        configVersion;
    Command    commands[MAX_COMMANDS];
    int        commandCount;
    int        nextCmdId;

    GestureDef gestures[MAX_GESTURES];
    int        gestureCount;

    HotkeyDef  hotkeys[MAX_HOTKEYS];
    int        hotkeyCount;

    int        minGesture;
    int        matchThreshold;
    int        mouseButton;
    int        trailEnabled;
    int        zoneLinesEnabled;
    int        enabled;
    BYTE       toggleMods;
    BYTE       toggleVk;
    char       toggleLabel[SC_LABEL_CAP];
    int        autoStart;
    int        uiLanguage; /* UiLanguage; existing configurations keep English */
    int        dashboardOpacity; /* Dashboard transparency, 20-100 percent */
    int        dashboardTheme;   /* DashboardTheme */
    int        dashboardQuickInput; /* type letters to open; opt-in */
    int        dashboardQuickDesktop;
    int        dashboardQuickExplorer;
    int        dashboardCompact;
    int        settingsWidth;  /* normal client size in 96-DPI logical pixels */
    int        settingsHeight;
    int        settingsColumnWidths[SC_SETTINGS_LIST_COUNT][SC_SETTINGS_COLUMN_COUNT]; /* 0=default; otherwise logical width + 1 (including hidden columns) */
    /* 外观自定义 */
    int        zoneLineColor;   /* 区域线颜色，默认绿 RGB(100,220,100) */
    int        zoneLineWidth;   /* 区域线宽，默认 2，范围 1-5 */
    int        trailGlowColor;  /* 轨迹外发光，默认蓝 RGB(0,120,220) */
    int        trailGlowWidth;  /* 外发光扩散量，默认 5（实际笔宽=Core+此值） */
    int        trailInnerColor; /* 轨迹内芯色，默认浅蓝 RGB(180,220,255) */
    int        trailInnerWidth; /* 轨迹内芯宽，默认 3，范围 1-8 */

    /* 仪表盘 */
    DockItem   dockItems[MAX_DOCK_ITEMS];
    int        dockCount;
    int        dockPageCount;
    UsageEntry usage[MAX_COMMANDS];
} AppConfig;

/* ── 全局变量声明 ─────────────────────────── */
extern AppConfig g_cfg;
extern int       g_enabled;
extern HINSTANCE g_hInst;
extern HWND      g_hWnd;
extern HMENU     g_hTrayMenu;
extern int       g_dpiScale;
extern wchar_t   g_exeDir[SC_PATH_CAP];

static inline const wchar_t *SC_Text(const wchar_t *english, const wchar_t *chinese)
{
    return g_cfg.uiLanguage == UI_LANG_CHINESE ? chinese : english;
}

/* Checked conversions never leave partial, unterminated output behind. */
static inline BOOL SC_Utf8FromWide(char *dst, size_t capacity, const wchar_t *src)
{
    if (!dst || !capacity) return FALSE;
    dst[0] = 0;
    if (!src) return FALSE;
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || (size_t)needed > capacity) return FALSE;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src, -1, dst, (int)capacity, NULL, NULL))
        { dst[0] = 0; return FALSE; }
    return TRUE;
}

static inline BOOL SC_WideFromUtf8(wchar_t *dst, size_t capacity, const char *src)
{
    if (!dst || !capacity) return FALSE;
    dst[0] = 0;
    if (!src) return FALSE;
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
    if (needed <= 0 || (size_t)needed > capacity) return FALSE;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, (int)capacity))
        { dst[0] = 0; return FALSE; }
    return TRUE;
}

static inline BOOL SC_CopyUtf8(char *dst, size_t capacity, const char *src)
{
    if (!dst || !capacity) return FALSE;
    if (!src) { dst[0] = 0; return FALSE; }
    size_t length = strlen(src);
    if (length >= capacity || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0))
        { dst[0] = 0; return FALSE; }
    memmove(dst, src, length + 1);
    return TRUE;
}

/* ── 各模块接口 ───────────────────────────── */
void Config_Load(void);
BOOL Config_Save(void);
DWORD Config_GetLastError(void);
const wchar_t *Config_GetLastErrorText(void);
void Config_Validate(AppConfig *config);
void Config_UnbindCommand(AppConfig *config, int commandId);
int  Config_AllocateCommandId(AppConfig *config);
void Config_SetDefaults(void);
BOOL Config_AutoStart(int enable);
void Scripts_ScanAndSync(void);
void UI_Show(HWND hParent);
BOOL UI_ProcessDialogMessage(MSG *message);
int UI_GetLanguage(void);
int UI_IsCapturingInput(void);
void Tray_ApplyLanguage(void);
void SC_SetEnabled(int enabled);
DWORD SC_GetInputStatus(void);
const wchar_t *SC_GetInputStatusText(void);
void SC_ReportError(const wchar_t *operation, DWORD error);
void SC_LogDiagnostic(const wchar_t *message);
void Gesture_Start(void);
void Gesture_Stop(void);
int  Gesture_IsActive(void);
void Gesture_Cancel(void);
void Gesture_DiscardPendingEvents(void);
void Gesture_HandleMatch(WPARAM wp, LPARAM lp);
void Gesture_OnDisplayChange(void);
int  Gesture_GetHookStatus(void);
void Action_ExecuteCustom(const GestureDef *g);
void Action_ExecuteCommand(int cmdId);
int  Action_ValidateKeySeq(const char *seq, wchar_t *error, size_t errorCount);
void Action_FormatKeySeq(const char *seq, wchar_t *out, size_t capacity);
void Action_Tick(void);
void Action_Cancel(void);
void Action_Shutdown(void);
void Action_HandleResult(LPARAM result);
void Action_NoteUserInput(void);
LRESULT Action_HandleForeground(UINT message, WPARAM target, LPARAM generation);
int  Action_HandleForegroundHotkey(WPARAM id, LPARAM key);
void Dashboard_Show(void);
void Dashboard_Init(void);
void Dashboard_Shutdown(void);
void Dashboard_AcceptIcons(void);
void Dashboard_Close(void);
void Dashboard_ApplyOpacity(void);
void Dashboard_ApplyTheme(void);
void Dashboard_ApplyLanguage(void);
void Dashboard_ApplyLayout(void);
int  Dashboard_IsVisible(void);
int  Dashboard_HasFocus(void);
void Dashboard_ForwardKey(DWORD vk, DWORD scan, DWORD flags);
void Dashboard_InputChar(wchar_t character);
int  Dashboard_HandlePhysicalKey(UINT message, DWORD vkCode, DWORD scanCode, DWORD flags);
int  Dashboard_HandleOutsideClick(POINT screenPoint);
int  QuickInput_HandlePending(UINT msg, DWORD vk, DWORD scan, DWORD flags);
int  QuickInput_TryStart(DWORD vk, DWORD scan, DWORD flags, BYTE mods);
void QuickInput_Dispatch(void);
void QuickInput_Cancel(void);
void Dashboard_RecordUsage(int cmdId);
int  Dashboard_GetHistCount(void);
const char* Dashboard_GetHistItem(int idx);
void Dashboard_ClearHist(void);
void Dashboard_AddHistItem(const char *name);
int  Dashboard_GetDockPos(int idx);
void Dashboard_SetDockPos(int idx, int pos);
void Hotkeys_Register(void);
void Hotkeys_Unregister(void);
int  Gesture_Downsample(const POINT *src, int srcCount,
                        POINT *dst, int dstMax, int minDist);
int  Gesture_EncodeChain(const POINT *pts, int count,
                         char *chain, int chainMax);
int  Gesture_Match(const char *chain, int chainLen, int modifier, int zone);
int  Gesture_GetZone(int x, int y, int screenW, int screenH);
extern char g_pendChain[64];

int  CmpCmd(const void *a, const void *b);
int  CmpGes(const void *a, const void *b);
int  CmpHk(const void *a, const void *b);
const wchar_t *GetExeDir(void);
const wchar_t *GetIniPath(void);

#endif /* MAIN_H */
