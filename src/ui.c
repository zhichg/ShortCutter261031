#include "main.h"
#include <stdlib.h>
#include <uxtheme.h>

/* ── 设置窗口尺寸 ─────────────────────────── */
#define SETS_W  480
#define SETS_H  340
#define SCALE(v) MulDiv((v), g_dpiScale, 96)

/* ── 控件 ID ──────────────────────────────── */
#define CID_TAB         2000
#define CID_CMD_LIST    2100
#define CID_CMD_NEW     2101
#define CID_CMD_EDIT    2102
#define CID_CMD_DEL     2103
#define CID_CMD_SCAN    2104
#define CID_GES_LIST    2200
#define CID_GES_NEW     2201
#define CID_GES_EDIT    2202
#define CID_GES_DEL     2203
#define CID_HK_LIST     2300
#define CID_HK_NEW      2301
#define CID_HK_EDIT     2302
#define CID_HK_DEL      2303
#define CID_TOGGLE_HK   2700
#define CID_MOUSE_BTN   3001
#define CID_AUTOSTART   2550
#define CID_BTN_SAVE    4000
#define CID_STATUS      5000

/* 外观自定义控件 ID */
#define CID_ZONE_COLOR   3020
#define CID_ZONE_WIDTH   3021
#define CID_GLOW_COLOR   3022
#define CID_GLOW_WIDTH   3023
#define CID_INNER_COLOR  3024
#define CID_INNER_WIDTH  3025
#define CID_DASH_OPACITY 3033
#define CID_DASH_OPACITY_VALUE 3034

/* 弹窗 ID */
#define CID_CMD_DLG_NAME  5100
#define CID_CMD_DLG_TYPE  5101
#define CID_CMD_DLG_PATH  5102
#define CID_CMD_DLG_KEYS  5103
#define CID_CMD_DLG_BROWSE 5104
#define CID_CMD_DLG_GROUP  5105
#define CID_REC_CANVAS    5200
#define CID_REC_CHAIN     5201
#define CID_REC_NAME      5202
#define CID_REC_MOD       5203
#define CID_REC_ENABLED   5205
#define CID_REC_ZONE      5206
#define CID_HK_DLG_HK     5300
#define CID_HK_DLG_DESC   5301
#define CID_HK_DLG_EN     5303

#define WM_SC_REFRESH    (WM_APP + 200)
#define WM_SC_DEFER_SAVE (WM_APP + 201)

/* ── 全局 ──────────────────────────────────── */
static HWND g_hDlg, g_hTab;
static HWND g_hCmdList, g_hGesList, g_hHkList;
static HWND g_hToggleHK, g_hMouseBtn, g_hStatus;
static HWND g_hDashOpacity, g_hDashOpacityValue;
static HFONT g_hFont;
static HBRUSH g_hWhiteBr = NULL;
static int  g_activeTab = 0;
static COLORREF g_zoneColor, g_glowColor, g_innerColor;

static BOOL UseWhiteBg(int cid) /* 白底控件 ID */
{
    if (cid >= 3000 && cid <= 3099) return TRUE; /* General tab */
    if (cid == 2550 || cid == 2700) return TRUE;
    if (cid == 5005 || cid == 5009) return TRUE; /* Commands: Type, Group */
    if (cid == 5006) return TRUE;                 /* Gestures: Zone */
    return FALSE;
}


/* ── 链方向名称 ────────────────────────────── */
static const char *g_dirNames[8] = {"Up","Dn","Lt","Rt","UR","DR","DL","UL"};

static void ChainToDisplay(const char *raw, char *buf, int bufSize)
{
    buf[0] = 0;
    for (int i = 0; raw[i] && (int)strlen(buf) < bufSize - 8; i++) {
        if (i > 0) strcat(buf, " ");
        int d = raw[i] - '0';
        if (d >= 0 && d <= 7) strcat(buf, g_dirNames[d]);
    }
}

/* ── 修饰键文字 ────────────────────────────── */
static const wchar_t *ModToStr(int mod)
{
    switch (mod) {
        case 0: return L"-";
        case 1: return L"Ctrl";
        case 2: return L"Shift";
        case 4: return L"Alt";
        case 8: return L"Win";
        case 3: return L"Ctrl+Shift";
        case 5: return L"Ctrl+Alt";
        case 6: return L"Alt+Shift";
        case 0x10: return L"X1";
        case 0x20: return L"X2";
        default: return L"?";
    }
}

/* ── 命令名 ↔ ID ───────────────────────────── */
static int CmdNameToId(const char *name)
{
    if (!name || !name[0]) return CMDID_NONE;
    if (lstrcmpiA(name, "input") == 0 || lstrcmpiA(name, "popup") == 0) return CMDID_INPUT;
    if (lstrcmpiA(name, "settings") == 0 || lstrcmpiA(name, "sc") == 0) return CMDID_SETTINGS;
    if (lstrcmpiA(name, "toggle") == 0) return CMDID_TOGGLE;
    if (lstrcmpiA(name, "cmd") == 0 || lstrcmpiA(name, "cmdprompt") == 0) return CMDID_CMDPROMPT;
    if (lstrcmpiA(name, "ps") == 0 || lstrcmpiA(name, "powershell") == 0) return CMDID_POWERSHELL;
    if (lstrcmpiA(name, "dashboard") == 0 || lstrcmpiA(name, "db") == 0) return CMDID_DASHBOARD;
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (lstrcmpiA(g_cfg.commands[i].name, name) == 0)
            return g_cfg.commands[i].id;
    return CMDID_NONE;
}

static void CmdIdToName(int cmdId, char *out, int outMax)
{
    out[0] = 0;
    if (cmdId == CMDID_NONE) return;
    if (cmdId == CMDID_INPUT) { strcpy(out, "Input"); return; }
    if (cmdId == CMDID_SETTINGS) { strcpy(out, "Settings"); return; }
    if (cmdId == CMDID_TOGGLE) { strcpy(out, "Toggle"); return; }
    if (cmdId == CMDID_CMDPROMPT) { strcpy(out, "Cmd"); return; }
    if (cmdId == CMDID_POWERSHELL) { strcpy(out, "PS"); return; }
    if (cmdId == CMDID_DASHBOARD) { strcpy(out, "Dashboard"); return; }
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (g_cfg.commands[i].id == cmdId)
            { strncpy(out, g_cfg.commands[i].name, outMax-1); return; }
}

/* ══════════ 列表填充函数 ══════════════════════ */
static int g_cmdFilter = 0; /* 0=All 1=Custom 2=Built-in 3=Scripts */
static int g_cmdGrpFilter = 0; /* 0=All, 1+ = index into g_cmdGrpNames */
static char g_cmdGrpNames[50][32]; /* 收集的所有 group 名 */
static int  g_cmdGrpCount = 0;
static int g_gesFilter = 0; /* 0=All 1=Main 2=TL 3=TR 4=BL 5=BR */

static const wchar_t *ZoneName(int zone)
{
    static const wchar_t *zn[ZONE_COUNT] = { L"Main", L"↖ TL", L"↗ TR", L"↙ BL", L"↘ BR" };
    if (zone < 0 || zone >= ZONE_COUNT) return L"Main";
    return zn[zone];
}

/* 根据当前筛选，将 ListView 行号映射回 commands[] 索引 */
static int CmdListRowToIdx(int row)
{
    int cnt = 0;
    for (int i = 0; i < g_cfg.commandCount; i++) {
        Command *c = &g_cfg.commands[i];
        if (g_cmdFilter == 1 && (c->locked || c->type == CMD_SCRIPT)) continue;
        if (g_cmdFilter == 2 && (!c->locked || c->type == CMD_SCRIPT)) continue;
        if (g_cmdFilter == 3 && c->type != CMD_SCRIPT) continue;
        if (g_cmdGrpFilter > 0 && lstrcmpiA(c->group, g_cmdGrpNames[g_cmdGrpFilter-1]) != 0) continue;
        if (cnt == row) return i;
        cnt++;
    }
    return -1;
}

