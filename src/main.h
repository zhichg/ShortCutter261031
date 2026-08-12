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

/* ── 常量 ─────────────────────────────────── */
#define APP_NAME       L"ShortCutter"
#define APP_TITLE      L"ShortCutter"
#define WM_TRAYICON    (WM_APP + 1)
#define WM_SC_MATCH    (WM_APP + 4)
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
#define MAX_DOCK_ITEMS     24
#define MAX_USAGE_HIST     20

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

/* 内置 Command ID */
#define CMDID_NONE        0
#define CMDID_INPUT      -1
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
#define ZONE_COUNT 5

/* ── 数据结构 ─────────────────────────────── */

/* Command — 独立指令 */
typedef struct {
    int     id;
    char    name[64];
    CmdType type;
    char    runPath[512];
    char    keySeq[512];
    int     locked;  /* 1=预置命令，不可删除 */
    char    group[32];
} Command;

/* GestureDef — 自定义手势 */
typedef struct {
    char    name[64];
    char    chains[3][64];
    int     chainCount;
    int     modifier;
    int     commandId;
    int     enabled;
    int     zone;  /* 0=Main 1=TL 2=TR 3=BL 4=BR */
    char    ptsStr[3][1024]; /* 采样点: "x,y;x,y;..." per sample */
} GestureDef;

/* HotkeyDef — 独立快捷键 */
typedef struct {
    BYTE    mods;
    BYTE    vk;
    char    label[32];
    char    description[64];
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
    char iconSource[512];
} DockItem;

/* 全局配置 */
typedef struct {
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
    char       toggleLabel[32];
    int        autoStart;
    int        dashboardOpacity; /* Dashboard transparency, 20-100 percent */
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
    UsageEntry usage[MAX_COMMANDS];
} AppConfig;

/* ── 全局变量声明 ─────────────────────────── */
extern AppConfig g_cfg;
extern int       g_enabled;
extern HINSTANCE g_hInst;
extern HWND      g_hWnd;
extern HMENU     g_hTrayMenu;
extern int       g_dpiScale;
extern wchar_t   g_exeDir[MAX_PATH];

/* ── 各模块接口 ───────────────────────────── */
void Config_Load(void);
void Config_Save(void);
void Config_SetDefaults(void);
void Config_AutoStart(int enable);
void Scripts_ScanAndSync(void);
void UI_Show(HWND hParent);
void Gesture_Start(void);
void Gesture_Stop(void);
void Action_ExecuteCustom(const GestureDef *g);
void Action_ExecuteCommand(int cmdId);
int  Popup_CloseIfOutside(int x, int y);
int  Popup_IsVisible(void);
int  Popup_HasFocus(void);
void Popup_ForwardKey(DWORD vkCode, DWORD scanCode, DWORD flags);
void Dashboard_Show(void);
void Dashboard_Close(void);
void Dashboard_ApplyOpacity(void);
int  Dashboard_IsVisible(void);
int  Dashboard_HasFocus(void);
void Dashboard_ForwardKey(DWORD vk, DWORD scan, DWORD flags);
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