static void CmdList_Fill(void)
{
    SendMessageW(g_hCmdList, LVM_DELETEALLITEMS, 0, 0);
    /* 收集所有 group 名（用于 Group 筛选下拉） */
    g_cmdGrpCount = 0;
    for (int i = 0; i < g_cfg.commandCount && g_cmdGrpCount < 50; i++) {
        if (!g_cfg.commands[i].group[0]) continue;
        int dup = 0;
        for (int j = 0; j < g_cmdGrpCount; j++)
            if (lstrcmpiA(g_cmdGrpNames[j], g_cfg.commands[i].group) == 0) { dup = 1; break; }
        if (!dup) strncpy(g_cmdGrpNames[g_cmdGrpCount++], g_cfg.commands[i].group, 31);
    }
    /* 更新 Group 筛选下拉 */
    { HWND hGrp = GetDlgItem(GetParent(g_hCmdList), 5008);
      if (hGrp) {
        int cur = (int)SendMessageW(hGrp, CB_GETCURSEL, 0, 0);
        SendMessageW(hGrp, CB_RESETCONTENT, 0, 0);
        SendMessageW(hGrp, CB_ADDSTRING, 0, (LPARAM)L"All");
        for (int i = 0; i < g_cmdGrpCount; i++) {
            wchar_t wg[32]; MultiByteToWideChar(CP_UTF8, 0, g_cmdGrpNames[i], -1, wg, 32);
            SendMessageW(hGrp, CB_ADDSTRING, 0, (LPARAM)wg);
        }
        if (cur >= 0 && cur <= g_cmdGrpCount) SendMessageW(hGrp, CB_SETCURSEL, (WPARAM)cur, 0);
        else SendMessageW(hGrp, CB_SETCURSEL, 0, 0);
      } }

    LVITEMW item = {0}; item.mask = LVIF_TEXT;
    int row = 0;
    for (int i = 0; i < g_cfg.commandCount; i++) {
        Command *c = &g_cfg.commands[i];
        if (g_cmdFilter == 1 && (c->locked || c->type == CMD_SCRIPT)) continue;
        if (g_cmdFilter == 2 && (!c->locked || c->type == CMD_SCRIPT)) continue;
        if (g_cmdFilter == 3 && c->type != CMD_SCRIPT) continue;
        if (g_cmdGrpFilter > 0 && g_cmdGrpFilter <= g_cmdGrpCount &&
            lstrcmpiA(c->group, g_cmdGrpNames[g_cmdGrpFilter-1]) != 0) continue;

        wchar_t wName[64], wWay[16], wGroup[32], wTarget[512];
        { char na[64];
          if (c->locked && c->type != CMD_SCRIPT) { na[0]='*'; strncpy(na+1, c->name, 62); }
          else strncpy(na, c->name, 63);
          MultiByteToWideChar(CP_UTF8, 0, na, -1, wName, 64); }
        wcscpy(wWay, c->type == CMD_KEYS ? L"Keys" :
                     c->type == CMD_SCRIPT ? L"Script" : L"Run");
        MultiByteToWideChar(CP_UTF8, 0, c->group[0] ? c->group : "None", -1, wGroup, 32);
        MultiByteToWideChar(CP_UTF8, 0,
            c->type == CMD_KEYS ? c->keySeq : c->runPath, -1, wTarget, 512);

        item.iItem = row; item.iSubItem = 0; item.pszText = wName;
        SendMessageW(g_hCmdList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1; item.pszText = wWay;
        SendMessageW(g_hCmdList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 2; item.pszText = wGroup;
        SendMessageW(g_hCmdList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 3; item.pszText = wTarget;
        SendMessageW(g_hCmdList, LVM_SETITEMW, 0, (LPARAM)&item);
        row++;
    }
}

static int GesListRowToIdx(int row)
{
    int cnt = 0;
    for (int i = 0; i < g_cfg.gestureCount; i++) {
        if (g_gesFilter != 0 && g_cfg.gestures[i].zone != g_gesFilter - 1) continue;
        if (cnt == row) return i;
        cnt++;
    }
    return -1;
}

static void GesList_Fill(void)
{
    SendMessageW(g_hGesList, LVM_DELETEALLITEMS, 0, 0);
    LVITEMW item = {0}; item.mask = LVIF_TEXT;
    int row = 0;
    for (int i = 0; i < g_cfg.gestureCount; i++) {
        GestureDef *g = &g_cfg.gestures[i];
        /* Zone 筛选 */
        if (g_gesFilter != 0 && g->zone != g_gesFilter - 1) continue;

        wchar_t wName[64], wChain[128], wMod[32], wCmd[128], wZone[16];
        MultiByteToWideChar(CP_UTF8, 0, g->name, -1, wName, 64);
        char buf[128]; ChainToDisplay(g->chains[0], buf, 127);
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wChain, 128);
        wcscpy(wMod, ModToStr(g->modifier));
        wcscpy(wZone, ZoneName(g->zone));
        /* 查找绑定 Command 名 */
        { char cn[64]; CmdIdToName(g->commandId, cn, 63);
          MultiByteToWideChar(CP_UTF8, 0, cn, -1, wCmd, 128); }

        wchar_t wOn[4] = {g->enabled ? L'Y' : L'N', 0};
        item.iItem = row; item.iSubItem = 0; item.pszText = wName;
        SendMessageW(g_hGesList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1; item.pszText = wChain;
        SendMessageW(g_hGesList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 2; item.pszText = wMod;
        SendMessageW(g_hGesList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 3; item.pszText = wZone;
        SendMessageW(g_hGesList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 4; item.pszText = wCmd;
        SendMessageW(g_hGesList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 5; item.pszText = wOn;
        SendMessageW(g_hGesList, LVM_SETITEMW, 0, (LPARAM)&item);
        row++;
    }
}

static void HkList_Fill(void)
{
    SendMessageW(g_hHkList, LVM_DELETEALLITEMS, 0, 0);
    LVITEMW item = {0}; item.mask = LVIF_TEXT;
    for (int i = 0; i < g_cfg.hotkeyCount; i++) {
        HotkeyDef *h = &g_cfg.hotkeys[i];
        wchar_t wLabel[64], wDesc[128], wCmd[128];
        MultiByteToWideChar(CP_UTF8, 0, h->label, -1, wLabel, 64);
        if (!wLabel[0]) wcscpy(wLabel, L"(unbound)");
        MultiByteToWideChar(CP_UTF8, 0, h->description, -1, wDesc, 128);
        { char cn[64]; CmdIdToName(h->commandId, cn, 63);
          MultiByteToWideChar(CP_UTF8, 0, cn, -1, wCmd, 128); }
        if (!wCmd[0]) wcscpy(wCmd, L"(none)");

        wchar_t wOn[4] = {h->enabled ? L'Y' : L'N', 0};
        item.iItem = i; item.iSubItem = 0; item.pszText = wLabel;
        SendMessageW(g_hHkList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1; item.pszText = wDesc;
        SendMessageW(g_hHkList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 2; item.pszText = wCmd;
        SendMessageW(g_hHkList, LVM_SETITEMW, 0, (LPARAM)&item);
        item.iSubItem = 3; item.pszText = wOn;
        SendMessageW(g_hHkList, LVM_SETITEMW, 0, (LPARAM)&item);
    }
}

/* ══════════ Command 编辑弹窗 ══════════════════ */
static HWND g_hCmdDlg = NULL, g_hCmdDlgName, g_hCmdDlgType, g_hCmdDlgPath, g_hCmdDlgKeys, g_hCmdDlgGroup;
static int  g_cmdEditIdx = -1;

static void CmdDlg_UpdateFields(void)
{
    int t = (int)SendMessageW(g_hCmdDlgType, CB_GETCURSEL, 0, 0);
    ShowWindow(g_hCmdDlgPath, t == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hCmdDlg, CID_CMD_DLG_BROWSE), t == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hCmdDlgKeys, t == 1 ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK CmdDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontW(SCALE(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(15),SCALE(50),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hCmdDlgName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(70),SCALE(13),SCALE(250),SCALE(22), hWnd, (HMENU)CID_CMD_DLG_NAME, g_hInst, NULL);
        SendMessageW(g_hCmdDlgName, WM_SETFONT, (WPARAM)hf, TRUE);
        /* Way */
        CreateWindowExW(0, L"STATIC", L"Way:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(45),SCALE(50),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hCmdDlgType = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            SCALE(70),SCALE(43),SCALE(65),SCALE(120), hWnd, (HMENU)CID_CMD_DLG_TYPE, g_hInst, NULL);
        SendMessageW(g_hCmdDlgType, WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(g_hCmdDlgType, CB_ADDSTRING, 0, (LPARAM)L"Run");
        SendMessageW(g_hCmdDlgType, CB_ADDSTRING, 0, (LPARAM)L"Keys");
        /* Group (可编辑下拉) */
        CreateWindowExW(0, L"STATIC", L"Group:", WS_CHILD|WS_VISIBLE,
            SCALE(142),SCALE(45),SCALE(40),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hCmdDlgGroup = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,
            SCALE(185),SCALE(43),SCALE(135),SCALE(120), hWnd, (HMENU)CID_CMD_DLG_GROUP, g_hInst, NULL);
        SendMessageW(g_hCmdDlgGroup, WM_SETFONT, (WPARAM)hf, TRUE);
        /* 填充已有 group 名 */
        for (int i = 0; i < g_cfg.commandCount; i++) {
            if (!g_cfg.commands[i].group[0]) continue;
            wchar_t wg[32]; MultiByteToWideChar(CP_UTF8, 0, g_cfg.commands[i].group, -1, wg, 32);
            int dup = 0;
            for (int j = 0; j < SendMessageW(g_hCmdDlgGroup, CB_GETCOUNT, 0, 0); j++)
                { wchar_t ex[32]; SendMessageW(g_hCmdDlgGroup, CB_GETLBTEXT, j, (LPARAM)ex);
                  if (lstrcmpW(ex, wg) == 0) { dup = 1; break; } }
            if (!dup) SendMessageW(g_hCmdDlgGroup, CB_ADDSTRING, 0, (LPARAM)wg);
        }
        /* Path */
        CreateWindowExW(0, L"STATIC", L"Path:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(78),SCALE(50),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hCmdDlgPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(70),SCALE(76),SCALE(180),SCALE(22), hWnd, (HMENU)CID_CMD_DLG_PATH, g_hInst, NULL);
        SendMessageW(g_hCmdDlgPath, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"BUTTON", L"Browse", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(258),SCALE(75),SCALE(70),SCALE(24), hWnd, (HMENU)CID_CMD_DLG_BROWSE, g_hInst, NULL);
        g_hCmdDlgKeys = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_MULTILINE,
            SCALE(15),SCALE(78),SCALE(315),SCALE(40), hWnd, (HMENU)CID_CMD_DLG_KEYS, g_hInst, NULL);
        SendMessageW(g_hCmdDlgKeys, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(100),SCALE(120),SCALE(80),SCALE(26), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(190),SCALE(120),SCALE(80),SCALE(26), hWnd, (HMENU)102, g_hInst, NULL);
        if (g_cmdEditIdx >= 0 && g_cmdEditIdx < g_cfg.commandCount) {
            Command *c = &g_cfg.commands[g_cmdEditIdx];
            { wchar_t wb[512];
              MultiByteToWideChar(CP_UTF8, 0, c->name, -1, wb, 512);
              SetWindowTextW(g_hCmdDlgName, wb); }
            SendMessageW(g_hCmdDlgType, CB_SETCURSEL, (WPARAM)c->type, 0);
            { wchar_t wb[512];
              MultiByteToWideChar(CP_UTF8, 0, c->runPath, -1, wb, 512);
              SetWindowTextW(g_hCmdDlgPath, wb); }
            { wchar_t wb[512];
              MultiByteToWideChar(CP_UTF8, 0, c->keySeq, -1, wb, 512);
              SetWindowTextW(g_hCmdDlgKeys, wb); }
            { wchar_t wb[64];
              MultiByteToWideChar(CP_UTF8, 0, c->group[0] ? c->group : "None", -1, wb, 64);
              SetWindowTextW(g_hCmdDlgGroup, wb); }
            /* 锁定命令（预置/脚本）禁改类型和路径，Group 始终可编辑 */
            if (c->locked) {
                EnableWindow(g_hCmdDlgType, FALSE);
                EnableWindow(g_hCmdDlgPath, FALSE);
                EnableWindow(g_hCmdDlgKeys, FALSE);
                EnableWindow(GetDlgItem(hWnd, CID_CMD_DLG_BROWSE), FALSE);
            }
        } else { SendMessageW(g_hCmdDlgType, CB_SETCURSEL, 0, 0);
            SetWindowTextW(g_hCmdDlgGroup, L"None"); }
        CmdDlg_UpdateFields();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == CID_CMD_DLG_TYPE && HIWORD(wp) == CBN_SELCHANGE)
            { CmdDlg_UpdateFields(); return 0; }
        if (LOWORD(wp) == CID_CMD_DLG_BROWSE) {
            wchar_t file[MAX_PATH] = L"";
            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"All Files\0*.*\0\0";
            ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_hCmdDlgPath, file);
            return 0;
        }
        if (LOWORD(wp) == 101) {
            wchar_t wb[512];
            char name[64] = ""; GetWindowTextW(g_hCmdDlgName, wb, 63);
            WideCharToMultiByte(CP_UTF8, 0, wb, -1, name, 64, NULL, NULL);
            if (!name[0]) { MessageBoxW(hWnd, L"Enter a name.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
            int idx = g_cmdEditIdx;
            if (idx < 0) {
                if (g_cfg.commandCount >= MAX_COMMANDS)
                    { MessageBoxW(hWnd, L"Max commands reached.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
                idx = g_cfg.commandCount; g_cfg.commandCount++;
            }
            Command *c = &g_cfg.commands[idx];
            if (idx >= g_cfg.commandCount - 1 && g_cmdEditIdx < 0) {
                c->id = g_cfg.nextCmdId++;
                c->locked = 0;
                c->keySeq[0] = 0;
                c->runPath[0] = 0;
            }
            /* 脚本改名 → 同步重命名文件 */
            if (c->type == CMD_SCRIPT && lstrcmpiA(c->name, name) != 0) {
                wchar_t wOldPath[512], wNewPath[512], wNewName[128];
                MultiByteToWideChar(CP_UTF8, 0, c->runPath, -1, wOldPath, 512);
                /* 提取旧扩展名 */
                wchar_t *ext = wcsrchr(wOldPath, L'.');
                /* 构造新路径：同目录 + 新名字 + 旧扩展名 */
                wcscpy(wNewPath, wOldPath);
                wchar_t *slash = wcsrchr(wNewPath, L'\\');
                if (slash) *(slash+1) = 0; else wNewPath[0] = 0;
                MultiByteToWideChar(CP_UTF8, 0, name, -1, wNewName, 128);
                wcscat(wNewPath, wNewName);
                if (ext) wcscat(wNewPath, ext);
                /* 目标文件已存在则拒绝 */
                if (GetFileAttributesW(wNewPath) != INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(hWnd, L"A script with this name already exists.", L"ShortCutter", MB_OK|MB_ICONWARNING);
                    return 0;
                }
                if (!MoveFileW(wOldPath, wNewPath)) {
                    MessageBoxW(hWnd, L"Failed to rename script file.", L"ShortCutter", MB_OK|MB_ICONWARNING);
                    return 0;
                }
                /* 更新 runPath */
                WideCharToMultiByte(CP_UTF8, 0, wNewPath, -1, c->runPath, 512, NULL, NULL);
            }
            strncpy(c->name, name, 63);
            { char grp[32] = "None"; GetWindowTextW(g_hCmdDlgGroup, wb, 31);
              WideCharToMultiByte(CP_UTF8, 0, wb, -1, grp, 32, NULL, NULL);
              /* 非锁定命令不能使用 Built-in / Scripts */
              if (!c->locked && (_strnicmp(grp, "Built-in", 8) == 0 || lstrcmpiA(grp, "Scripts") == 0))
                  strncpy(grp, "None", 31);
              strncpy(c->group, grp, 31); }
            /* 锁定命令只改名字和 Group */
            if (!c->locked) {
                c->type = (CmdType)SendMessageW(g_hCmdDlgType, CB_GETCURSEL, 0, 0);
                GetWindowTextW(g_hCmdDlgPath, wb, 511);
                WideCharToMultiByte(CP_UTF8, 0, wb, -1, c->runPath, 512, NULL, NULL);
                GetWindowTextW(g_hCmdDlgKeys, wb, 511);
                WideCharToMultiByte(CP_UTF8, 0, wb, -1, c->keySeq, 512, NULL, NULL);
            }
            PostMessageW(GetParent(hWnd), WM_SC_REFRESH, 0, 0);
            DestroyWindow(hWnd); return 0;
        }
        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_hCmdDlg = NULL; return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void CmdDlg_Show(HWND hParent, int idx)
{
    if (g_hCmdDlg) { SetForegroundWindow(g_hCmdDlg); return; }
    g_cmdEditIdx = idx;
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = CmdDlgProc;
        wc.hInstance = g_hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"SC_CmdDlg"; RegisterClassExW(&wc); reg = TRUE;
    }
    int w = SCALE(340), h = SCALE(180);
    g_hCmdDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_CmdDlg",
        idx < 0 ? L"New Command" : L"Edit Command",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
}

/* ══════════ Gesture 录制弹窗 ══════════════════ */
static int    g_recording = 0;
static POINT  g_recPts[500];
static int    g_recPtCount = 0;
/* 多次采样 */
static POINT  g_recSamples[3][500];
static int    g_recSampleCounts[3];
static char   g_recSampleChains[3][64];
static int    g_recSampleChainLens[3];
static int    g_recTotalSamples = 0;
static HWND   g_hRecChain = NULL, g_hRecSampLabel = NULL;
static HWND   g_hRecDlg = NULL, g_hRecCanvas, g_hRecName, g_hRecMod, g_hRecCmdEdit, g_hRecZone;
static int    g_recEditIdx = -1;

static void RecDlg_UpdateChain(void)
{
    if (!g_hRecChain) return;
    POINT ds[500];
    int minX=g_recPts[0].x, maxX=g_recPts[0].x, minY=g_recPts[0].y, maxY=g_recPts[0].y;
    for (int i=1; i<g_recPtCount; i++) {
        if (g_recPts[i].x < minX) minX=g_recPts[i].x;
        if (g_recPts[i].x > maxX) maxX=g_recPts[i].x;
        if (g_recPts[i].y < minY) minY=g_recPts[i].y;
        if (g_recPts[i].y > maxY) maxY=g_recPts[i].y;
    }
    int box = (maxX-minX) > (maxY-minY) ? (maxX-minX) : (maxY-minY);
    int dist = box / 20; if (dist<5) dist=5; if (dist>50) dist=50;
    int dsCnt = Gesture_Downsample(g_recPts, g_recPtCount, ds, 500, dist);
    int cl = Gesture_EncodeChain(ds, dsCnt, g_recSampleChains[g_recTotalSamples], 63);
    g_recSampleChainLens[g_recTotalSamples] = cl;
    /* 拷贝点数据 */
    g_recSampleCounts[g_recTotalSamples] = g_recPtCount;
    memcpy(g_recSamples[g_recTotalSamples], g_recPts, sizeof(POINT)*g_recPtCount);
    g_recTotalSamples++;
    /* 显示所有链 */
    char buf[256] = ""; int remain = 255;
    for (int s = 0; s < g_recTotalSamples; s++) {
        char one[80]; ChainToDisplay(g_recSampleChains[s], one, 79);
        int n = (int)strlen(buf);
        if (s > 0 && n < 250) { strncat(buf, "  |  ", remain); remain -= 5; }
        if (remain > 5) { strncat(buf, one, remain); remain -= (int)strlen(one); }
    }
    SetWindowTextA(g_hRecChain, buf);
    /* 更新采样计数 */
    if (g_hRecSampLabel) {
        wchar_t wb[32]; swprintf(wb, 32, L"Sample: %d/3", g_recTotalSamples);
        SetWindowTextW(g_hRecSampLabel, wb);
    }
}

static LRESULT CALLBACK RecCanvasProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_RBUTTONDOWN:
        if (g_recTotalSamples >= 3) return 0;
        g_recording = 1; g_recPtCount = 0;
        { POINT pt = { LOWORD(lp), HIWORD(lp) }; g_recPts[g_recPtCount++] = pt; }
        SetCapture(hWnd); InvalidateRect(hWnd, NULL, TRUE); return 0;
    case WM_MOUSEMOVE:
        if (g_recording && g_recPtCount < 500) {
            POINT pt = { LOWORD(lp), HIWORD(lp) };
            int dx = pt.x - g_recPts[g_recPtCount-1].x;
            int dy = pt.y - g_recPts[g_recPtCount-1].y;
            if (dx*dx + dy*dy >= 16) g_recPts[g_recPtCount++] = pt;
            InvalidateRect(hWnd, NULL, FALSE);
        } return 0;
    case WM_RBUTTONUP:
        if (g_recording) { g_recording = 0; ReleaseCapture(); RecDlg_UpdateChain(); InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        /* 已存储的采样（灰色细线） */
        for (int s = 0; s < g_recTotalSamples; s++) {
            if (g_recSampleCounts[s] > 1) {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(180,180,180));
                HPEN old = SelectObject(hdc, pen);
                MoveToEx(hdc, g_recSamples[s][0].x, g_recSamples[s][0].y, NULL);
                for (int i = 1; i < g_recSampleCounts[s]; i++)
                    LineTo(hdc, g_recSamples[s][i].x, g_recSamples[s][i].y);
                SelectObject(hdc, old); DeleteObject(pen);
            }
        }
        /* 当前录制（蓝色粗线） */
        if (g_recPtCount > 1) {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0,100,200));
            HPEN old = SelectObject(hdc, pen);
            MoveToEx(hdc, g_recPts[0].x, g_recPts[0].y, NULL);
            for (int i = 1; i < g_recPtCount; i++) LineTo(hdc, g_recPts[i].x, g_recPts[i].y);
            SelectObject(hdc, old); DeleteObject(pen);
        }
        /* 起点标记 */
        if (g_recPtCount > 0) {
            HPEN rp = CreatePen(PS_SOLID, 1, RGB(200,50,50));
            HBRUSH rb = CreateSolidBrush(RGB(200,50,50));
            SelectObject(hdc, rp); SelectObject(hdc, rb);
            Ellipse(hdc, g_recPts[0].x-3, g_recPts[0].y-3, g_recPts[0].x+3, g_recPts[0].y+3);
            DeleteObject(rb); DeleteObject(rp);
        } else if (g_recTotalSamples > 0) {
            /* 不在录制中时显示最后一个采样的起点 */
            int ls = g_recTotalSamples - 1;
            if (g_recSampleCounts[ls] > 0) {
                HPEN rp = CreatePen(PS_SOLID, 1, RGB(200,50,50));
                HBRUSH rb = CreateSolidBrush(RGB(200,50,50));
                SelectObject(hdc, rp); SelectObject(hdc, rb);
                Ellipse(hdc, g_recSamples[ls][0].x-3, g_recSamples[ls][0].y-3,
                        g_recSamples[ls][0].x+3, g_recSamples[ls][0].y+3);
                DeleteObject(rb); DeleteObject(rp);
            }
        }
        EndPaint(hWnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK RecDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontW(SCALE(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,L"Segoe UI");
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        { static BOOL cvReg = FALSE;
          if (!cvReg) { WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=RecCanvasProc;
              wc.hInstance=g_hInst; wc.hCursor=LoadCursor(NULL,IDC_CROSS);
              wc.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
              wc.lpszClassName=L"SC_RecCanvas"; RegisterClassExW(&wc); cvReg=TRUE; }
          g_hRecCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, L"SC_RecCanvas", L"",
              WS_CHILD|WS_VISIBLE,
              SCALE(12),SCALE(10),SCALE(320),SCALE(150), hWnd, (HMENU)CID_REC_CANVAS, g_hInst, NULL); }
        int y = SCALE(168);
        g_hRecSampLabel = CreateWindowExW(0, L"STATIC", L"Sample: 0/3",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(3),SCALE(95),SCALE(16), hWnd, NULL, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Record", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(230),y,SCALE(55),SCALE(22), hWnd, (HMENU)103, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(290),y,SCALE(50),SCALE(22), hWnd, (HMENU)104, g_hInst, NULL);
        y = SCALE(195);
        CreateWindowExW(0, L"STATIC", L"Chain:", WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(2),SCALE(45),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecChain = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,
            SCALE(60),y,SCALE(272),SCALE(22), hWnd, (HMENU)CID_REC_CHAIN, g_hInst, NULL);
        SendMessageW(g_hRecChain, WM_SETFONT, (WPARAM)hf, TRUE); y+=SCALE(28);
        CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(2),SCALE(45),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(54),y,SCALE(80),SCALE(22), hWnd, (HMENU)CID_REC_NAME, g_hInst, NULL);
        SendMessageW(g_hRecName, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"STATIC", L"Mod:", WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(140),y+SCALE(2),SCALE(32),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecMod = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            SCALE(172),y,SCALE(65),SCALE(100), hWnd, (HMENU)CID_REC_MOD, g_hInst, NULL);
        SendMessageW(g_hRecMod, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"STATIC", L"Zone:", WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(242),y+SCALE(2),SCALE(35),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecZone = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            SCALE(276),y,SCALE(60),SCALE(100), hWnd, (HMENU)CID_REC_ZONE, g_hInst, NULL);
        SendMessageW(g_hRecZone, WM_SETFONT, (WPARAM)hf, TRUE);
        for (int zi = 0; zi < ZONE_COUNT; zi++)
            SendMessageW(g_hRecZone, CB_ADDSTRING, 0, (LPARAM)ZoneName(zi));
        SendMessageW(g_hRecZone, CB_SETCURSEL, 0, 0);
        { const wchar_t *ms[]={L"None",L"Ctrl",L"Shift",L"Alt",L"Win",L"Ctrl+Shift",L"Ctrl+Alt",L"Alt+Shift",L"X1",L"X2"};
          int vs[]={0,1,2,4,8,3,5,6,0x10,0x20};
          for (int i=0;i<10;i++) { int ix=(int)SendMessageW(g_hRecMod,CB_ADDSTRING,0,(LPARAM)ms[i]);
              SendMessageW(g_hRecMod,CB_SETITEMDATA,ix,vs[i]); } }
        y+=SCALE(28);
        CreateWindowExW(0, L"STATIC", L"Cmd:", WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(2),SCALE(40),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecCmdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(55),y,SCALE(277),SCALE(22), hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hRecCmdEdit, WM_SETFONT, (WPARAM)hf, TRUE); y+=SCALE(28);
        CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            SCALE(12),y+SCALE(2),SCALE(80),SCALE(20), hWnd, (HMENU)CID_REC_ENABLED, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(130),y+SCALE(2),SCALE(80),SCALE(26), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(218),y+SCALE(2),SCALE(80),SCALE(26), hWnd, (HMENU)102, g_hInst, NULL);

        if (g_recEditIdx >= 0 && g_recEditIdx < g_cfg.gestureCount) {
            GestureDef *g = &g_cfg.gestures[g_recEditIdx];
            { wchar_t wb[128]; MultiByteToWideChar(CP_UTF8,0,g->name,-1,wb,128);
              SetWindowTextW(g_hRecName, wb); }
            { int cnt=(int)SendMessageW(g_hRecMod,CB_GETCOUNT,0,0);
              for (int i=0;i<cnt;i++) if ((int)SendMessageW(g_hRecMod,CB_GETITEMDATA,i,0)==g->modifier)
                  { SendMessageW(g_hRecMod,CB_SETCURSEL,i,0); break; } }
            if (g->zone >= 0 && g->zone < ZONE_COUNT)
                SendMessageW(g_hRecZone, CB_SETCURSEL, (WPARAM)g->zone, 0);
            { char cname[64]; CmdIdToName(g->commandId, cname, 63);
              wchar_t wc[128]; MultiByteToWideChar(CP_UTF8,0,cname,-1,wc,128);
              SetWindowTextW(g_hRecCmdEdit, wc); }
            SendMessageW(GetDlgItem(hWnd,CID_REC_ENABLED),BM_SETCHECK,g->enabled?BST_CHECKED:BST_UNCHECKED,0);
            /* 加载已有采样链 + 点数据（从内存 ptsStr） */
            if (g->chainCount > 0) {
                g_recTotalSamples = g->chainCount;
                char buf[256] = ""; int remain = 255;
                for (int s = 0; s < g->chainCount; s++) {
                    strncpy(g_recSampleChains[s], g->chains[s], 63);
                    g_recSampleChainLens[s] = (int)strlen(g->chains[s]);
                    char one[80]; ChainToDisplay(g->chains[s], one, 79);
                    int n = (int)strlen(buf);
                    if (s > 0 && n < 250) { strncat(buf, "  |  ", remain); remain -= 5; }
                    if (remain > 5) { strncat(buf, one, remain); remain -= (int)strlen(one); }
                    /* 从 ptsStr 恢复点数据 */
                    if (g->ptsStr[s][0]) {
                        int p = 0;
                        const char *cur = g->ptsStr[s];
                        while (p < 500 && *cur) {
                            int x = 0, y = 0;
                            while (*cur && *cur != ',') { x = x*10 + (*cur - '0'); cur++; }
                            if (*cur == ',') cur++;
                            while (*cur && *cur != ';') { y = y*10 + (*cur - '0'); cur++; }
                            if (*cur == ';') cur++;
                            if (x > 0 && y > 0) {
                                g_recSamples[s][p].x = x; g_recSamples[s][p].y = y; p++;
                            }
                        }
                        g_recSampleCounts[s] = p;
                    } else g_recSampleCounts[s] = 0;
                }
                SetWindowTextA(g_hRecChain, buf);
            }
            wchar_t wb[32]; swprintf(wb, 32, L"Sample: %d/3", g_recTotalSamples);
            SetWindowTextW(g_hRecSampLabel, wb);
        } else {
            SendMessageW(g_hRecMod, CB_SETCURSEL, 0, 0); /* 默认 None */
            SendMessageW(g_hRecZone, CB_SETCURSEL, 0, 0); /* 默认 Main */
            SendMessageW(GetDlgItem(hWnd,CID_REC_ENABLED),BM_SETCHECK,BST_CHECKED,0);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 103) { /* Record 按钮 — 提示在画布上右键绘制 */
            if (g_recTotalSamples >= 3)
                { MessageBoxW(hWnd, L"Max 3 samples reached.", L"ShortCutter", MB_OK|MB_ICONINFORMATION); return 0; }
            SetWindowTextW(g_hRecSampLabel, L"Right-drag on canvas...");
            SetFocus(g_hRecCanvas);
            return 0; }
        if (LOWORD(wp) == 104) { /* Clear */
            g_recTotalSamples = 0; g_recPtCount = 0; g_recording = 0;
            memset(g_recSampleCounts, 0, sizeof(g_recSampleCounts));
            memset(g_recSampleChains, 0, sizeof(g_recSampleChains));
            SetWindowTextA(g_hRecChain, "");
            SetWindowTextW(g_hRecSampLabel, L"Sample: 0/3");
            InvalidateRect(g_hRecCanvas, NULL, TRUE);
            return 0; }
        if (LOWORD(wp) == 101) { /* OK */
            wchar_t wRec[128]; char name[64]="";
            GetWindowTextW(g_hRecName, wRec, 63);
            WideCharToMultiByte(CP_UTF8,0,wRec,-1,name,64,NULL,NULL);
            if (!name[0]) { MessageBoxW(hWnd, L"Enter a name.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
            if (g_recTotalSamples == 0 && g_recEditIdx < 0)
                { MessageBoxW(hWnd, L"Draw a gesture first.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
            int idx = g_recEditIdx;
            if (idx < 0) {
                if (g_cfg.gestureCount >= MAX_GESTURES)
                    { MessageBoxW(hWnd, L"Max gestures.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
                idx = g_cfg.gestureCount; g_cfg.gestureCount++;
            }
            GestureDef *g = &g_cfg.gestures[idx];
            strncpy(g->name, name, 63);
            g->chainCount = g_recTotalSamples;
            /* 保存链 + 点数据到内存（INI 由 Config_Save 统一写入） */
            for (int s = 0; s < g_recTotalSamples; s++) {
                strncpy(g->chains[s], g_recSampleChains[s], 63);
                int pos = 0, maxP = g_recSampleCounts[s];
                if (maxP > 200) maxP = 200;
                for (int p = 0; p < maxP && pos < 1000; p++)
                    pos += snprintf(g->ptsStr[s] + pos, 1024 - pos, "%d,%d;",
                        (int)g_recSamples[s][p].x, (int)g_recSamples[s][p].y);
            }
            /* 清除多余采样 */
            for (int s = g_recTotalSamples; s < 3; s++)
                g->ptsStr[s][0] = 0;
            { int s=(int)SendMessageW(g_hRecMod,CB_GETCURSEL,0,0);
              g->modifier=(int)SendMessageW(g_hRecMod,CB_GETITEMDATA,s,0); }
            g->zone = (int)SendMessageW(g_hRecZone, CB_GETCURSEL, 0, 0);
            if (g->zone < 0 || g->zone >= ZONE_COUNT) g->zone = ZONE_MAIN;
            { wchar_t wc[128]; char cname[64];
              GetWindowTextW(g_hRecCmdEdit, wc, 63);
              WideCharToMultiByte(CP_UTF8,0,wc,-1,cname,64,NULL,NULL);
              g->commandId = CmdNameToId(cname); }
            g->enabled = (SendMessageW(GetDlgItem(hWnd,CID_REC_ENABLED),BM_GETCHECK,0,0)==BST_CHECKED);
            PostMessageW(GetParent(hWnd), WM_SC_REFRESH, 0, 0);
            DestroyWindow(hWnd); return 0;
        }
        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_hRecDlg = NULL; g_hRecCanvas = NULL; g_hRecSampLabel = NULL; return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void RecDlg_Show(HWND hParent, int idx)
{
    if (g_hRecDlg) { SetForegroundWindow(g_hRecDlg); return; }
    g_recEditIdx = idx; g_recording = 0; g_recPtCount = 0;
    g_recTotalSamples = 0;
    memset(g_recSampleCounts, 0, sizeof(g_recSampleCounts));
    memset(g_recSampleChains, 0, sizeof(g_recSampleChains));
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=RecDlgProc;
        wc.hInstance=g_hInst; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName=L"SC_RecDlg"; RegisterClassExW(&wc); reg=TRUE;
    }
    int w = SCALE(360), h = SCALE(375);
    g_hRecDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_RecDlg",
        idx < 0 ? L"Record Gesture" : L"Edit Gesture",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
}

/* ══════════ Hotkey 编辑弹窗 ═══════════════════ */
static HWND g_hHkDlg = NULL, g_hHkDlgHK, g_hHkDlgDesc, g_hHkDlgCmdEdit;
static int  g_hkEditIdx = -1;
static BYTE g_hkCapMods, g_hkCapVk;
static char g_hkCapLabel[32];

static LRESULT CALLBACK HkDlgHKProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR refData)
{
    (void)id; (void)refData;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        BYTE vk = (BYTE)wp;
        if (vk == VK_ESCAPE) { g_hkCapMods = 0; g_hkCapVk = 0; g_hkCapLabel[0]=0;
            SetWindowTextW(g_hHkDlgHK, L"Click to bind"); return 0; }
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) return 0;
        BYTE mods = 0;
        if (GetKeyState(VK_CONTROL)&0x8000) mods|=SC_MOD_CTRL;
        if (GetKeyState(VK_MENU)&0x8000)    mods|=SC_MOD_ALT;
        if (GetKeyState(VK_SHIFT)&0x8000)   mods|=SC_MOD_SHIFT;
        if (GetKeyState(VK_LWIN)&0x8000)    mods|=SC_MOD_WIN;
        char label[32]="";
        if (mods&SC_MOD_CTRL) strcat(label,"Ctrl+");
        if (mods&SC_MOD_ALT)  strcat(label,"Alt+");
        if (mods&SC_MOD_SHIFT)strcat(label,"Shift+");
        if (mods&SC_MOD_WIN)  strcat(label,"Win+");
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LONG lpk = (scan<<16);
        if (vk==VK_LEFT||vk==VK_RIGHT||vk==VK_UP||vk==VK_DOWN||
            vk==VK_PRIOR||vk==VK_NEXT||vk==VK_END||vk==VK_HOME||
            vk==VK_INSERT||vk==VK_DELETE) lpk|=(1<<24);
        wchar_t wn[32]={0};
        if (GetKeyNameTextW(lpk,wn,32)) {
            int n=WideCharToMultiByte(CP_UTF8,0,wn,-1,NULL,0,NULL,NULL);
            if (n>0&&n<30) { char *t=(char*)HeapAlloc(GetProcessHeap(),0,n);
                if(t){WideCharToMultiByte(CP_UTF8,0,wn,-1,t,n,NULL,NULL);
                strcat(label,t); HeapFree(GetProcessHeap(),0,t); } }
        }
        g_hkCapMods=mods; g_hkCapVk=vk; strncpy(g_hkCapLabel,label,31);
        SetWindowTextA(g_hHkDlgHK, label); return 0;
    }
    if (msg == WM_RBUTTONUP) { g_hkCapMods=0; g_hkCapVk=0; g_hkCapLabel[0]=0;
        SetWindowTextW(g_hHkDlgHK, L"Click to bind"); return 0; }
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return DefSubclassProc(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK HkDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontW(SCALE(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,L"Segoe UI");
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        CreateWindowExW(0, L"STATIC", L"Hotkey:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(15),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgHK = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(75),SCALE(13),SCALE(260),SCALE(22), hWnd, (HMENU)CID_HK_DLG_HK, g_hInst, NULL);
        SendMessageW(g_hHkDlgHK, WM_SETFONT, (WPARAM)hf, TRUE);
        SetWindowSubclass(g_hHkDlgHK, HkDlgHKProc, CID_HK_DLG_HK, 0);
        CreateWindowExW(0, L"STATIC", L"Desc:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(45),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgDesc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(75),SCALE(43),SCALE(260),SCALE(22), hWnd, (HMENU)CID_HK_DLG_DESC, g_hInst, NULL);
        SendMessageW(g_hHkDlgDesc, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"STATIC", L"Cmd:", WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(75),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgCmdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(75),SCALE(73),SCALE(260),SCALE(22), hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hHkDlgCmdEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            SCALE(15),SCALE(103),SCALE(80),SCALE(20), hWnd, (HMENU)CID_HK_DLG_EN, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(130),SCALE(103),SCALE(80),SCALE(26), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(220),SCALE(103),SCALE(80),SCALE(26), hWnd, (HMENU)102, g_hInst, NULL);
        if (g_hkEditIdx >= 0 && g_hkEditIdx < g_cfg.hotkeyCount) {
            HotkeyDef *h = &g_cfg.hotkeys[g_hkEditIdx];
            if (h->label[0]) SetWindowTextA(g_hHkDlgHK, h->label);
            else SetWindowTextW(g_hHkDlgHK, L"Click to bind");
            g_hkCapMods = h->mods; g_hkCapVk = h->vk;
            strncpy(g_hkCapLabel, h->label, 31);
            { wchar_t wd[128]; MultiByteToWideChar(CP_UTF8,0,h->description,-1,wd,128);
              SetWindowTextW(g_hHkDlgDesc, wd); }
            { char cname[64]; CmdIdToName(h->commandId, cname, 63);
              wchar_t wc[128]; MultiByteToWideChar(CP_UTF8,0,cname,-1,wc,128);
              SetWindowTextW(g_hHkDlgCmdEdit, wc); }
            SendMessageW(GetDlgItem(hWnd,CID_HK_DLG_EN),BM_SETCHECK,h->enabled?BST_CHECKED:BST_UNCHECKED,0);
        } else {
            SetWindowTextW(g_hHkDlgHK, L"Click to bind"); g_hkCapMods=0; g_hkCapVk=0; g_hkCapLabel[0]=0;
            SendMessageW(GetDlgItem(hWnd,CID_HK_DLG_EN),BM_SETCHECK,BST_CHECKED,0);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 101) {
            int idx = g_hkEditIdx;
            if (idx < 0) {
                if (g_cfg.hotkeyCount >= MAX_HOTKEYS)
                    { MessageBoxW(hWnd, L"Max hotkeys.", L"ShortCutter", MB_OK|MB_ICONWARNING); return 0; }
                idx = g_cfg.hotkeyCount; g_cfg.hotkeyCount++;
            }
            HotkeyDef *h = &g_cfg.hotkeys[idx];
            h->mods = g_hkCapMods; h->vk = g_hkCapVk;
            strncpy(h->label, g_hkCapLabel, 31);
            { wchar_t wd[128]; GetWindowTextW(g_hHkDlgDesc, wd, 63);
              WideCharToMultiByte(CP_UTF8,0,wd,-1,h->description,64,NULL,NULL); }
            { wchar_t wc[128]; char cname[64];
              GetWindowTextW(g_hHkDlgCmdEdit, wc, 63);
              WideCharToMultiByte(CP_UTF8,0,wc,-1,cname,64,NULL,NULL);
              h->commandId = CmdNameToId(cname); }
            h->enabled = (SendMessageW(GetDlgItem(hWnd,CID_HK_DLG_EN),BM_GETCHECK,0,0)==BST_CHECKED);
            PostMessageW(GetParent(hWnd), WM_SC_REFRESH, 0, 0);
            DestroyWindow(hWnd); return 0;
        }
        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_hHkDlg = NULL; return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void HkDlg_Show(HWND hParent, int idx)
{
    if (g_hHkDlg) { SetForegroundWindow(g_hHkDlg); return; }
    g_hkEditIdx = idx;
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=HkDlgProc;
        wc.hInstance=g_hInst; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName=L"SC_HkDlg"; RegisterClassExW(&wc); reg=TRUE;
    }
    int w = SCALE(360), h = SCALE(170);
    g_hHkDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_HkDlg",
        idx < 0 ? L"New Hotkey" : L"Edit Hotkey",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
}

/* ══════════ Tab 切换 ══════════════════════════ */
static void Tab_Refresh(HWND hParent)
{
    int s;
    /* Tab 0: General */
    s = (g_activeTab == 0) ? SW_SHOW : SW_HIDE;
    { static const int genCids[] = {3010,3002,3011,3003,3012,CID_MOUSE_BTN,3013,
        CID_TOGGLE_HK,3004,CID_AUTOSTART,3005,
        CID_ZONE_COLOR,CID_ZONE_WIDTH,CID_GLOW_COLOR,CID_GLOW_WIDTH,
        CID_INNER_COLOR,CID_INNER_WIDTH,
        CID_DASH_OPACITY,CID_DASH_OPACITY_VALUE,
        3026,3027,3028,3029,3030,3031,3032,3035,0};
      for (int i = 0; genCids[i]; i++)
          ShowWindow(GetDlgItem(hParent, genCids[i]), s); }
    /* Tab 1: Commands */
    s = (g_activeTab == 1) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hParent, 5005), s); /* Type: label */
    ShowWindow(GetDlgItem(hParent, 5002), s); /* Type filter */
    ShowWindow(GetDlgItem(hParent, 5009), s); /* Group: label */
    ShowWindow(GetDlgItem(hParent, 5008), s); /* Group filter */
    ShowWindow(g_hCmdList, s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_DEL), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_SCAN), s);
    /* Tab 2: Gestures */
    s = (g_activeTab == 2) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hParent, 5006), s); /* Zone: label */
    ShowWindow(GetDlgItem(hParent, 5007), s); /* Zone filter combo */
    ShowWindow(g_hGesList, s);
    ShowWindow(GetDlgItem(hParent, CID_GES_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_GES_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_GES_DEL), s);
    /* Tab 3: Hotkeys */
    s = (g_activeTab == 3) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hHkList, s);
    ShowWindow(GetDlgItem(hParent, CID_HK_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_HK_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_HK_DEL), s);
}

/* ── Toggle 快捷键捕获子类化 ────────────────── */
static int g_tglCap = 0;
static LRESULT CALLBACK TglHKProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR id, DWORD_PTR refData)
{
    (void)id; (void)refData;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        BYTE vk = (BYTE)wp;
        if (vk == VK_ESCAPE) {
            g_cfg.toggleMods = 0; g_cfg.toggleVk = 0; g_cfg.toggleLabel[0] = 0;
            SetWindowTextW(hWnd, L"Click to bind"); g_tglCap = 0;
            SetWindowTextW(g_hStatus, L"● Listening"); return 0; }
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN)
            return 0;
        BYTE mods = 0;
        if (GetKeyState(VK_CONTROL)&0x8000) mods|=SC_MOD_CTRL;
        if (GetKeyState(VK_MENU)&0x8000)    mods|=SC_MOD_ALT;
        if (GetKeyState(VK_SHIFT)&0x8000)   mods|=SC_MOD_SHIFT;
        if (GetKeyState(VK_LWIN)&0x8000)    mods|=SC_MOD_WIN;
        char label[32]="";
        if (mods&SC_MOD_CTRL) strcat(label,"Ctrl+");
        if (mods&SC_MOD_ALT)  strcat(label,"Alt+");
        if (mods&SC_MOD_SHIFT)strcat(label,"Shift+");
        if (mods&SC_MOD_WIN)  strcat(label,"Win+");
        UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);
        LONG lpk=(scan<<16);
        if (vk==VK_LEFT||vk==VK_RIGHT||vk==VK_UP||vk==VK_DOWN||
            vk==VK_PRIOR||vk==VK_NEXT||vk==VK_END||vk==VK_HOME||
            vk==VK_INSERT||vk==VK_DELETE) lpk|=(1<<24);
        wchar_t wn[32]={0};
        if (GetKeyNameTextW(lpk,wn,32)) {
            int n=WideCharToMultiByte(CP_UTF8,0,wn,-1,NULL,0,NULL,NULL);
            if (n>0&&n<30) { char *t=(char*)HeapAlloc(GetProcessHeap(),0,n);
                if(t){WideCharToMultiByte(CP_UTF8,0,wn,-1,t,n,NULL,NULL);
                strcat(label,t); HeapFree(GetProcessHeap(),0,t); }}
        }
        g_cfg.toggleMods = mods; g_cfg.toggleVk = vk;
        strncpy(g_cfg.toggleLabel, label, 31);
        SetWindowTextA(hWnd, label); g_tglCap = 0;
        SetWindowTextW(g_hStatus, L"● Listening"); return 0;
    }
    if (msg == WM_RBUTTONUP) {
        g_cfg.toggleMods = 0; g_cfg.toggleVk = 0; g_cfg.toggleLabel[0]=0;
        SetWindowTextW(hWnd, L"Click to bind"); g_tglCap = 0;
        SetWindowTextW(g_hStatus, L"● Listening"); return 0; }
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return DefSubclassProc(hWnd, msg, wp, lp);
}

/* ══════════ 设置窗口过程 ══════════════════════ */
/* ══════════ Help 弹窗（可滚动） ══════════════════ */
static HWND g_hHelpDlg = NULL;

static const wchar_t *g_helpText =
L"=== ShortCutter 3.0 ===\r\n"
L"\r\n"
L"── 内置快捷键 (Built-in:Keys) ──\r\n"   /* ── 内置快捷键 ── */
L"  Alt+F4            关闭当前窗口\r\n"                   /* 关闭当前窗口 */
L"  Alt+Space,N       最小化窗口\r\n"                         /* 最小化窗口 */
L"  Alt+Space,X       最大化窗口\r\n"                         /* 最大化窗口 */
L"  Alt+Tab           切换窗口\r\n"                               /* 切换窗口 */
L"  Alt+Shift+Tab     反向切换窗口\r\n"                   /* 反向切换窗口 */
L"  Alt+Esc           按打开顺序切换\r\n"             /* 按打开顺序切换 */
L"  Ctrl+Alt+Tab      固定切换面板\r\n"                   /* 固定切换面板 */
L"  Win+D             显示/隐藏桌面\r\n"                  /* 显示/隐藏桌面 */
L"  Win+M             最小化所有窗口\r\n"             /* 最小化所有窗口 */
L"  Win+Shift+M       还原所有窗口\r\n"                   /* 还原所有窗口 */
L"  Win+Up            最大化当前窗口\r\n"             /* 最大化当前窗口 */
L"  Win+Down          还原/最小化当前窗口\r\n" /* 还原/最小化当前窗口 */
L"  Win+Left          贴靠左半屏\r\n"                         /* 贴靠左半屏 */
L"  Win+Right         贴靠右半屏\r\n"                         /* 贴靠右半屏 */
L"  Win+Home          最小化其余窗口\r\n"             /* 最小化其余窗口 */
L"  Win+Tab           任务视图\r\n"                               /* 任务视图 */
L"  Ctrl+T            新建标签页\r\n"                         /* 新建标签页 */
L"  Ctrl+Shift+T      恢复关闭的标签\r\n"             /* 恢复关闭的标签 */
L"  Ctrl+Tab          下一标签\r\n"                               /* 下一标签 */
L"  Ctrl+Shift+Tab    上一标签\r\n"                               /* 上一标签 */
L"  Alt+Left          后退\r\n"                                           /* 后退 */
L"  Alt+Right         前进\r\n"                                           /* 前进 */
L"\r\n"
L"── 内置启动程序 (Built-in:Run) ──\r\n" /* ── 内置启动程序 ── */
L"  Calculator        计算器\r\n"                                     /* 计算器 */
L"  Notepad           记事本\r\n"                                     /* 记事本 */
L"  Task Manager      任务管理器\r\n"                         /* 任务管理器 */
L"  Control Panel     控制面板\r\n"                               /* 控制面板 */
L"  Explorer          文件资源管理器\r\n"             /* 文件资源管理器 */
L"\r\n"
L"── 特殊命令 (弹框输入执行) ──\r\n" /* ── 特殊命令（弹框输入执行）── */
L"  input             弹出命令搜索框\r\n"             /* 弹出命令搜索框 */
L"  sc / settings     打开设置对话框\r\n"             /* 打开设置对话框 */
L"  toggle            启用/禁用 ShortCutter\r\n"                  /* 启用/禁用 */
L"  cmd               管理员命令提示符\r\n"       /* 管理员命令提示符 */
L"  ps / powershell   管理员 PowerShell\r\n"                           /* 管理员 PowerShell */
L"  dashboard / db     打开仪表盘弹窗\r\n"       /* 打开仪表盘弹窗 */
L"\r\n"
L"── 手势系统 ──\r\n"                         /* ── 手势系统 ── */
L"  • 按住右键（或中键）拖动绘制任意形状\r\n" /* • 按住右键（或中键）拖动绘制任意形状 */
L"  • 方向链编码 + LCS 最长公共子序列匹配\r\n" /* • 方向链编码 + LCS 最长公共子序列匹配 */
L"  • 匹配阈值：50（宽松）~ 95（严格），默认 70\r\n" /* • 匹配阈值：50（宽松）~ 95（严格），默认 70 */
L"  • 同一手势录制 2~3 次可提高识别率\r\n" /* • 同一手势录制 2~3 次可提高识别率 */
L"  • 手势期间按住修饰键即视为修饰手势\r\n" /* • 手势期间按住修饰键即视为修饰手势 */
L"\r\n"
L"── 手势区域 ──\r\n"                         /* ── 手势区域 ── */
L"  屏幕划分为 5 个区域：4 个角落 (1/3 宽 x 1/3 高) + 主区域\r\n" /* 屏幕划分为 5 个区域：4 个角落 + 主区域 */
L"  同一手势在不同区域可绑定不同命令\r\n" /* 同一手势在不同区域可绑定不同命令 */
L"  匹配优先级：精确区域 > Main(后备)\r\n" /* 匹配优先级：精确区域 > Main(后备) */
L"  在 General 标签页启用 Zone Lines 可显示区域边界\r\n" /* 启用 Zone Lines 可显示区域边界 */
L"\r\n"
L"── 按键序列格式 ──\r\n"               /* ── 按键序列格式 ── */
L"  {Key down}        按下并保持\r\n"                         /* 按下并保持 */
L"  {Key up}          释放按键\r\n"                               /* 释放按键 */
L"  {Key}             按下并释放（默认）\r\n" /* 按下并释放（默认） */
L"  {Sleep N}         延迟 N 毫秒\r\n"                            /* 延迟 N 毫秒 */
L"  支持：Ctrl Alt Shift Win Tab Enter Space Esc\r\n"                 /* 支持：... */
L"    Backspace Delete Insert Home End PgUp PgDn\r\n"
L"    Up Down Left Right F1-F12 Apps PrtSc A-Z 0-9\r\n"
L"\r\n"
L"── 脚本系统 (scripts\\ 文件夹) ──\r\n" /* ── 脚本系统 ── */
L"  • .bat / .cmd / .ps1 / .vbs 自动注册为命令 (Group=Scripts)\r\n" /* • .bat/.cmd/.ps1/.vbs 自动注册为命令 */
L"  • 默认静默执行（无控制台窗口）\r\n" /* • 默认静默执行（无控制台窗口） */
L"  • 文件名以 _Show 结尾 → 显示窗口\r\n" /* • 文件名以 _Show 结尾 → 显示窗口 */
L"  • 在设置中修改 Name 会同步重命名实际脚本文件\r\n" /* • 修改 Name 同步重命名实际脚本文件 */
L"  • 从文件夹删除脚本文件即移除对应命令\r\n" /* • 删除脚本文件即移除对应命令 */
L"\r\n"
L"── 筛选与分组 ──\r\n"                     /* ── 筛选与分组 ── */
L"  Commands 标签页：Type 筛选 (All/Custom/Built-in/Scripts)\r\n" /* Commands 标签页：Type 筛选 */
L"                 + Group 筛选（动态收集所有已分配的组名）\r\n" /* + Group 筛选（动态收集） */
L"  Gestures 标签页：Zone 筛选 (All/Main/↖ TL/↗ TR/↙ BL/↘ BR)\r\n" /* Gestures 标签页：Zone 筛选 */
L"\r\n"
L"── 扫描已装软件 ──\r\n"
L"  • Commands 标签页 > Scan Apps... 按钮\r\n"
L"  • 自动扫描桌面和开始菜单的 .lnk 快捷方式\r\n"
L"  • 来源筛选：All / Desktop / PF / PFx86 / Other\r\n"
L"  • 只抓 .exe，自动跳过卸载程序和系统组件\r\n"
L"  • 已添加的自动灰色，不可重复导入\r\n"
L"\r\n"
L"── 仪表盘 (Dashboard) ──\r\n"
L"  • 输入 dashboard / db 或托盘菜单打开\r\n"
L"  • 左栏：搜索框 + 历史/匹配列表，支持模糊搜索\r\n"
L"  • 右栏：最常使用 Top 8，显示使用次数\r\n"
L"  • 下半区：自定义命令 Dock，6x4 网格拖拽排列\r\n"
L"  • 双击 Custom 标签添加推荐列表高亮项到 Dock\r\n"
L"  • 右键 Dock 图标可移除，退出时自动保存布局和统计\r\n"
L"  • 搜索框强制英文输入，Space 执行第一匹配项\r\n"
L"  • 点击列表条目直接执行命令并关闭仪表盘\r\n"
L"\r\n"
L"── 其他 ──\r\n"                                       /* ── 其他 ── */
L"  • 单实例运行：重复启动 exe 将静默退出\r\n" /* • 单实例运行 */
L"  • 开关快捷键可在 General 标签页中设置\r\n" /* • 开关快捷键可在 General 标签页中设置 */
L"  • 开机自启通过 HKCU Run 注册表项实现\r\n" /* • 开机自启通过注册表实现 */
L"  • 配置文件：ShortCutter.ini (UTF-16 LE)，位于 exe 同目录\r\n" /* • 配置文件位于 exe 同目录 */
L"\r\n"
L"Created by zhichg.\r\n";

/* ══════════ 应用扫描 ════════════════════════ */
#define MAX_SCAN_ENTRIES  300

typedef struct {
    wchar_t name[128];
    wchar_t path[512];
    int     added;   /* 1=已在 Commands 中 */
    int     source;  /* 0=桌面 1=PF 2=PFx86 3=其他 */
} ScanEntry;

static ScanEntry g_scanEntries[MAX_SCAN_ENTRIES];
static int       g_scanCount = 0;
static int       g_scanFilter = 0; /* 0=All 1=Desktop 2=PF 3=PFx86 4=Other */
static HWND      g_hScanCbo = NULL;

/* 过滤：跳过系统组件 */
/* case-insensitive 子串匹配 */
static int wcsistr(const wchar_t *s, const wchar_t *sub)
{
    int sl = (int)wcslen(s), subl = (int)wcslen(sub);
    if (subl > sl) return 0;
    for (int i = 0; i <= sl - subl; i++) {
        int ok = 1;
        for (int j = 0; j < subl; j++) {
            wchar_t a = s[i+j], b = sub[j];
            if (a >= L'A' && a <= L'Z') a += 32;
            if (b >= L'A' && b <= L'Z') b += 32;
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int Scan_FilterOut(const wchar_t *exePath)
{
    if (!exePath || !exePath[0]) return 1;
    /* 只要 .exe */
    { const wchar_t *ext = wcsrchr(exePath, L'.');
      if (!ext || lstrcmpiW(ext, L".exe") != 0) return 1; }
    /* 系统路径 */
    if (wcsstr(exePath, L"\\Windows\\") || wcsstr(exePath, L"\\WINDOWS\\")) return 1;
    /* 卸载程序 */
    if (wcsistr(exePath, L"unins") || wcsistr(exePath, L"卸载")) return 1;
    return 0;
}

/* 去重：检查 exe 路径是否已在结果中 */
static int Scan_IsDup(const wchar_t *exePath)
{
    for (int i = 0; i < g_scanCount; i++)
        if (lstrcmpiW(g_scanEntries[i].path, exePath) == 0) return 1;
    return 0;
}

/* 扫描注册表 Uninstall */
/* 扫描 .lnk 文件夹（Start Menu / 桌面），递归一层 */
static void Scan_LnkDir(const wchar_t *dir, int depth, int source)
{
    if (depth > 2 || g_scanCount >= MAX_SCAN_ENTRIES) return;
    wchar_t p[MAX_PATH];
    lstrcpyW(p, dir); lstrcatW(p, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wchar_t sub[MAX_PATH];
            lstrcpyW(sub, dir); lstrcatW(sub, L"\\"); lstrcatW(sub, fd.cFileName);
            Scan_LnkDir(sub, depth + 1, source);
        } else if (g_scanCount < MAX_SCAN_ENTRIES) {
            wchar_t *ext = wcsrchr(fd.cFileName, L'.');
            if (!ext || lstrcmpiW(ext, L".lnk") != 0) continue;
            wchar_t full[MAX_PATH];
            lstrcpyW(full, dir); lstrcatW(full, L"\\"); lstrcatW(full, fd.cFileName);
            IShellLinkW *psl;
            IPersistFile *ppf;
            if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC,
                &IID_IShellLinkW, (void**)&psl))) continue;
            if (FAILED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
                psl->lpVtbl->Release(psl); continue; }
            wchar_t target[MAX_PATH] = {0};
            if (SUCCEEDED(ppf->lpVtbl->Load(ppf, full, STGM_READ)))
                psl->lpVtbl->GetPath(psl, target, MAX_PATH, NULL, SLGP_RAWPATH);
            ppf->lpVtbl->Release(ppf);
            psl->lpVtbl->Release(psl);
            if (!target[0]) continue;
            if (Scan_FilterOut(target)) continue;
            if (Scan_IsDup(target)) continue;
            wchar_t *nameEnd = ext;
            wchar_t nameOnly[128];
            lstrcpynW(nameOnly, fd.cFileName, (int)(nameEnd - fd.cFileName) + 1);
            ScanEntry *e = &g_scanEntries[g_scanCount];
            lstrcpynW(e->name, nameOnly, 127);
            lstrcpynW(e->path, target, 511);
            e->added = 0;
            /* 判断来源 */
            if (source == 0)
                e->source = 0; /* 桌面 */
            else if (wcsistr(target, L"\\Program Files (x86)\\"))
                e->source = 2; /* PF x86 */
            else if (wcsistr(target, L"\\Program Files\\"))
                e->source = 1; /* PF */
            else
                e->source = 3; /* 其他 */
            g_scanCount++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 主扫描：桌面优先 → 开始菜单 */
static void Scan_All(void)
{
    g_scanCount = 0;
    CoInitialize(NULL);

    { wchar_t d[MAX_PATH];
      /* 1. 桌面（优先，source=0） */
      if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, d) == S_OK) Scan_LnkDir(d, 0, 0);
      if (SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, d) == S_OK) Scan_LnkDir(d, 0, 0);
      /* 2. 开始菜单（source=-1 → 自动判断 x86/其它） */
      if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, d) == S_OK) Scan_LnkDir(d, 0, -1);
      if (SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, d) == S_OK) Scan_LnkDir(d, 0, -1); }

    CoUninitialize();

    /* 标记已添加的 */
    for (int i = 0; i < g_scanCount; i++) {
        for (int j = 0; j < g_cfg.commandCount; j++) {
            if (g_cfg.commands[j].type == CMD_RUN) {
                wchar_t wPath[512];
                MultiByteToWideChar(CP_UTF8, 0, g_cfg.commands[j].runPath, -1, wPath, 512);
                if (lstrcmpiW(wPath, g_scanEntries[i].path) == 0)
                    { g_scanEntries[i].added = 1; break; }
            }
        }
    }
}

/* ══════════ 扫描结果弹窗 ════════════════════════ */
static HWND g_hScanDlg = NULL, g_hScanList = NULL;

/* ListView row → g_scanEntries index（考虑筛选） */
static int ScanDlg_RowToIdx(int row)
{
    int cnt = 0;
    for (int i = 0; i < g_scanCount; i++) {
        int s = g_scanEntries[i].source;
        if (g_scanFilter != 0 && g_scanFilter != s + 1) continue;
        if (cnt == row) return i;
        cnt++;
    }
    return -1;
}

static void ScanDlg_Fill(void)
{
    SendMessageW(g_hScanList, LVM_DELETEALLITEMS, 0, 0);
    LVITEMW item = {0}; item.mask = LVIF_TEXT;
    int row = 0;
    for (int i = 0; i < g_scanCount; i++) {
        ScanEntry *e = &g_scanEntries[i];
        int s = e->source;
        if (g_scanFilter != 0 && g_scanFilter != s + 1) continue; /* 筛选 */
        item.iItem = row; item.iSubItem = 0; item.pszText = e->name;
        SendMessageW(g_hScanList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1; item.pszText = e->path;
        SendMessageW(g_hScanList, LVM_SETITEMW, 0, (LPARAM)&item);
        ListView_SetCheckState(g_hScanList, row, FALSE); /* 默认不勾选 */
        row++;
    }
    { LONG gs = GetWindowLongW(g_hScanList, GWL_STYLE);
      SetWindowLongW(g_hScanList, GWL_STYLE, gs & ~WS_HSCROLL);
      SetWindowPos(g_hScanList, NULL, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED); }
}

static void ScanDlg_ToggleAll(void)
{
    int allChecked = 1, cnt = (int)SendMessageW(g_hScanList, LVM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < cnt; i++) {
        if (!ListView_GetCheckState(g_hScanList, i)) { allChecked = 0; break; }
    }
    int newState = allChecked ? 0 : 1;
    for (int i = 0; i < cnt; i++)
        ListView_SetCheckState(g_hScanList, i, newState);
}

static LRESULT CALLBACK ScanDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontW(SCALE(14), 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);

        CreateWindowExW(0, L"STATIC", g_scanCount > 0 ? L"" : L"Scanning...",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(10), SCALE(2), SCALE(400), SCALE(16), hWnd, (HMENU)5010, g_hInst, NULL);

        /* 来源筛选（单选） */
        { g_hScanCbo = CreateWindowExW(0, L"COMBOBOX", L"",
              WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
              SCALE(10), SCALE(20), SCALE(180), SCALE(200), hWnd, (HMENU)5020, g_hInst, NULL);
          SendMessageW(g_hScanCbo, WM_SETFONT, (WPARAM)hf, TRUE);
          { const wchar_t *items[] = {L"Desktop", L"Program Files", L"Program Files (x86)", L"Other"};
            SendMessageW(g_hScanCbo, CB_ADDSTRING, 0, (LPARAM)L"All");
            for (int s = 0; s < 4; s++) SendMessageW(g_hScanCbo, CB_ADDSTRING, 0, (LPARAM)items[s]); }
          SendMessageW(g_hScanCbo, CB_SETCURSEL, (WPARAM)g_scanFilter, 0); }

        g_hScanList = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            SCALE(10), SCALE(48), SCALE(430), SCALE(240), hWnd, (HMENU)5011, g_hInst, NULL);
        SendMessageW(g_hScanList, WM_SETFONT, (WPARAM)hf, TRUE);
        ListView_SetExtendedListViewStyle(g_hScanList,
            LVS_EX_FULLROWSELECT|LVS_EX_CHECKBOXES);
        { LONG gs = GetWindowLongW(g_hScanList, GWL_STYLE);
          SetWindowLongW(g_hScanList, GWL_STYLE, gs & ~WS_HSCROLL);
          SetWindowPos(g_hScanList, NULL, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED); }
        { LVCOLUMNW col={0}; col.mask=LVCF_TEXT|LVCF_WIDTH;
          col.cx=SCALE(148); col.pszText=L"Name"; SendMessageW(g_hScanList,LVM_INSERTCOLUMNW,0,(LPARAM)&col);
          col.cx=SCALE(255); col.pszText=L"Path"; SendMessageW(g_hScanList,LVM_INSERTCOLUMNW,1,(LPARAM)&col); }

        CreateWindowExW(0, L"BUTTON", L"Toggle All",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(10), SCALE(302), SCALE(80), SCALE(24), hWnd, (HMENU)5012, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Detail",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(98), SCALE(302), SCALE(55), SCALE(24), hWnd, (HMENU)5013, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            SCALE(270), SCALE(302), SCALE(75), SCALE(24), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            SCALE(352), SCALE(302), SCALE(75), SCALE(24), hWnd, (HMENU)102, g_hInst, NULL);

        { wchar_t wc[32];
          swprintf(wc, 32, L"Found %d apps", g_scanCount);
          SetWindowTextW(GetDlgItem(hWnd, 5010), wc); }
        ScanDlg_Fill();
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR*)lp;
        if (nm->idFrom == 5011 && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nlv = (NMLISTVIEW*)lp;
            if ((nlv->uChanged & LVIF_STATE) &&
                ((nlv->uOldState & 0x2000) != (nlv->uNewState & 0x2000))) {
                int row = nlv->iItem, i2 = ScanDlg_RowToIdx(row);
                if (i2 >= 0 && g_scanEntries[i2].added)
                    ListView_SetCheckState(g_hScanList, row, FALSE);
            }
        }
        if (nm->idFrom == 5011 && nm->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW*)lp;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                SetWindowLongPtrW(hWnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int row = (int)cd->nmcd.dwItemSpec, i3 = ScanDlg_RowToIdx(row);
                if (i3 >= 0 && g_scanEntries[i3].added) {
                    cd->clrText = RGB(160,160,160);
                    cd->clrTextBk = GetSysColor(COLOR_BTNFACE);
                }
                SetWindowLongPtrW(hWnd, DWLP_MSGRESULT, CDRF_NEWFONT);
                return CDRF_NEWFONT;
            }
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 5020 && HIWORD(wp) == CBN_SELCHANGE) {
            g_scanFilter = (int)SendMessageW(g_hScanCbo, CB_GETCURSEL, 0, 0);
            ScanDlg_Fill(); return 0; }
        if (LOWORD(wp) == 5012 && HIWORD(wp) == BN_CLICKED) { ScanDlg_ToggleAll(); return 0; }
        if (LOWORD(wp) == 5013 && HIWORD(wp) == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hScanList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { int idx = ScanDlg_RowToIdx(sel);
                if (idx >= 0) MessageBoxW(hWnd, g_scanEntries[idx].path,
                    g_scanEntries[idx].name, MB_OK|MB_ICONINFORMATION); }
            return 0;
        }
        if (LOWORD(wp) == 101) {
            /* 导入勾选的 */
            int added = 0;
            int rows = (int)SendMessageW(g_hScanList, LVM_GETITEMCOUNT, 0, 0);
            for (int r = 0; r < rows && g_cfg.commandCount < MAX_COMMANDS; r++) {
                if (!ListView_GetCheckState(g_hScanList, r)) continue;
                int i = ScanDlg_RowToIdx(r);
                if (i < 0 || g_scanEntries[i].added) continue;
                Command *c = &g_cfg.commands[g_cfg.commandCount];
                c->id = g_cfg.nextCmdId++;
                WideCharToMultiByte(CP_UTF8, 0, g_scanEntries[i].name, -1, c->name, 64, NULL, NULL);
                c->type = CMD_RUN;
                WideCharToMultiByte(CP_UTF8, 0, g_scanEntries[i].path, -1, c->runPath, 512, NULL, NULL);
                c->keySeq[0] = 0;
                c->locked = 0;
                strncpy(c->group, "Apps", 31);
                g_cfg.commandCount++;
                added++;
            }
            if (added > 0) PostMessageW(GetParent(hWnd), WM_SC_REFRESH, 0, 0);
            DestroyWindow(hWnd); return 0;
        }
        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_hScanDlg = NULL; g_hScanList = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void ScanDlg_Show(HWND hParent)
{
    if (g_hScanDlg) { SetForegroundWindow(g_hScanDlg); return; }
    Scan_All();
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ScanDlgProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"SC_ScanDlg"; RegisterClassExW(&wc); reg = TRUE;
    }
    int w = SCALE(460), h = SCALE(365);
    g_hScanDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_ScanDlg",
        L"Scan Installed Apps",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
}

/* ══════════ Help 弹窗（可滚动） ══════════════════ */
static LRESULT CALLBACK HelpDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontW(SCALE(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas");
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_helpText,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,
            SCALE(10), SCALE(10), SCALE(460), SCALE(340),
            hWnd, NULL, g_hInst, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(hEdit, EM_SETMARGINS, EC_LEFTMARGIN|EC_RIGHTMARGIN, MAKELPARAM(8,8));
        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            SCALE(195), SCALE(360), SCALE(90), SCALE(26),
            hWnd, (HMENU)101, g_hInst, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 101 || LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_hHelpDlg = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ══════════ 设置窗口过程 ══════════════════════ */
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int fs = SCALE(15);
        g_hFont = CreateFontW(fs,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,L"Segoe UI");
        g_hWhiteBr = CreateSolidBrush(RGB(255,255,255));
        /* Tab 控件 */
        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
            SCALE(5),SCALE(5), SCALE(SETS_W-10), SCALE(275),
            hWnd, (HMENU)CID_TAB, g_hInst, NULL);
        SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SetWindowTheme(g_hTab, L"", L"");
        { TCITEMW tci={0}; tci.mask=TCIF_TEXT;
          tci.pszText=L"General"; SendMessageW(g_hTab,TCM_INSERTITEMW,0,(LPARAM)&tci);
          tci.pszText=L"Commands"; SendMessageW(g_hTab,TCM_INSERTITEMW,1,(LPARAM)&tci);
          tci.pszText=L"Gestures"; SendMessageW(g_hTab,TCM_INSERTITEMW,2,(LPARAM)&tci);
          tci.pszText=L"Hotkeys"; SendMessageW(g_hTab,TCM_INSERTITEMW,3,(LPARAM)&tci); }

        int tabY = SCALE(28), btnY;

        /* General Tab */
        { int y = tabY + SCALE(6), r1 = y + SCALE(12);
          /* Row 1 — 识别参数 */
          CreateWindowExW(0, L"STATIC", L"Min Size:", WS_CHILD|SS_LEFT,
              SCALE(8),r1+SCALE(2),SCALE(55),SCALE(16), hWnd,(HMENU)3010, g_hInst,NULL);
          CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD|WS_TABSTOP|ES_NUMBER,
              SCALE(65),r1,SCALE(35),SCALE(22), hWnd,(HMENU)3002, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"Match:", WS_CHILD|SS_LEFT,
              SCALE(115),r1+SCALE(2),SCALE(42),SCALE(16), hWnd,(HMENU)3011, g_hInst,NULL);
          CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD|WS_TABSTOP|ES_NUMBER,
              SCALE(159),r1,SCALE(35),SCALE(22), hWnd,(HMENU)3003, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"%", WS_CHILD|SS_LEFT,
              SCALE(196),r1+SCALE(2),SCALE(14),SCALE(16), hWnd,(HMENU)3029, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"Mouse:", WS_CHILD|SS_LEFT,
              SCALE(218),r1+SCALE(2),SCALE(45),SCALE(16), hWnd,(HMENU)3012, g_hInst,NULL);
          g_hMouseBtn = CreateWindowExW(0, L"COMBOBOX", NULL,
              WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
              SCALE(266),r1-SCALE(1),SCALE(105),SCALE(120), hWnd,(HMENU)CID_MOUSE_BTN, g_hInst,NULL);
          /* Row 2 — 开关快捷键 */
          int r2 = y + SCALE(48);
          CreateWindowExW(0, L"STATIC", L"Toggle:", WS_CHILD|SS_LEFT,
              SCALE(8),r2+SCALE(2),SCALE(48),SCALE(18), hWnd,(HMENU)3013, g_hInst,NULL);
          g_hToggleHK = CreateWindowExW(0, L"BUTTON", L"",
              WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
              SCALE(60),r2,SCALE(140),SCALE(22), hWnd,(HMENU)CID_TOGGLE_HK, g_hInst,NULL);
          /* Row 3 — Trail 开关 + Outer / Core */
          int r3 = y + SCALE(86);
          CreateWindowExW(0, L"BUTTON", L"Trail",
              WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,
              SCALE(8),r3,SCALE(46),SCALE(22), hWnd,(HMENU)3004, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"Outer", WS_CHILD|SS_LEFT,
              SCALE(58),r3+SCALE(3),SCALE(34),SCALE(16), hWnd,(HMENU)3026, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
              SCALE(94),r3,SCALE(24),SCALE(22), hWnd,(HMENU)CID_GLOW_COLOR, g_hInst,NULL);
          { HWND hGw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,
              SCALE(122),r3+SCALE(1),SCALE(34),SCALE(20), hWnd,(HMENU)CID_GLOW_WIDTH, g_hInst,NULL);
            SendMessageW(hGw, WM_SETFONT, (WPARAM)g_hFont, TRUE); }
          CreateWindowExW(0, L"STATIC", L"px", WS_CHILD|SS_LEFT,
              SCALE(158),r3+SCALE(3),SCALE(18),SCALE(16), hWnd,(HMENU)3030, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"Core", WS_CHILD|SS_LEFT,
              SCALE(185),r3+SCALE(3),SCALE(30),SCALE(16), hWnd,(HMENU)3028, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
              SCALE(217),r3,SCALE(24),SCALE(22), hWnd,(HMENU)CID_INNER_COLOR, g_hInst,NULL);
          { HWND hIw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,
              SCALE(245),r3+SCALE(1),SCALE(34),SCALE(20), hWnd,(HMENU)CID_INNER_WIDTH, g_hInst,NULL);
            SendMessageW(hIw, WM_SETFONT, (WPARAM)g_hFont, TRUE); }
          CreateWindowExW(0, L"STATIC", L"px", WS_CHILD|SS_LEFT,
              SCALE(281),r3+SCALE(3),SCALE(18),SCALE(16), hWnd,(HMENU)3031, g_hInst,NULL);
          /* Row 4 — Zone 开关 + 颜色 */
          int r4 = y + SCALE(114);
          CreateWindowExW(0, L"BUTTON", L"Zone",
              WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,
              SCALE(8),r4,SCALE(50),SCALE(22), hWnd,(HMENU)3005, g_hInst,NULL);
          CreateWindowExW(0, L"STATIC", L"Color", WS_CHILD|SS_LEFT,
              SCALE(58),r4+SCALE(3),SCALE(34),SCALE(16), hWnd,(HMENU)3027, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
              SCALE(94),r4,SCALE(24),SCALE(22), hWnd,(HMENU)CID_ZONE_COLOR, g_hInst,NULL);
          { HWND hZw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,
              SCALE(122),r4+SCALE(1),SCALE(34),SCALE(20), hWnd,(HMENU)CID_ZONE_WIDTH, g_hInst,NULL);
            SendMessageW(hZw, WM_SETFONT, (WPARAM)g_hFont, TRUE); }
          CreateWindowExW(0, L"STATIC", L"px", WS_CHILD|SS_LEFT,
              SCALE(158),r4+SCALE(3),SCALE(18),SCALE(16), hWnd,(HMENU)3032, g_hInst,NULL);
          /* Row 5 — 开机自启 */
          int r5 = y + SCALE(146);
          CreateWindowExW(0, L"BUTTON", L"Auto-start with Windows",
              WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,
              SCALE(8),r5,SCALE(180),SCALE(22), hWnd,(HMENU)CID_AUTOSTART, g_hInst,NULL);
          /* Row 6 — Dashboard transparency */
          int r6 = y + SCALE(178);
          CreateWindowExW(0, L"STATIC", L"Dashboard opacity:", WS_CHILD|SS_LEFT,
              SCALE(8),r6+SCALE(7),SCALE(103),SCALE(16), hWnd,
              (HMENU)3035, g_hInst,NULL);
          g_hDashOpacity = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
              WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_AUTOTICKS|TBS_HORZ,
              SCALE(112),r6,SCALE(205),SCALE(30), hWnd,
              (HMENU)CID_DASH_OPACITY, g_hInst,NULL);
          g_hDashOpacityValue = CreateWindowExW(0, L"STATIC", L"100%",
              WS_CHILD|WS_VISIBLE|SS_RIGHT,
              SCALE(325),r6+SCALE(7),SCALE(42),SCALE(16), hWnd,
              (HMENU)CID_DASH_OPACITY_VALUE, g_hInst,NULL);
 }

        /* Commands Tab */
        { int y = tabY + SCALE(6);
          /* 筛选下拉 */
          { CreateWindowExW(0, L"STATIC", L"Type:", WS_CHILD|SS_LEFT,
                SCALE(8),y+SCALE(3),SCALE(35),SCALE(16), hWnd, (HMENU)5005, g_hInst, NULL);
            { HWND hC = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(42),y,SCALE(72),SCALE(120), hWnd,(HMENU)5002, g_hInst,NULL);
            SendMessageW(hC, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"All");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Custom");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Built-in");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Scripts");
            SendMessageW(hC, CB_SETCURSEL, 0, 0); }
            CreateWindowExW(0, L"STATIC", L"Group:", WS_CHILD|SS_LEFT,
                SCALE(118),y+SCALE(3),SCALE(38),SCALE(16), hWnd, (HMENU)5009, g_hInst, NULL);
            { HWND hG = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(158),y,SCALE(90),SCALE(120), hWnd,(HMENU)5008, g_hInst,NULL);
            SendMessageW(hG, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hG, CB_ADDSTRING, 0, (LPARAM)L"All");
            SendMessageW(hG, CB_SETCURSEL, 0, 0); } }
          int listY = y + SCALE(24);
          g_hCmdList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
              SCALE(8), listY, SCALE(SETS_W-26), SCALE(160), hWnd, (HMENU)CID_CMD_LIST, g_hInst, NULL);
          SendMessageW(g_hCmdList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hCmdList, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
          SetWindowTheme((HWND)SendMessageW(g_hCmdList, LVM_GETHEADER, 0, 0), L"", L"");
          LVCOLUMNW col={0}; col.mask=LVCF_TEXT|LVCF_WIDTH;
          col.cx=SCALE(120); col.pszText=L"Name"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,0,(LPARAM)&col);
          col.cx=SCALE(45);  col.pszText=L"Way"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,1,(LPARAM)&col);
          col.cx=SCALE(70);  col.pszText=L"Group"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,2,(LPARAM)&col);
          col.cx=SCALE(195); col.pszText=L"Target"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,3,(LPARAM)&col);
          btnY = listY + SCALE(168);
          CreateWindowExW(0, L"BUTTON", L"New...", WS_CHILD|WS_TABSTOP,
              SCALE(8),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(88),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(168),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_DEL, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Scan Apps...", WS_CHILD|WS_TABSTOP,
              SCALE(252),btnY,SCALE(95),SCALE(24), hWnd,(HMENU)CID_CMD_SCAN, g_hInst,NULL); }

        /* Gestures Tab */
        { int y = tabY + SCALE(6);
          /* Zone 筛选下拉 */
          { CreateWindowExW(0, L"STATIC", L"Zone:", WS_CHILD|SS_LEFT,
                SCALE(8),y+SCALE(3),SCALE(38),SCALE(16), hWnd, (HMENU)5006, g_hInst, NULL);
            { HWND hZ = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(48),y,SCALE(80),SCALE(120), hWnd,(HMENU)5007, g_hInst,NULL);
            SendMessageW(hZ, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"All");
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"Main");
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"↖ TL");
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"↗ TR");
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"↙ BL");
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"↘ BR");
            SendMessageW(hZ, CB_SETCURSEL, 0, 0); } }
          int listY = y + SCALE(24);
          g_hGesList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
              SCALE(8), listY, SCALE(SETS_W-26), SCALE(165), hWnd, (HMENU)CID_GES_LIST, g_hInst, NULL);
          SendMessageW(g_hGesList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hGesList, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
          SetWindowTheme((HWND)SendMessageW(g_hGesList, LVM_GETHEADER, 0, 0), L"", L"");
          { LONG gs = GetWindowLongW(g_hGesList, GWL_STYLE);
            SetWindowLongW(g_hGesList, GWL_STYLE, gs & ~WS_HSCROLL);
            SetWindowPos(g_hGesList, NULL, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED); }
          LVCOLUMNW col2={0}; col2.mask=LVCF_TEXT|LVCF_WIDTH;
          col2.cx=SCALE(80);  col2.pszText=L"Name"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,0,(LPARAM)&col2);
          col2.cx=SCALE(105); col2.pszText=L"Chain"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,1,(LPARAM)&col2);
          col2.cx=SCALE(48);  col2.pszText=L"Mod"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,2,(LPARAM)&col2);
          col2.cx=SCALE(52);  col2.pszText=L"Zone"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,3,(LPARAM)&col2);
          col2.cx=SCALE(100); col2.pszText=L"Command"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,4,(LPARAM)&col2);
          col2.cx=SCALE(30);  col2.pszText=L"On"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,5,(LPARAM)&col2);
          btnY = listY + SCALE(173);
          CreateWindowExW(0, L"BUTTON", L"Record...", WS_CHILD|WS_TABSTOP,
              SCALE(8),btnY,SCALE(80),SCALE(24), hWnd,(HMENU)CID_GES_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(93),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_GES_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(173),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_GES_DEL, g_hInst,NULL); }

        /* Hotkeys Tab */
        { int y = tabY + SCALE(6);
          g_hHkList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
              SCALE(8), y, SCALE(SETS_W-26), SCALE(185), hWnd, (HMENU)CID_HK_LIST, g_hInst, NULL);
          SendMessageW(g_hHkList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hHkList, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
          SetWindowTheme((HWND)SendMessageW(g_hHkList, LVM_GETHEADER, 0, 0), L"", L"");
          LVCOLUMNW col3={0}; col3.mask=LVCF_TEXT|LVCF_WIDTH;
          col3.cx=SCALE(150); col3.pszText=L"Hotkey"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,0,(LPARAM)&col3);
          col3.cx=SCALE(130); col3.pszText=L"Description"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,1,(LPARAM)&col3);
          col3.cx=SCALE(110); col3.pszText=L"Command"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,2,(LPARAM)&col3);
          col3.cx=SCALE(30);  col3.pszText=L"On"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,3,(LPARAM)&col3);
          btnY = y + SCALE(192);
          CreateWindowExW(0, L"BUTTON", L"New...", WS_CHILD|WS_TABSTOP,
              SCALE(8),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(88),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(168),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_DEL, g_hInst,NULL); }

        g_hStatus = CreateWindowExW(0, L"STATIC", L"● Listening",
            WS_CHILD|WS_VISIBLE|SS_LEFT, SCALE(8), SCALE(285), SCALE(SETS_W-210), SCALE(16),
            hWnd,(HMENU)CID_STATUS, g_hInst,NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        /* Save + Help 按钮固定在右下角，所有标签页可见 */
        CreateWindowExW(0, L"BUTTON", L"Help",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(SETS_W-200), SCALE(281), SCALE(55), SCALE(24),
            hWnd,(HMENU)5001, g_hInst,NULL);
        CreateWindowExW(0, L"BUTTON", L"Save && Apply",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(SETS_W-135), SCALE(281), SCALE(120), SCALE(24),
            hWnd,(HMENU)CID_BTN_SAVE, g_hInst,NULL);

        /* 填充数据 */
        { SendMessageW(g_hMouseBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          SendMessageW(g_hMouseBtn, CB_ADDSTRING, 0, (LPARAM)L"Right Button");
          SendMessageW(g_hMouseBtn, CB_ADDSTRING, 0, (LPARAM)L"Middle Button");
          SendMessageW(g_hMouseBtn, CB_SETCURSEL, (WPARAM)(g_cfg.mouseButton-1), 0); }
        SendMessageW(g_hDashOpacity, TBM_SETRANGE, TRUE, MAKELONG(20, 100));
        SendMessageW(g_hDashOpacity, TBM_SETPAGESIZE, 0, 10);
        SendMessageW(g_hDashOpacity, TBM_SETTICFREQ, 10, 0);
        SendMessageW(g_hDashOpacity, TBM_SETPOS, TRUE, g_cfg.dashboardOpacity);
        { wchar_t opacityText[16];
          swprintf(opacityText, 16, L"%d%%", g_cfg.dashboardOpacity);
          SetWindowTextW(g_hDashOpacityValue, opacityText); }
        /* General tab 控件字体 */
        { HWND c;
          c=GetDlgItem(hWnd,3002); SendMessageW(c,WM_SETFONT,(WPARAM)g_hFont,TRUE);
          { wchar_t mb[32]; swprintf(mb,32,L"%d",g_cfg.minGesture); SetWindowTextW(c,mb); }
          c=GetDlgItem(hWnd,3003); SendMessageW(c,WM_SETFONT,(WPARAM)g_hFont,TRUE);
          { wchar_t mt[32]; swprintf(mt,32,L"%d",g_cfg.matchThreshold); SetWindowTextW(c,mt); }
          SendMessageW(g_hToggleHK, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          SetWindowSubclass(g_hToggleHK, TglHKProc, CID_TOGGLE_HK, 0); }
        if (g_cfg.toggleLabel[0]) SetWindowTextA(g_hToggleHK, g_cfg.toggleLabel);
        else SetWindowTextW(g_hToggleHK, L"Click to bind");
        SendMessageW(GetDlgItem(hWnd,CID_AUTOSTART), BM_SETCHECK, g_cfg.autoStart?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hWnd,3004), BM_SETCHECK, g_cfg.trailEnabled?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hWnd,3005), BM_SETCHECK, g_cfg.zoneLinesEnabled?BST_CHECKED:BST_UNCHECKED,0);
        /* 初始化外观颜色变量 */
        g_zoneColor  = (COLORREF)g_cfg.zoneLineColor;
        g_glowColor  = (COLORREF)g_cfg.trailGlowColor;
        g_innerColor = (COLORREF)g_cfg.trailInnerColor;
        /* 填充宽度输入框 */
        { wchar_t ws[8];
          swprintf(ws,8,L"%d",g_cfg.trailGlowWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_GLOW_WIDTH), ws);
          swprintf(ws,8,L"%d",g_cfg.trailInnerWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_INNER_WIDTH), ws);
          swprintf(ws,8,L"%d",g_cfg.zoneLineWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_ZONE_WIDTH), ws);
        }
        Scripts_ScanAndSync();
        SendMessageW(GetDlgItem(hWnd,5002), CB_SETCURSEL, (WPARAM)g_cmdFilter, 0);
        SendMessageW(GetDlgItem(hWnd,5007), CB_SETCURSEL, (WPARAM)g_gesFilter, 0);
        CmdList_Fill(); GesList_Fill(); HkList_Fill();

        g_activeTab = 0;
        SendMessageW(g_hTab, TCM_SETCURSEL, 0, 0);
        Tab_Refresh(hWnd);
        return 0;
    }

    case WM_SC_DEFER_SAVE:
        Config_Save();
        Config_AutoStart(g_cfg.autoStart);
        Hotkeys_Register();  /* 重注册 Toggle + 键盘钩子 */
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            /* 窗口激活时同步所有筛选下拉框 */
            SendMessageW(GetDlgItem(hWnd,5002), CB_SETCURSEL, (WPARAM)g_cmdFilter, 0);
            { HWND hG = GetDlgItem(hWnd, 5008);
              if (hG) SendMessageW(hG, CB_SETCURSEL, (WPARAM)g_cmdGrpFilter, 0); }
            SendMessageW(GetDlgItem(hWnd,5007), CB_SETCURSEL, (WPARAM)g_gesFilter, 0);
        }
        break;
    case WM_SC_REFRESH:
        Scripts_ScanAndSync();
        SendMessageW(GetDlgItem(hWnd,5002), CB_SETCURSEL, (WPARAM)g_cmdFilter, 0);
        { HWND hG = GetDlgItem(hWnd, 5008);
          if (hG) SendMessageW(hG, CB_SETCURSEL, (WPARAM)g_cmdGrpFilter, 0); }
        SendMessageW(GetDlgItem(hWnd,5007), CB_SETCURSEL, (WPARAM)g_gesFilter, 0);
        /* 刷新前排序 */
        if (g_cfg.commandCount > 1) qsort(g_cfg.commands, g_cfg.commandCount, sizeof(Command), CmpCmd);
        if (g_cfg.gestureCount > 1) qsort(g_cfg.gestures, g_cfg.gestureCount, sizeof(GestureDef), CmpGes);
        if (g_cfg.hotkeyCount  > 1) qsort(g_cfg.hotkeys,  g_cfg.hotkeyCount,  sizeof(HotkeyDef), CmpHk);
        CmdList_Fill(); GesList_Fill(); HkList_Fill();
        Hotkeys_Register();
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == g_hDashOpacity) {
            int opacity = (int)SendMessageW(g_hDashOpacity, TBM_GETPOS, 0, 0);
            wchar_t opacityText[16];
            swprintf(opacityText, 16, L"%d%%", opacity);
            SetWindowTextW(g_hDashOpacityValue, opacityText);
            return 0;
        }
        break;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR*)lp;
        if (nm->idFrom == CID_TAB && nm->code == TCN_SELCHANGE) {
            g_activeTab = (int)SendMessageW(g_hTab, TCM_GETCURSEL, 0, 0);
            Tab_Refresh(hWnd); return 0;
        }
        break;
    }

    case WM_COMMAND: {
        WORD cid = LOWORD(wp), code = HIWORD(wp);
        /* Commands */
        if (cid == 5002 && code == CBN_SELCHANGE) {
            g_cmdFilter = (int)SendMessageW(GetDlgItem(hWnd,5002), CB_GETCURSEL, 0, 0);
            CmdList_Fill(); return 0; }
        if (cid == 5008 && code == CBN_SELCHANGE) {
            g_cmdGrpFilter = (int)SendMessageW(GetDlgItem(hWnd,5008), CB_GETCURSEL, 0, 0);
            CmdList_Fill(); return 0; }
        if (cid == CID_CMD_NEW && code == BN_CLICKED) { CmdDlg_Show(hWnd, -1); return 0; }
        if (cid == CID_CMD_EDIT && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hCmdList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { int idx = CmdListRowToIdx(sel);
                if (idx >= 0) CmdDlg_Show(hWnd, idx); } return 0; }
        if (cid == CID_CMD_DEL && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hCmdList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { int idx = CmdListRowToIdx(sel);
                if (idx >= 0) {
                    Command *c = &g_cfg.commands[idx];
                    if (c->locked) {
                        MessageBoxW(hWnd, c->type == CMD_SCRIPT ?
                            L"Scripts cannot be deleted. Remove the file from scripts\\ folder." :
                            L"Built-in commands cannot be deleted.",
                            L"ShortCutter", MB_OK|MB_ICONINFORMATION); return 0; }
                    for (int i = idx; i < g_cfg.commandCount - 1; i++) g_cfg.commands[i] = g_cfg.commands[i+1];
                    g_cfg.commandCount--; CmdList_Fill();
            } } return 0; }
        if (cid == CID_CMD_SCAN && code == BN_CLICKED) { ScanDlg_Show(hWnd); return 0; }
        /* Gestures */
        if (cid == 5007 && code == CBN_SELCHANGE) {
            g_gesFilter = (int)SendMessageW(GetDlgItem(hWnd,5007), CB_GETCURSEL, 0, 0);
            GesList_Fill(); return 0; }
        if (cid == CID_GES_NEW && code == BN_CLICKED) { RecDlg_Show(hWnd, -1); return 0; }
        if (cid == CID_GES_EDIT && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hGesList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { int idx = GesListRowToIdx(sel);
                if (idx >= 0) RecDlg_Show(hWnd, idx); } return 0; }
        if (cid == CID_GES_DEL && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hGesList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) {
                int idx = GesListRowToIdx(sel);
                if (idx < 0) return 0;
                for (int i = idx; i < g_cfg.gestureCount - 1; i++) g_cfg.gestures[i] = g_cfg.gestures[i+1];
                g_cfg.gestureCount--; GesList_Fill();
            } return 0; }
        /* Hotkeys */
        if (cid == CID_HK_NEW && code == BN_CLICKED) { HkDlg_Show(hWnd, -1); return 0; }
        if (cid == CID_HK_EDIT && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hHkList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { HkDlg_Show(hWnd, sel); } return 0; }
        if (cid == CID_HK_DEL && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hHkList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) {
                for (int i = sel; i < g_cfg.hotkeyCount - 1; i++) g_cfg.hotkeys[i] = g_cfg.hotkeys[i+1];
                g_cfg.hotkeyCount--; HkList_Fill();
            } return 0; }
        /* 开关快捷键 */
        if (cid == CID_TOGGLE_HK && code == BN_CLICKED) {
            g_tglCap = 1; SetWindowTextA(g_hToggleHK, "...");
            SetWindowTextW(g_hStatus, L"⌨ Press key for toggle..."); SetFocus(g_hToggleHK);
            return 0; }
        /* 开机自启 */
        if (cid == CID_AUTOSTART && code == BN_CLICKED) {
            int chk = (int)SendMessageW(GetDlgItem(hWnd,CID_AUTOSTART), BM_GETCHECK, 0, 0);
            Config_AutoStart(chk == BST_CHECKED ? 1 : 0);
            Config_Save();
            SetWindowTextW(g_hStatus, (chk==BST_CHECKED)?L"\x2714 Auto-start":L"● Listening");
            return 0; }
        /* 颜色按钮 → ChooseColor */
        if ((cid == CID_GLOW_COLOR || cid == CID_INNER_COLOR || cid == CID_ZONE_COLOR)
            && code == BN_CLICKED) {
            static COLORREF cust[16] = {0};
            COLORREF *pClr = NULL;
            HWND hBtn = GetDlgItem(hWnd, (int)cid);
            if (cid == CID_GLOW_COLOR)  pClr = &g_glowColor;
            if (cid == CID_INNER_COLOR) pClr = &g_innerColor;
            if (cid == CID_ZONE_COLOR)  pClr = &g_zoneColor;
            if (pClr) {
                CHOOSECOLORW cc = {0};
                cc.lStructSize = sizeof(cc); cc.hwndOwner = hWnd;
                cc.rgbResult = *pClr; cc.lpCustColors = cust;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    *pClr = cc.rgbResult;
                    InvalidateRect(hBtn, NULL, TRUE);
                }
            }
            return 0;
        }
        /* Save */
        if (cid == 5001 && code == BN_CLICKED) {
            /* Help 弹窗（可滚动） */
            if (g_hHelpDlg) { SetForegroundWindow(g_hHelpDlg); return 0; }
            { static BOOL helpReg = FALSE;
              if (!helpReg) {
                WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(wc);
                wc.lpfnWndProc = HelpDlgProc; wc.hInstance = g_hInst;
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
                wc.lpszClassName = L"SC_HelpDlg";
                RegisterClassExW(&wc); helpReg = TRUE;
              }
            }
            int w = SCALE(488), h = SCALE(420);
            g_hHelpDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_HelpDlg",
                L"ShortCutter - Help",
                WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
                (GetSystemMetrics(SM_CXSCREEN)-w)/2,
                (GetSystemMetrics(SM_CYSCREEN)-h)/2,
                w, h, hWnd, NULL, g_hInst, NULL);
            return 0; }
        if (cid == CID_BTN_SAVE && code == BN_CLICKED) {
            wchar_t tbuf[32];
            GetWindowTextW(GetDlgItem(hWnd,3002), tbuf, 32);
            int t = _wtoi(tbuf); if (t >= 10) g_cfg.minGesture = t;
            GetWindowTextW(GetDlgItem(hWnd,3003), tbuf, 32);
            t = _wtoi(tbuf);
            if (t >= 50 && t <= 95) g_cfg.matchThreshold = t;
            g_cfg.mouseButton = (int)SendMessageW(g_hMouseBtn, CB_GETCURSEL, 0, 0) + 1;
            g_cfg.enabled = g_enabled;
            g_cfg.dashboardOpacity = (int)SendMessageW(g_hDashOpacity, TBM_GETPOS, 0, 0);
            if (g_cfg.dashboardOpacity < 20) g_cfg.dashboardOpacity = 20;
            if (g_cfg.dashboardOpacity > 100) g_cfg.dashboardOpacity = 100;
            Dashboard_ApplyOpacity();
            g_cfg.autoStart = (SendMessageW(GetDlgItem(hWnd,CID_AUTOSTART),BM_GETCHECK,0,0)==BST_CHECKED);
            g_cfg.trailEnabled = (SendMessageW(GetDlgItem(hWnd,3004),BM_GETCHECK,0,0)==BST_CHECKED);
            g_cfg.zoneLinesEnabled = (SendMessageW(GetDlgItem(hWnd,3005),BM_GETCHECK,0,0)==BST_CHECKED);
            /* 外观颜色 */
            g_cfg.trailGlowColor  = (int)g_glowColor;
            g_cfg.trailInnerColor = (int)g_innerColor;
            g_cfg.zoneLineColor   = (int)g_zoneColor;
            /* 外观宽度（从 EDIT 读取，clamp 0-15） */
            { wchar_t tbuf[8]; int v;
              GetWindowTextW(GetDlgItem(hWnd,CID_GLOW_WIDTH), tbuf, 8);
              v = _wtoi(tbuf); if (v < 0) v = 0; if (v > 15) v = 15;
              g_cfg.trailGlowWidth = v;
              GetWindowTextW(GetDlgItem(hWnd,CID_INNER_WIDTH), tbuf, 8);
              v = _wtoi(tbuf); if (v < 1) v = 1; if (v > 15) v = 15;
              g_cfg.trailInnerWidth = v;
              GetWindowTextW(GetDlgItem(hWnd,CID_ZONE_WIDTH), tbuf, 8);
              v = _wtoi(tbuf); if (v < 1) v = 1; if (v > 15) v = 15;
              g_cfg.zoneLineWidth = v; }
            SetWindowTextW(g_hStatus, L"✓ Saved");
            PostMessageW(hWnd, WM_SC_DEFER_SAVE, 0, 0);
            return 0; }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT ds = (LPDRAWITEMSTRUCT)lp;
        COLORREF clr = 0;
        switch (ds->CtlID) {
            case CID_GLOW_COLOR:  clr = g_glowColor;  break;
            case CID_INNER_COLOR: clr = g_innerColor; break;
            case CID_ZONE_COLOR:  clr = g_zoneColor;  break;
            default: return DefWindowProcW(hWnd, msg, wp, lp);
        }
        /* 按钮边框 */
        UINT df = DFCS_BUTTONPUSH;
        if (ds->itemState & ODS_SELECTED) df |= DFCS_PUSHED;
        DrawFrameControl(ds->hDC, &ds->rcItem, DFC_BUTTON, df);
        /* 颜色色块（内缩 3px） */
        { HBRUSH hbr = CreateSolidBrush(clr);
          HPEN   hpn = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
          RECT rc = ds->rcItem;
          InflateRect(&rc, -3, -3);
          if (ds->itemState & ODS_SELECTED) OffsetRect(&rc, 1, 1);
          HBRUSH oldBr = SelectObject(ds->hDC, hbr);
          HPEN   oldPn = SelectObject(ds->hDC, hpn);
          Rectangle(ds->hDC, rc.left, rc.top, rc.right, rc.bottom);
          SelectObject(ds->hDC, oldBr);
          SelectObject(ds->hDC, oldPn);
          DeleteObject(hpn); DeleteObject(hbr); }
        /* 焦点框 */
        if (ds->itemState & ODS_FOCUS) {
            RECT fr = ds->rcItem; InflateRect(&fr, -2, -2);
            DrawFocusRect(ds->hDC, &fr); }
        return TRUE;
    }

    case WM_CTLCOLOREDIT: {
        int cid = GetDlgCtrlID((HWND)lp);
        if (UseWhiteBg(cid)) { SetBkColor((HDC)wp, RGB(255,255,255)); return (LRESULT)g_hWhiteBr; }
        break;
    }
    case WM_CTLCOLORBTN: {
        int cid = GetDlgCtrlID((HWND)lp);
        if (UseWhiteBg(cid)) { SetBkColor((HDC)wp, RGB(255,255,255)); return (LRESULT)g_hWhiteBr; }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        g_hDlg = NULL;
        g_hDashOpacity = NULL;
        g_hDashOpacityValue = NULL;
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
        if (g_hWhiteBr) { DeleteObject(g_hWhiteBr); g_hWhiteBr = NULL; }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ══════════ 注册 + 显示 ═══════════════════════ */
static BOOL g_sClassRegistered = FALSE;
static void UI_RegisterClass(void)
{
    if (g_sClassRegistered) return;
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW); wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = g_hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ShortCutterSettings";
    RegisterClassExW(&wc);
    g_sClassRegistered = TRUE;
}


static int S(int v) { return MulDiv(v, g_dpiScale, 96); }

void UI_Show(HWND hParent)
{
    (void)hParent;
    if (g_hDlg) { SetForegroundWindow(g_hDlg); ShowWindow(g_hDlg, SW_SHOW); return; }
    UI_RegisterClass();
    int w = S(SETS_W), h = S(SETS_H);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g_hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ShortCutterSettings",
        L"ShortCutter - Settings", WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, w, h, NULL, NULL, g_hInst, NULL);
    if (g_hWnd) SetWindowLongPtrW(g_hDlg, GWLP_HWNDPARENT, (LONG_PTR)g_hWnd);
}
