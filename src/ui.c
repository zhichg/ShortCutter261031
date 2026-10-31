#include "main.h"
#include <stdlib.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <errno.h>
#include "key_record.h"

/* ── 设置窗口客户区尺寸 ────────────────────── */
#define SETS_W  SC_SETTINGS_MIN_WIDTH
#define SETS_H  SC_SETTINGS_MIN_HEIGHT
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
#define CID_CANCEL      4001
#define CID_LIST_SEARCH 5003
#define CID_LIST_DETAIL 5004
#define CID_DASH_COMPACT 3048
#define SETTINGS_VIEW_TIMER 4022
#define CID_CMD_DLG_ARGS 5106
#define CID_CMD_DLG_DIR 5107
#define CID_CMD_DLG_ACTIVATE 5108
#define CID_CMD_DLG_CONFIRM 5109
#define CID_CMD_DLG_ADMIN 5110
#define CID_CMD_DLG_HINT 5111
#define CID_CMD_DLG_RECORD 5115
#define CID_CMD_DLG_CLEAR 5116
#define WM_SC_KEY_PREVIEW (WM_APP + 202)
#define CMD_RECORD_TIMER 4023

/* 外观自定义控件 ID */
#define CID_ZONE_COLOR   3020
#define CID_ZONE_WIDTH   3021
#define CID_GLOW_COLOR   3022
#define CID_GLOW_WIDTH   3023
#define CID_INNER_COLOR  3024
#define CID_INNER_WIDTH  3025
#define CID_DASH_OPACITY 3033
#define CID_DASH_OPACITY_VALUE 3034
#define CID_DASH_THEME 3036
#define CID_DASH_QUICK 3038
#define CID_DASH_QUICK_DESKTOP 3039
#define CID_DASH_QUICK_EXPLORER 3040
#define CID_DASH_QUICK_HINT 3041
#define CID_APPEAR_COLOR 3042
#define CID_APPEAR_WIDTH 3043
#define CID_MIN_SIZE_UNIT 3044
#define CID_DASH_QUICK_SCOPE 3045
#define CID_LANGUAGE_LABEL 3046
#define CID_LANGUAGE 3047
#define CID_GROUP_STARTUP 3050
#define CID_GROUP_RECOGNITION 3051
#define CID_GROUP_GESTURE_APPEAR 3052
#define CID_GROUP_DASH_APPEAR 3053
#define CID_GROUP_DASH_QUICK 3054

enum { TAB_GENERAL, TAB_DASHBOARD, TAB_COMMANDS, TAB_GESTURES, TAB_HOTKEYS };

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
static HWND g_hDashTheme;
static HFONT g_hFont;
static HBRUSH g_hWhiteBr = NULL;
static int  g_activeTab = 0;
static COLORREF g_zoneColor, g_glowColor, g_innerColor;
static AppConfig g_uiDraft;
static int g_uiEditing;
static int g_settingsDirty;
static int g_commandSortColumn, g_commandSortDescending;
static int g_settingsWindowWidth, g_settingsWindowHeight, g_windowSizeSaveFailed;
static int g_settingsColumnBaseline[SC_SETTINGS_LIST_COUNT][SC_SETTINGS_COLUMN_COUNT];
static int g_settingsColumnsReady, g_settingsRestoringColumns;
static void Settings_CaptureColumns(void);

static AppConfig *UI_ActualConfig(void) { return &g_cfg; }
static AppConfig *UI_Config(void) { return g_uiEditing ? &g_uiDraft : &g_cfg; }
#define g_cfg (*UI_Config())
static const wchar_t *UI_Text(const wchar_t *english, const wchar_t *chinese)
{ return g_cfg.uiLanguage == UI_LANG_CHINESE ? chinese : english; }
#define SC_Text UI_Text
int UI_GetLanguage(void) { return g_cfg.uiLanguage; }

static HFONT UI_CreateFont(void)
{
    return CreateFontW(-MulDiv(9,g_dpiScale,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        FF_DONTCARE,L"Microsoft YaHei UI");
}
static BOOL CALLBACK UI_SetFont(HWND child, LPARAM font)
{ SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE); return TRUE; }
static void UI_SetUtf8(HWND control, const char *text)
{
    wchar_t wide[SC_PATH_CAP];
    SC_WideFromUtf8(wide,SC_PATH_CAP,text);
    SetWindowTextW(control,wide);
}
static int UI_ReadUtf8(HWND parent, HWND control, char *out, size_t cap)
{
    int length = GetWindowTextLengthW(control);
    wchar_t *text = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,((size_t)length+1)*sizeof(wchar_t));
    if (!text) return 0;
    GetWindowTextW(control,text,length+1);
    int ok = SC_Utf8FromWide(out,cap,text);
    HeapFree(GetProcessHeap(),0,text);
    if (!ok) {
        MessageBoxW(parent,SC_Text(L"This text is too long or invalid. Please shorten it.",L"内容过长或包含无效字符，请缩短后再试。"),L"ShortCutter",MB_OK|MB_ICONWARNING);
        SetFocus(control);
    }
    return ok;
}
static int UI_FindCommand(int id)
{ for (int i=0;i<g_cfg.commandCount;i++) if (g_cfg.commands[i].id==id) return i; return -1; }
static void UI_Dirty(void)
{
    g_settingsDirty=1;
    if (g_hStatus) SetWindowTextW(g_hStatus,SC_Text(L"Changes not applied",L"更改尚未应用"));
}
static void UI_CenterOwned(HWND window, HWND parent)
{
    RECT rect; GetWindowRect(window,&rect);
    HMONITOR monitor=MonitorFromWindow(parent?parent:window,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info={0}; info.cbSize=sizeof(info);
    if (!GetMonitorInfoW(monitor,&info)) return;
    int w=rect.right-rect.left,h=rect.bottom-rect.top;
    if (w>info.rcWork.right-info.rcWork.left) w=info.rcWork.right-info.rcWork.left;
    if (h>info.rcWork.bottom-info.rcWork.top) h=info.rcWork.bottom-info.rcWork.top;
    SetWindowPos(window,NULL,info.rcWork.left+(info.rcWork.right-info.rcWork.left-w)/2,
        info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-h)/2,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
}

static void UI_ScaleDialog(HWND window,WPARAM wp,LPARAM lp)
{
    int previous=g_dpiScale;g_dpiScale=HIWORD(wp);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        RECT r;GetWindowRect(child,&r);MapWindowPoints(NULL,window,(POINT*)&r,2);
        int height=MulDiv(r.bottom-r.top,g_dpiScale,previous);wchar_t kind[32];GetClassNameW(child,kind,32);
        if(!wcscmp(kind,L"ComboBox"))height=SCALE(180);
        MoveWindow(child,MulDiv(r.left,g_dpiScale,previous),MulDiv(r.top,g_dpiScale,previous),MulDiv(r.right-r.left,g_dpiScale,previous),height,FALSE);
    }
    HFONT old=(HFONT)GetWindowLongPtrW(window,GWLP_USERDATA),font=UI_CreateFont();SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)font);EnumChildWindows(window,UI_SetFont,(LPARAM)font);if(old)DeleteObject(old);
    RECT *target=(RECT*)lp;SetWindowPos(window,NULL,target->left,target->top,target->right-target->left,target->bottom-target->top,SWP_NOZORDER|SWP_NOACTIVATE);
}

static BOOL UseWhiteBg(int cid) /* 白底控件 ID */
{
    if (cid >= 3000 && cid <= 3047) return TRUE; /* Settings fields */
    if (cid == 2550 || cid == 2700) return TRUE;
    if (cid == 5005 || cid == 5009) return TRUE; /* Commands: Type, Group */
    if (cid == 5006) return TRUE;                 /* Gestures: Zone */
    return FALSE;
}


/* ── 修饰键文字 ────────────────────────────── */
static const wchar_t *ModToStr(int mod)
{
    switch (mod) {
        case 0: return L"-";
        case 1: return L"Ctrl";
        case 2: return L"Alt";
        case 4: return L"Shift";
        case 8: return L"Win";
        case 3: return L"Ctrl+Alt";
        case 5: return L"Ctrl+Shift";
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
    if (cmdId == CMDID_SETTINGS) { strcpy(out, "Settings"); return; }
    if (cmdId == CMDID_TOGGLE) { strcpy(out, "Toggle"); return; }
    if (cmdId == CMDID_CMDPROMPT) { strcpy(out, "Cmd"); return; }
    if (cmdId == CMDID_POWERSHELL) { strcpy(out, "PS"); return; }
    if (cmdId == CMDID_DASHBOARD) { strcpy(out, "Dashboard"); return; }
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (g_cfg.commands[i].id == cmdId)
            { SC_CopyUtf8(out,(size_t)outMax,g_cfg.commands[i].name); return; }
}

/* ══════════ 列表填充函数 ══════════════════════ */
static int g_cmdFilter = 0; /* 0=All 1=Custom 2=Built-in 3=Scripts */
static int g_cmdGrpFilter = 0; /* 0=All, 1+ = index into g_cmdGrpNames */
static char g_cmdGrpNames[MAX_COMMANDS][SC_GROUP_CAP]; /* 收集的所有 group 名 */
static int  g_cmdGrpCount = 0;
static int g_gesFilter = 0; /* 0=All gestures; otherwise zone + 1. */

static const wchar_t *ZoneName(int zone)
{
    const wchar_t *zn[ZONE_COUNT] = {
        SC_Text(L"Main", L"主区域"), SC_Text(L"↖ TL", L"↖ 左上"),
        SC_Text(L"↗ TR", L"↗ 右上"), SC_Text(L"↙ BL", L"↙ 左下"),
        SC_Text(L"↘ BR", L"↘ 右下"), SC_Text(L"All areas", L"全部区域")
    };
    if (zone < 0 || zone >= ZONE_COUNT) return zn[ZONE_MAIN];
    return zn[zone];
}

/* 根据当前筛选，将 ListView 行号映射回 commands[] 索引 */
static wchar_t g_listQuery[SC_NAME_CAP];
static char g_selectedGroup[SC_GROUP_CAP];
static int UI_Contains(const wchar_t *text,const wchar_t *query)
{
    size_t n=wcslen(query);
    if (!n) return 1;
    for (;*text;text++) if (!_wcsnicmp(text,query,n)) return 1;
    return 0;
}
static int UI_QueryMatches(const char *name,const char *extra)
{
    wchar_t text[SC_PATH_CAP];
    if (!g_listQuery[0]) return 1;
    SC_WideFromUtf8(text,SC_PATH_CAP,name);
    if (UI_Contains(text,g_listQuery)) return 1;
    SC_WideFromUtf8(text,SC_PATH_CAP,extra?extra:"");
    return UI_Contains(text,g_listQuery);
}
static const wchar_t *UI_GroupLabel(const char *group,wchar_t *buffer,size_t count)
{
    if (!group[0] || !lstrcmpiA(group,"None")) return SC_Text(L"Ungrouped",L"未分组");
    if (!lstrcmpiA(group,"Built-in:Keys")) return SC_Text(L"Built-in keys",L"内置按键");
    if (!lstrcmpiA(group,"Built-in:Run")) return SC_Text(L"Built-in apps",L"内置应用");
    if (!lstrcmpiA(group,"Scripts")) return SC_Text(L"Scripts",L"脚本");
    SC_WideFromUtf8(buffer,count,group); return buffer;
}
static void UI_ListText(HWND list,int row,int column,LPWSTR text)
{LVITEMW item={0};item.iSubItem=column;item.pszText=text;SendMessageW(list,LVM_SETITEMTEXTW,row,(LPARAM)&item);}
static int UI_ListData(HWND list,int row)
{
    LVITEMW item={0}; item.mask=LVIF_PARAM; item.iItem=row;
    return row>=0 && SendMessageW(list,LVM_GETITEMW,0,(LPARAM)&item)?(int)item.lParam:-1;
}
static int CmdListRowToIdx(int row) { return UI_FindCommand(UI_ListData(g_hCmdList,row)); }
static int GesListRowToIdx(int row) { return UI_ListData(g_hGesList,row); }
static int HkListRowToIdx(int row) { return UI_ListData(g_hHkList,row); }
static int CALLBACK UI_CommandCompare(LPARAM left,LPARAM right,LPARAM context)
{
    (void)context;
    int a=UI_FindCommand((int)left),b=UI_FindCommand((int)right);
    if (a<0 || b<0) return 0;
    Command *x=&g_cfg.commands[a],*y=&g_cfg.commands[b];
    int r=0;
    switch (g_commandSortColumn) {
    case 1:r=(int)x->type-(int)y->type;break;
    case 2:r=lstrcmpiA(x->group,y->group);break;
    case 3:r=lstrcmpiA(x->runPath,y->runPath);break;
    case 4:r=x->locked-y->locked;break;
    default:r=lstrcmpiA(x->name,y->name);break;
    }
    return g_commandSortDescending?-r:r;
}
static void CmdList_Fill(void)
{
    SendMessageW(g_hCmdList,WM_SETREDRAW,FALSE,0);
    ListView_DeleteAllItems(g_hCmdList);
    g_cmdGrpCount=0;
    for (int i=0;i<g_cfg.commandCount && g_cmdGrpCount<MAX_COMMANDS;i++) {
        const char *group=g_cfg.commands[i].group;
        if (!group[0]) continue;
        int duplicate=0;
        for (int j=0;j<g_cmdGrpCount;j++) if (!lstrcmpiA(group,g_cmdGrpNames[j])) duplicate=1;
        if (!duplicate) SC_CopyUtf8(g_cmdGrpNames[g_cmdGrpCount++],SC_GROUP_CAP,group);
    }
    HWND combo=GetDlgItem(GetParent(g_hCmdList),5008);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)SC_Text(L"All",L"全部"));
    g_cmdGrpFilter=0;
    for (int i=0;i<g_cmdGrpCount;i++) {
        wchar_t group[SC_GROUP_CAP];
        SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)UI_GroupLabel(g_cmdGrpNames[i],group,SC_GROUP_CAP));
        if (g_selectedGroup[0] && !lstrcmpiA(g_selectedGroup,g_cmdGrpNames[i])) g_cmdGrpFilter=i+1;
    }
    if (!g_cmdGrpFilter) g_selectedGroup[0]=0;
    SendMessageW(combo,CB_SETCURSEL,g_cmdGrpFilter,0);
    int row=0;
    for (int i=0;i<g_cfg.commandCount;i++) {
        Command *c=&g_cfg.commands[i];
        if (g_cmdFilter==1 && (c->locked || c->type==CMD_SCRIPT)) continue;
        if (g_cmdFilter==2 && (!c->locked || c->type==CMD_SCRIPT)) continue;
        if (g_cmdFilter==3 && c->type!=CMD_SCRIPT) continue;
        if (g_selectedGroup[0] && lstrcmpiA(c->group,g_selectedGroup)) continue;
        if (!UI_QueryMatches(c->name,c->runPath)) continue;
        wchar_t name[SC_NAME_CAP],group[SC_GROUP_CAP],target[SC_PATH_CAP];
        SC_WideFromUtf8(name,SC_NAME_CAP,c->name);
        SC_WideFromUtf8(target,SC_PATH_CAP,c->type==CMD_KEYS?c->keySeq:c->runPath);
        LVITEMW item={0}; item.mask=LVIF_TEXT|LVIF_PARAM; item.iItem=row; item.pszText=name; item.lParam=c->id;
        SendMessageW(g_hCmdList,LVM_INSERTITEMW,0,(LPARAM)&item);
        UI_ListText(g_hCmdList,row,1,(LPWSTR)(c->type==CMD_KEYS?SC_Text(L"Keys",L"按键"):c->type==CMD_SCRIPT?SC_Text(L"Script",L"脚本"):SC_Text(L"Run",L"运行")));
        UI_ListText(g_hCmdList,row,2,(LPWSTR)UI_GroupLabel(c->group,group,SC_GROUP_CAP));
        UI_ListText(g_hCmdList,row,3,target);
        UI_ListText(g_hCmdList,row,4,(LPWSTR)(c->type==CMD_SCRIPT?SC_Text(L"Script folder",L"脚本目录"):c->locked?SC_Text(L"Built-in",L"内置"):SC_Text(L"Custom",L"自定义")));
        row++;
    }
    ListView_SortItems(g_hCmdList,UI_CommandCompare,0);
    SendMessageW(g_hCmdList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(g_hCmdList,NULL,TRUE);
}
static void UI_ChainArrows(const char *raw,wchar_t *out,size_t cap)
{
    const wchar_t *arrows[]={L"↑",L"↓",L"←",L"→",L"↗",L"↘",L"↙",L"↖"};
    out[0]=0;
    for (int i=0;raw[i] && wcslen(out)+3<cap;i++) {
        int dir=raw[i]-'0'; if (dir<0 || dir>7) continue;
        if (out[0]) wcscat(out,L" ");
        wcscat(out,arrows[dir]);
    }
}
static HIMAGELIST g_gestureImages;
static void UI_GestureThumbnails(void)
{
    int size=SCALE(24);HIMAGELIST images=ImageList_Create(size,size,ILC_COLOR32|ILC_MASK,MAX_GESTURES,1);
    HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);
    for(int i=0;i<g_cfg.gestureCount && images;i++){
        HBITMAP bitmap=CreateCompatibleBitmap(screen,size,size),old=SelectObject(dc,bitmap);
        RECT rect={0,0,size,size};FillRect(dc,&rect,GetSysColorBrush(COLOR_WINDOW));
        POINT points[64]={{0,0}};int count=1,minX=0,maxX=0,minY=0,maxY=0;
        const int dx[]={0,0,-1,1,1,1,-1,-1},dy[]={-1,1,0,0,-1,1,1,-1};
        const char *chain=g_cfg.gestures[i].chains[0];
        for(int n=0;chain[n] && count<64;n++){int d=chain[n]-'0';if(d<0 || d>7)continue;points[count].x=points[count-1].x+dx[d];points[count].y=points[count-1].y+dy[d];if(points[count].x<minX)minX=points[count].x;if(points[count].x>maxX)maxX=points[count].x;if(points[count].y<minY)minY=points[count].y;if(points[count].y>maxY)maxY=points[count].y;count++;}
        int span=maxX-minX;if(maxY-minY>span)span=maxY-minY;if(span<1)span=1;
        for(int n=0;n<count;n++){points[n].x=SCALE(3)+MulDiv(points[n].x-minX,size-SCALE(6),span);points[n].y=SCALE(3)+MulDiv(points[n].y-minY,size-SCALE(6),span);}
        HPEN pen=CreatePen(PS_SOLID,SCALE(2),RGB(0,110,210)),oldPen=SelectObject(dc,pen);if(count>1)Polyline(dc,points,count);SelectObject(dc,oldPen);DeleteObject(pen);
        SelectObject(dc,old);ImageList_AddMasked(images,bitmap,GetSysColor(COLOR_WINDOW));DeleteObject(bitmap);
    }
    DeleteDC(dc);ReleaseDC(NULL,screen);ListView_SetImageList(g_hGesList,images,LVSIL_SMALL);if(g_gestureImages)ImageList_Destroy(g_gestureImages);g_gestureImages=images;
}
static void GesList_Fill(void)
{
    ListView_DeleteAllItems(g_hGesList);
    UI_GestureThumbnails();
    int row=0;
    for (int i=0;i<g_cfg.gestureCount;i++) {
        GestureDef *g=&g_cfg.gestures[i];
        if (g_gesFilter && g->zone!=g_gesFilter-1) continue;
        char command[SC_NAME_CAP]; CmdIdToName(g->commandId,command,sizeof(command));
        if (!UI_QueryMatches(g->name,command)) continue;
        wchar_t name[SC_NAME_CAP],chain[192],cmd[SC_NAME_CAP];
        SC_WideFromUtf8(name,SC_NAME_CAP,g->name); UI_ChainArrows(g->chains[0],chain,192);
        SC_WideFromUtf8(cmd,SC_NAME_CAP,command);
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM|LVIF_IMAGE;item.iItem=row;item.pszText=name;item.lParam=i;item.iImage=i;
        SendMessageW(g_hGesList,LVM_INSERTITEMW,0,(LPARAM)&item);
        UI_ListText(g_hGesList,row,1,chain);
        UI_ListText(g_hGesList,row,2,(LPWSTR)ModToStr(g->modifier));
        UI_ListText(g_hGesList,row,3,(LPWSTR)ZoneName(g->zone));
        UI_ListText(g_hGesList,row,4,cmd);
        UI_ListText(g_hGesList,row,5,(LPWSTR)(g->enabled?SC_Text(L"Yes",L"是"):SC_Text(L"No",L"否")));
        row++;
    }
}
static void HkList_Fill(void)
{
    ListView_DeleteAllItems(g_hHkList); int row=0;
    for (int i=0;i<g_cfg.hotkeyCount;i++) {
        HotkeyDef *h=&g_cfg.hotkeys[i];
        char command[SC_NAME_CAP];CmdIdToName(h->commandId,command,sizeof(command));
        if (!UI_QueryMatches(h->description,command) && !UI_QueryMatches(h->label,NULL)) continue;
        wchar_t label[SC_LABEL_CAP],desc[SC_NAME_CAP],cmd[SC_NAME_CAP];
        SC_WideFromUtf8(label,SC_LABEL_CAP,h->label);SC_WideFromUtf8(desc,SC_NAME_CAP,h->description);SC_WideFromUtf8(cmd,SC_NAME_CAP,command);
        if (!label[0]) lstrcpyW(label,SC_Text(L"Unbound",L"未绑定"));
        if (!cmd[0]) lstrcpyW(cmd,SC_Text(L"Missing command",L"命令已移除"));
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=row;item.pszText=label;item.lParam=i;
        SendMessageW(g_hHkList,LVM_INSERTITEMW,0,(LPARAM)&item);
        UI_ListText(g_hHkList,row,1,desc);UI_ListText(g_hHkList,row,2,cmd);
        UI_ListText(g_hHkList,row,3,(LPWSTR)(h->enabled?SC_Text(L"Yes",L"是"):SC_Text(L"No",L"否")));row++;
    }
}
static void UI_FillCommandChoice(HWND combo,int selected)
{
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    const int ids[]={CMDID_DASHBOARD,CMDID_SETTINGS,CMDID_TOGGLE,CMDID_CMDPROMPT,CMDID_POWERSHELL};
    const wchar_t *names[]={SC_Text(L"Dashboard",L"仪表盘"),SC_Text(L"Settings",L"设置"),SC_Text(L"Enable / pause",L"启用 / 暂停"),L"Command Prompt",L"PowerShell"};
    int selectedRow=-1;
    for (int i=0;i<5;i++) {int r=(int)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)names[i]);SendMessageW(combo,CB_SETITEMDATA,r,ids[i]);if(ids[i]==selected)selectedRow=r;}
    for (int i=0;i<g_cfg.commandCount;i++) {
        wchar_t name[SC_NAME_CAP],group[SC_GROUP_CAP],display[SC_NAME_CAP+SC_GROUP_CAP+32];
        Command *c=&g_cfg.commands[i];SC_WideFromUtf8(name,SC_NAME_CAP,c->name);
        swprintf(display,sizeof(display)/sizeof(*display),L"%ls  ·  %ls  (#%d)",name,UI_GroupLabel(c->group,group,SC_GROUP_CAP),c->id);
        int r=(int)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)display);SendMessageW(combo,CB_SETITEMDATA,r,c->id);if(c->id==selected)selectedRow=r;
    }
    SendMessageW(combo,CB_SETCURSEL,selectedRow,0);
}
static int UI_ChoiceCommand(HWND parent,HWND combo)
{
    int r=(int)SendMessageW(combo,CB_GETCURSEL,0,0);
    if (r>=0) return (int)SendMessageW(combo,CB_GETITEMDATA,r,0);
    char name[SC_NAME_CAP];
    if (!UI_ReadUtf8(parent,combo,name,sizeof(name))) return CMDID_NONE;
    int id=CmdNameToId(name);
    if (!id) {MessageBoxW(parent,SC_Text(L"Choose an existing command from the list.",L"请从列表中选择已有命令。"),L"ShortCutter",MB_OK|MB_ICONWARNING);SetFocus(combo);}
    return id;
}
static int UI_HotkeyConflict(BYTE mods,BYTE vk,int skip)
{
    if (!vk) return 0;
    if (g_cfg.toggleVk==vk && g_cfg.toggleMods==mods) return 1;
    for(int i=0;i<g_cfg.hotkeyCount;i++) if(i!=skip && g_cfg.hotkeys[i].enabled && g_cfg.hotkeys[i].vk==vk && g_cfg.hotkeys[i].mods==mods) return 1;
    return 0;
}
static int UI_BeginEditor(HWND parent)
{
    if (parent && !IsWindowEnabled(parent)) {HWND popup=GetWindow(parent,GW_ENABLEDPOPUP);if(popup)SetForegroundWindow(popup);return 0;}
    return 1;
}
static void UI_EndEditor(HWND window)
{
    HWND parent=GetParent(window);if(parent && IsWindow(parent)){EnableWindow(parent,TRUE);SetForegroundWindow(parent);}
}

/* Command edits resolve a stable ID at commit; the parent is disabled while editing. */
static HWND g_hCmdDlg,g_hCmdDlgName,g_hCmdDlgType,g_hCmdDlgPath,g_hCmdDlgKeys,g_hCmdDlgGroup;
static int g_cmdEditIdx=-1,g_cmdEditId;
static char g_cmdKeySeq[SC_KEYS_CAP];
static KeyRecording g_cmdRecording;
static HHOOK g_cmdRecordHook;
static BYTE g_cmdRecordIgnored[256];
static int g_cmdRecordFull, g_cmdPreviewPending;

static void Cmd_RecordPreview(void)
{
    wchar_t preview[SC_KEYS_CAP*8];
    const char *sequence=g_cmdRecordHook?g_cmdRecording.sequence:g_cmdKeySeq;
    Action_FormatKeySeq(sequence,preview,sizeof(preview)/sizeof(preview[0]));
    SetWindowTextW(g_hCmdDlgKeys,preview);
    if (g_cmdRecordHook) {
        SendMessageW(g_hCmdDlgKeys,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        SendMessageW(g_hCmdDlgKeys,EM_SCROLLCARET,0,0);
    }
}

static void Cmd_RecordStop(void)
{
    if (!g_cmdRecordHook) return;
    UnhookWindowsHookEx(g_cmdRecordHook);g_cmdRecordHook=NULL;
    KillTimer(g_hCmdDlg,CMD_RECORD_TIMER);
    KeyRecord_Finish(&g_cmdRecording);
    /* Clicking Record and immediately stopping keeps the previous mapping. */
    if (g_cmdRecording.hasEvent) SC_CopyUtf8(g_cmdKeySeq,sizeof(g_cmdKeySeq),g_cmdRecording.sequence);
    const int ids[]={CID_CMD_DLG_NAME,CID_CMD_DLG_TYPE,CID_CMD_DLG_GROUP,CID_CMD_DLG_CLEAR,101};
    for (size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++) EnableWindow(GetDlgItem(g_hCmdDlg,ids[i]),TRUE);
    SetWindowTextW(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD),SC_Text(L"Start recording",L"开始录制"));
    SetWindowTextW(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_HINT),g_cmdRecordFull?
        SC_Text(L"Recording limit reached. The captured keys have been kept.",L"已达录制上限，已保留录制的按键。"):
        SC_Text(L"Click Start recording, then press your keys. Click Stop recording to finish. Recording again replaces this sequence.",L"点击“开始录制”后按下所需按键，点击“停止录制”结束。重新录制会替换当前序列。"));
    Cmd_RecordPreview();
}

static LRESULT CALLBACK Cmd_RecordHook(int code,WPARAM message,LPARAM param)
{
    if (code!=HC_ACTION || !g_cmdRecordHook) return CallNextHookEx(NULL,code,message,param);
    KBDLLHOOKSTRUCT *key=(KBDLLHOOKSTRUCT*)param;
    if (key->vkCode>=256 || key->flags&LLKHF_INJECTED) return CallNextHookEx(NULL,code,message,param);
    if (GetForegroundWindow()!=g_hCmdDlg) {
        /* Stop before letting input reach another application. */
        Cmd_RecordStop();return CallNextHookEx(NULL,code,message,param);
    }
    int up=message==WM_KEYUP || message==WM_SYSKEYUP;
    if (!up && message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN) return CallNextHookEx(NULL,code,message,param);
    if (g_cmdRecordIgnored[key->vkCode]) {
        if (up) {g_cmdRecordIgnored[key->vkCode]=0;return CallNextHookEx(NULL,code,message,param);}
        return 1;
    }
    if (!g_cmdRecordFull && !KeyRecord_Event(&g_cmdRecording,key->vkCode,
            !!(key->flags&LLKHF_EXTENDED),up,key->time)) g_cmdRecordFull=1;
    /* Keep the hook quick; render the preview outside the callback. */
    if (!g_cmdPreviewPending) g_cmdPreviewPending=PostMessageW(g_hCmdDlg,WM_SC_KEY_PREVIEW,0,0)!=0;
    return 1;
}

static void Cmd_RecordStart(void)
{
    KeyRecord_Reset(&g_cmdRecording);g_cmdRecordFull=0;g_cmdPreviewPending=0;
    for (int vk=0;vk<256;vk++) g_cmdRecordIgnored[vk]=!!(GetAsyncKeyState(vk)&0x8000);
    g_cmdRecordHook=SetWindowsHookExW(WH_KEYBOARD_LL,Cmd_RecordHook,g_hInst,0);
    if (!g_cmdRecordHook) {
        MessageBoxW(g_hCmdDlg,SC_Text(L"Could not start recording. Please try again.",L"无法开始录制，请重试。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return;
    }
    const int ids[]={CID_CMD_DLG_NAME,CID_CMD_DLG_TYPE,CID_CMD_DLG_GROUP,CID_CMD_DLG_CLEAR,101};
    for (size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++) EnableWindow(GetDlgItem(g_hCmdDlg,ids[i]),FALSE);
    SetWindowTextW(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD),SC_Text(L"Stop recording",L"停止录制"));
    SetWindowTextW(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_HINT),SC_Text(
        L"Recording… Press keys, then click Stop recording. Keyboard shortcuts are captured here. Switching windows stops recording.",
        L"正在录制…请按键，完成后用鼠标点击“停止录制”。快捷键会被此窗口捕获；切换窗口会自动停止。"));
    SetFocus(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD));
    SetTimer(g_hCmdDlg,CMD_RECORD_TIMER,100,NULL);Cmd_RecordPreview();
}

static void CmdDlg_UpdateFields(void)
{
    int type=(int)SendMessageW(g_hCmdDlgType,CB_GETCURSEL,0,0);
    const int runIds[]={CID_CMD_DLG_PATH,CID_CMD_DLG_BROWSE,CID_CMD_DLG_ARGS,CID_CMD_DLG_DIR,5112,5113,5114};
    for(int i=0;i<7;i++)ShowWindow(GetDlgItem(g_hCmdDlg,runIds[i]),type==CMD_KEYS?SW_HIDE:SW_SHOW);
    ShowWindow(g_hCmdDlgKeys,type==CMD_KEYS?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD),type==CMD_KEYS?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_CLEAR),type==CMD_KEYS?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_ACTIVATE),type==CMD_RUN?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_ADMIN),type==CMD_KEYS?SW_HIDE:SW_SHOW);
    SetWindowTextW(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_HINT),type==CMD_KEYS?
        SC_Text(L"Click Start recording, then press your keys. Click Stop recording to finish. Recording again replaces this sequence.",L"点击“开始录制”后按下所需按键，点击“停止录制”结束。重新录制会替换当前序列。"):
        SC_Text(L"Documents and links always open normally. Existing-instance activation applies to executable files only.",L"文档和链接始终正常打开。“激活已有窗口”仅用于可执行程序。"));
}
static HWND Cmd_Field(HWND parent,const wchar_t *kind,const wchar_t *caption,int id,int x,int y,int w,int h,DWORD extra)
{
    return CreateWindowExW(!wcscmp(kind,L"EDIT")?WS_EX_CLIENTEDGE:0,kind,caption,WS_CHILD|WS_VISIBLE|extra,SCALE(x),SCALE(y),SCALE(w),SCALE(h),parent,(HMENU)(INT_PTR)id,g_hInst,NULL);
}
static int UI_ValidFileName(const wchar_t *name)
{
    size_t n=wcslen(name);if(!n || name[n-1]==L'.' || name[n-1]==L' ' || wcspbrk(name,L"<>:\"/\\|?*"))return 0;
    for(const wchar_t *p=name;*p;p++)if(*p<32)return 0;
    wchar_t base[SC_NAME_CAP];lstrcpynW(base,name,SC_NAME_CAP);wchar_t *dot=wcschr(base,L'.');if(dot)*dot=0;
    if(!lstrcmpiW(base,L"CON")||!lstrcmpiW(base,L"PRN")||!lstrcmpiW(base,L"AUX")||!lstrcmpiW(base,L"NUL"))return 0;
    if((!_wcsnicmp(base,L"COM",3)||!_wcsnicmp(base,L"LPT",3))&&base[3]>=L'1'&&base[3]<=L'9'&&!base[4])return 0;
    return 1;
}
static LRESULT CALLBACK CmdDlgProc(HWND hWnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg){
    case WM_CREATE:{
        g_hCmdDlg=hWnd;
        HFONT font=UI_CreateFont();SetWindowLongPtrW(hWnd,GWLP_USERDATA,(LONG_PTR)font);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Name:",L"名称："),0,20,20,66,24,0);
        g_hCmdDlgName=Cmd_Field(hWnd,L"EDIT",L"",CID_CMD_DLG_NAME,92,18,410,24,WS_TABSTOP|ES_AUTOHSCROLL);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Type:",L"类型："),0,20,56,66,24,0);
        g_hCmdDlgType=Cmd_Field(hWnd,L"COMBOBOX",L"",CID_CMD_DLG_TYPE,92,54,98,160,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        const wchar_t *types[]={SC_Text(L"Run",L"运行"),SC_Text(L"Keys",L"按键"),SC_Text(L"Script",L"脚本")};
        for(int i=0;i<3;i++)SendMessageW(g_hCmdDlgType,CB_ADDSTRING,0,(LPARAM)types[i]);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Group:",L"分组："),0,212,56,62,24,0);
        g_hCmdDlgGroup=Cmd_Field(hWnd,L"COMBOBOX",L"",CID_CMD_DLG_GROUP,278,54,224,160,WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL);
        for(int i=0;i<g_cmdGrpCount;i++){wchar_t group[SC_GROUP_CAP];SC_WideFromUtf8(group,SC_GROUP_CAP,g_cmdGrpNames[i]);SendMessageW(g_hCmdDlgGroup,CB_ADDSTRING,0,(LPARAM)group);}
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Target:",L"目标："),5112,20,94,66,24,0);
        g_hCmdDlgPath=Cmd_Field(hWnd,L"EDIT",L"",CID_CMD_DLG_PATH,92,92,312,24,WS_TABSTOP|ES_AUTOHSCROLL);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Browse...",L"浏览…"),CID_CMD_DLG_BROWSE,412,90,90,28,WS_TABSTOP);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Arguments:",L"参数："),5113,20,130,70,24,0);
        Cmd_Field(hWnd,L"EDIT",L"",CID_CMD_DLG_ARGS,92,128,410,24,WS_TABSTOP|ES_AUTOHSCROLL);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Folder:",L"工作目录："),5114,20,166,72,24,0);
        Cmd_Field(hWnd,L"EDIT",L"",CID_CMD_DLG_DIR,92,164,410,24,WS_TABSTOP|ES_AUTOHSCROLL);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Activate an existing application window",L"激活已运行的应用窗口"),CID_CMD_DLG_ACTIVATE,20,200,482,24,BS_AUTOCHECKBOX|WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Ask before running this command",L"执行此命令前确认"),CID_CMD_DLG_CONFIRM,20,232,482,24,BS_AUTOCHECKBOX|WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Run with administrator permission",L"以管理员权限运行"),CID_CMD_DLG_ADMIN,20,264,482,24,BS_AUTOCHECKBOX|WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Start recording",L"开始录制"),CID_CMD_DLG_RECORD,20,90,132,28,WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Clear",L"清空"),CID_CMD_DLG_CLEAR,162,90,80,28,WS_TABSTOP);
        g_hCmdDlgKeys=Cmd_Field(hWnd,L"EDIT",L"",CID_CMD_DLG_KEYS,20,126,482,94,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY);
        Cmd_Field(hWnd,L"STATIC",L"",CID_CMD_DLG_HINT,20,300,482,48,SS_LEFT);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"OK",L"确定"),101,306,358,92,30,WS_TABSTOP|BS_DEFPUSHBUTTON);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Cancel",L"取消"),102,410,358,92,30,WS_TABSTOP);
        g_cmdKeySeq[0]=0;
        int idx=UI_FindCommand(g_cmdEditId);Command *c=idx>=0?&g_cfg.commands[idx]:NULL;
        if(c){
            UI_SetUtf8(g_hCmdDlgName,c->name);UI_SetUtf8(g_hCmdDlgPath,c->runPath);SC_CopyUtf8(g_cmdKeySeq,sizeof(g_cmdKeySeq),c->keySeq);UI_SetUtf8(g_hCmdDlgGroup,c->group);
            UI_SetUtf8(GetDlgItem(hWnd,CID_CMD_DLG_ARGS),c->arguments);UI_SetUtf8(GetDlgItem(hWnd,CID_CMD_DLG_DIR),c->workingDir);
            SendMessageW(g_hCmdDlgType,CB_SETCURSEL,c->type,0);
            SendDlgItemMessageW(hWnd,CID_CMD_DLG_ACTIVATE,BM_SETCHECK,c->activateExisting?BST_CHECKED:BST_UNCHECKED,0);
            SendDlgItemMessageW(hWnd,CID_CMD_DLG_CONFIRM,BM_SETCHECK,c->confirmBeforeRun?BST_CHECKED:BST_UNCHECKED,0);
            SendDlgItemMessageW(hWnd,CID_CMD_DLG_ADMIN,BM_SETCHECK,c->runAsAdmin?BST_CHECKED:BST_UNCHECKED,0);
            if(c->locked){EnableWindow(g_hCmdDlgType,FALSE);EnableWindow(g_hCmdDlgPath,FALSE);EnableWindow(GetDlgItem(hWnd,CID_CMD_DLG_RECORD),FALSE);EnableWindow(GetDlgItem(hWnd,CID_CMD_DLG_CLEAR),FALSE);EnableWindow(GetDlgItem(hWnd,CID_CMD_DLG_BROWSE),FALSE);}
        }else{SendMessageW(g_hCmdDlgType,CB_SETCURSEL,CMD_RUN,0);SetWindowTextW(g_hCmdDlgGroup,L"Apps");SendDlgItemMessageW(hWnd,CID_CMD_DLG_ACTIVATE,BM_SETCHECK,BST_CHECKED,0);}
        EnumChildWindows(hWnd,UI_SetFont,(LPARAM)font);CmdDlg_UpdateFields();Cmd_RecordPreview();return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==CID_CMD_DLG_RECORD){if(g_cmdRecordHook)Cmd_RecordStop();else if(IsWindowEnabled(GetDlgItem(hWnd,CID_CMD_DLG_RECORD)))Cmd_RecordStart();return 0;}
        if(LOWORD(wp)==CID_CMD_DLG_CLEAR && IsWindowEnabled(GetDlgItem(hWnd,CID_CMD_DLG_CLEAR))){g_cmdKeySeq[0]=0;Cmd_RecordPreview();return 0;}
        if(LOWORD(wp)==CID_CMD_DLG_TYPE && HIWORD(wp)==CBN_SELCHANGE){CmdDlg_UpdateFields();return 0;}
        if(LOWORD(wp)==CID_CMD_DLG_BROWSE){
            wchar_t file[SC_PATH_CAP]=L"";OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hWnd;ofn.lpstrFilter=SC_Text(L"All files\0*.*\0\0",L"所有文件\0*.*\0\0");ofn.lpstrFile=file;ofn.nMaxFile=SC_PATH_CAP;ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
            if(GetOpenFileNameW(&ofn))SetWindowTextW(g_hCmdDlgPath,file);
            return 0;
        }
        if(LOWORD(wp)==101){
            if(g_cmdRecordHook)return 0;
            int idx=UI_FindCommand(g_cmdEditId);if(g_cmdEditId && idx<0){MessageBoxW(hWnd,SC_Text(L"This command was removed. Close the editor and select another command.",L"此命令已被移除，请关闭编辑窗口后重新选择。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}
            Command candidate={0};if(idx>=0)candidate=g_cfg.commands[idx];
            if(!UI_ReadUtf8(hWnd,g_hCmdDlgName,candidate.name,sizeof(candidate.name)) || !candidate.name[0]){SetFocus(g_hCmdDlgName);return 0;}
            int changedName=idx<0 || lstrcmpiA(candidate.name,g_cfg.commands[idx].name);
            if(changedName){
                int alias=CmdNameToId(candidate.name);
                if(alias<0){MessageBoxW(hWnd,SC_Text(L"This name is reserved. Choose a different command name.",L"此名称是系统保留名称，请更换。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}
                for(int i=0;i<g_cfg.commandCount;i++)if(i!=idx && !lstrcmpiA(candidate.name,g_cfg.commands[i].name)){MessageBoxW(hWnd,SC_Text(L"A command with this name already exists.",L"已有同名命令，请更换名称。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}
            }
            if(!UI_ReadUtf8(hWnd,g_hCmdDlgGroup,candidate.group,sizeof(candidate.group)))return 0;
            if(!candidate.group[0])SC_CopyUtf8(candidate.group,sizeof(candidate.group),"None");
            if(!candidate.locked && (!_strnicmp(candidate.group,"Built-in",8)||!lstrcmpiA(candidate.group,"Scripts")))SC_CopyUtf8(candidate.group,sizeof(candidate.group),"None");
            if(!candidate.locked){candidate.type=(CmdType)SendMessageW(g_hCmdDlgType,CB_GETCURSEL,0,0);if(!UI_ReadUtf8(hWnd,g_hCmdDlgPath,candidate.runPath,sizeof(candidate.runPath)))return 0;SC_CopyUtf8(candidate.keySeq,sizeof(candidate.keySeq),g_cmdKeySeq);}
            if(!UI_ReadUtf8(hWnd,GetDlgItem(hWnd,CID_CMD_DLG_ARGS),candidate.arguments,sizeof(candidate.arguments)) || !UI_ReadUtf8(hWnd,GetDlgItem(hWnd,CID_CMD_DLG_DIR),candidate.workingDir,sizeof(candidate.workingDir)))return 0;
            if(candidate.type==CMD_KEYS){wchar_t error[256];if(!Action_ValidateKeySeq(candidate.keySeq,error,256)){MessageBoxW(hWnd,error,L"ShortCutter",MB_OK|MB_ICONWARNING);SetFocus(g_hCmdDlgKeys);return 0;}}
            else if(!candidate.runPath[0]){MessageBoxW(hWnd,SC_Text(L"Enter a target path or application name.",L"请输入目标路径或应用名称。"),L"ShortCutter",MB_OK|MB_ICONWARNING);SetFocus(g_hCmdDlgPath);return 0;}
            if(candidate.type==CMD_SCRIPT && changedName){wchar_t name[SC_NAME_CAP];SC_WideFromUtf8(name,SC_NAME_CAP,candidate.name);if(!UI_ValidFileName(name)){MessageBoxW(hWnd,SC_Text(L"Use a valid file name without folders or reserved characters.",L"请使用有效文件名，不包含目录分隔符或保留字符。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}}
            candidate.activateExisting=SendDlgItemMessageW(hWnd,CID_CMD_DLG_ACTIVATE,BM_GETCHECK,0,0)==BST_CHECKED;
            candidate.confirmBeforeRun=SendDlgItemMessageW(hWnd,CID_CMD_DLG_CONFIRM,BM_GETCHECK,0,0)==BST_CHECKED;
            candidate.runAsAdmin=candidate.type!=CMD_KEYS && SendDlgItemMessageW(hWnd,CID_CMD_DLG_ADMIN,BM_GETCHECK,0,0)==BST_CHECKED;
            if(idx<0){if(g_cfg.commandCount>=MAX_COMMANDS){MessageBoxW(hWnd,SC_Text(L"Command capacity reached. Remove an unused command first.",L"命令数量已达上限，请先移除不用的命令。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}candidate.id=Config_AllocateCommandId(&g_cfg);if(!candidate.id)return 0;idx=g_cfg.commandCount++;}
            g_cfg.commands[idx]=candidate;UI_Dirty();PostMessageW(GetParent(hWnd),WM_SC_REFRESH,0,0);DestroyWindow(hWnd);return 0;
        }
        if(LOWORD(wp)==102){DestroyWindow(hWnd);return 0;}break;
    case WM_SC_KEY_PREVIEW:g_cmdPreviewPending=0;if(g_cmdRecordFull)Cmd_RecordStop();else Cmd_RecordPreview();return 0;
    case WM_TIMER:if(wp==CMD_RECORD_TIMER && g_cmdRecordHook && GetForegroundWindow()!=hWnd)Cmd_RecordStop();break;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)Cmd_RecordStop();break;
    case WM_DPICHANGED:UI_ScaleDialog(hWnd,wp,lp);return 0;
    case WM_CLOSE:DestroyWindow(hWnd);return 0;
    case WM_DESTROY:{Cmd_RecordStop();UI_EndEditor(hWnd);HFONT font=(HFONT)GetWindowLongPtrW(hWnd,GWLP_USERDATA);if(font)DeleteObject(font);g_hCmdDlg=NULL;return 0;}
    }
    return DefWindowProcW(hWnd,msg,wp,lp);
}
static void CmdDlg_Show(HWND parent,int idx)
{
    if(g_hCmdDlg){SetForegroundWindow(g_hCmdDlg);return;}if(!UI_BeginEditor(parent))return;
    g_cmdEditIdx=idx;g_cmdEditId=idx>=0 && idx<g_cfg.commandCount?g_cfg.commands[idx].id:0;
    static BOOL registered;if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=CmdDlgProc;wc.hInstance=g_hInst;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.lpszClassName=L"SC_CmdDlg";RegisterClassExW(&wc);registered=TRUE;}
    g_hCmdDlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"SC_CmdDlg",g_cmdEditId?SC_Text(L"Edit command",L"编辑命令"):SC_Text(L"New command",L"新建命令"),WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,0,0,SCALE(536),SCALE(430),parent,NULL,g_hInst,NULL);
    if(g_hCmdDlg){EnableWindow(parent,FALSE);UI_CenterOwned(g_hCmdDlg,parent);SetFocus(g_hCmdDlgName);}
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

static void RecDlg_ShowChains(void)
{
    wchar_t text[512]=L"";
    for(int s=0;s<g_recTotalSamples && s<3;s++) {
        wchar_t one[128];UI_ChainArrows(g_recSampleChains[s],one,128);
        if(s)wcscat(text,L"  |  ");
        wcscat(text,one);
    }
    SetWindowTextW(g_hRecChain,text);
}

static void RecDlg_UpdateChain(void)
{
    if (!g_hRecChain || g_recPtCount < 2 || g_recTotalSamples >= 3) return;
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
    RecDlg_ShowChains();
    /* 更新采样计数 */
    if (g_hRecSampLabel) {
        wchar_t wb[32]; swprintf(wb, 32, SC_Text(L"Sample: %d/3", L"采样：%d/3"), g_recTotalSamples);
        SetWindowTextW(g_hRecSampLabel, wb);
    }
}

static LRESULT CALLBACK RecCanvasProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_RBUTTONDOWN:
        if (g_recTotalSamples >= 3) return 0;
        g_recording = 1; g_recPtCount = 0;
        { POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT bounds;GetClientRect(hWnd,&bounds);if(pt.x<0)pt.x=0;if(pt.y<0)pt.y=0;if(pt.x>bounds.right)pt.x=bounds.right;if(pt.y>bounds.bottom)pt.y=bounds.bottom; g_recPts[g_recPtCount++] = pt; }
        SetCapture(hWnd); InvalidateRect(hWnd, NULL, TRUE); return 0;
    case WM_MOUSEMOVE:
        if (g_recording && g_recPtCount < 500) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT bounds;GetClientRect(hWnd,&bounds);if(pt.x<0)pt.x=0;if(pt.y<0)pt.y=0;if(pt.x>bounds.right)pt.x=bounds.right;if(pt.y>bounds.bottom)pt.y=bounds.bottom;
            int dx = pt.x - g_recPts[g_recPtCount-1].x;
            int dy = pt.y - g_recPts[g_recPtCount-1].y;
            if (dx*dx + dy*dy >= 16) g_recPts[g_recPtCount++] = pt;
            InvalidateRect(hWnd, NULL, FALSE);
        } return 0;
    case WM_RBUTTONUP:
        if (g_recording) {
            /* ReleaseCapture synchronously sends WM_CAPTURECHANGED, including
               on normal completion. Commit before releasing; only an active
               recording may be cancelled by the capture-change handler. */
            g_recording = 0;
            RecDlg_UpdateChain();
            if (GetCapture() == hWnd) ReleaseCapture();
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (g_recording) {
            g_recording = 0; g_recPtCount = 0;
            if (GetCapture() == hWnd) ReleaseCapture();
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        if(!g_recPtCount && !g_recTotalSamples){SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,RGB(110,110,110));HFONT old=SelectObject(hdc,(HFONT)SendMessageW(hWnd,WM_GETFONT,0,0));DrawTextW(hdc,SC_Text(L"Right-drag here · up to 3 samples",L"在这里按住右键绘制 · 最多 3 个采样"),-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(hdc,old);}
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
            HPEN oldPen=SelectObject(hdc,rp); HBRUSH oldBrush=SelectObject(hdc,rb);
            Ellipse(hdc, g_recPts[0].x-3, g_recPts[0].y-3, g_recPts[0].x+3, g_recPts[0].y+3);
            SelectObject(hdc,oldPen); SelectObject(hdc,oldBrush); DeleteObject(rb); DeleteObject(rp);
        } else if (g_recTotalSamples > 0) {
            /* 不在录制中时显示最后一个采样的起点 */
            int ls = g_recTotalSamples - 1;
            if (g_recSampleCounts[ls] > 0) {
                HPEN rp = CreatePen(PS_SOLID, 1, RGB(200,50,50));
                HBRUSH rb = CreateSolidBrush(RGB(200,50,50));
                HPEN oldPen=SelectObject(hdc,rp); HBRUSH oldBrush=SelectObject(hdc,rb);
                Ellipse(hdc, g_recSamples[ls][0].x-3, g_recSamples[ls][0].y-3,
                        g_recSamples[ls][0].x+3, g_recSamples[ls][0].y+3);
                SelectObject(hdc,oldPen); SelectObject(hdc,oldBrush); DeleteObject(rb); DeleteObject(rp);
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
        HFONT hf = UI_CreateFont();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        RECT client;GetClientRect(hWnd,&client);
        const int left=SCALE(12),right=client.right-SCALE(12),fieldLeft=SCALE(60);
        { static BOOL cvReg = FALSE;
          if (!cvReg) { WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=RecCanvasProc;
              wc.hInstance=g_hInst; wc.hCursor=LoadCursor(NULL,IDC_CROSS);
              wc.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
              wc.lpszClassName=L"SC_RecCanvas"; RegisterClassExW(&wc); cvReg=TRUE; }
          g_hRecCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, L"SC_RecCanvas", L"",
              WS_CHILD|WS_VISIBLE,
              left,SCALE(10),right-left,SCALE(150), hWnd, (HMENU)CID_REC_CANVAS, g_hInst, NULL); }
        int y = SCALE(168);
        g_hRecSampLabel = CreateWindowExW(0, L"STATIC", SC_Text(L"Sample: 0/3", L"采样：0/3"),
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            left,y+SCALE(3),right-left-SCALE(126),SCALE(20), hWnd, NULL, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Record", L"录制"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(118),y,SCALE(55),SCALE(22), hWnd, (HMENU)103, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Clear", L"清空"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(55),y,SCALE(55),SCALE(22), hWnd, (HMENU)104, g_hInst, NULL);
        y = SCALE(195);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Chain:", L"方向："), WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(2),SCALE(45),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecChain = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,
            fieldLeft,y,right-fieldLeft,SCALE(22), hWnd, (HMENU)CID_REC_CHAIN, g_hInst, NULL);
        SendMessageW(g_hRecChain, WM_SETFONT, (WPARAM)hf, TRUE); y+=SCALE(28);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Name:", L"名称："), WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(12),y+SCALE(2),SCALE(45),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            fieldLeft,y,right-fieldLeft,SCALE(22), hWnd, (HMENU)CID_REC_NAME, g_hInst, NULL);
        SendMessageW(g_hRecName, WM_SETFONT, (WPARAM)hf, TRUE);
        y+=SCALE(28);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Mod:", L"修饰"), WS_CHILD|WS_VISIBLE|SS_LEFT,
            left,y+SCALE(2),SCALE(44),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecMod = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            fieldLeft,y,SCALE(108),SCALE(100), hWnd, (HMENU)CID_REC_MOD, g_hInst, NULL);
        SendMessageW(g_hRecMod, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Zone:", L"区域"), WS_CHILD|WS_VISIBLE|SS_LEFT,
            SCALE(184),y+SCALE(2),SCALE(44),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecZone = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            SCALE(232),y,right-SCALE(232),SCALE(150), hWnd, (HMENU)CID_REC_ZONE, g_hInst, NULL);
        SendMessageW(g_hRecZone, WM_SETFONT, (WPARAM)hf, TRUE);
        for (int zi = 0; zi < ZONE_COUNT; zi++)
            SendMessageW(g_hRecZone, CB_ADDSTRING, 0, (LPARAM)ZoneName(zi));
        SendMessageW(g_hRecZone, CB_SETCURSEL, 0, 0);
        { const wchar_t *ms[]={SC_Text(L"None", L"无"),L"Ctrl",L"Shift",L"Alt",L"Win",L"Ctrl+Shift",L"Ctrl+Alt",L"Alt+Shift",L"X1",L"X2"};
          int vs[]={0,1,4,2,8,5,3,6,0x10,0x20};
          for (int i=0;i<10;i++) { int ix=(int)SendMessageW(g_hRecMod,CB_ADDSTRING,0,(LPARAM)ms[i]);
              SendMessageW(g_hRecMod,CB_SETITEMDATA,ix,vs[i]); } }
        y+=SCALE(28);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Cmd:", L"命令"), WS_CHILD|WS_VISIBLE|SS_LEFT,
            left,y+SCALE(2),SCALE(45),SCALE(18), hWnd, NULL, g_hInst, NULL);
        g_hRecCmdEdit = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,
            fieldLeft,y,right-fieldLeft,SCALE(180), hWnd, (HMENU)5204, g_hInst, NULL);
        UI_FillCommandChoice(g_hRecCmdEdit,g_recEditIdx>=0?g_cfg.gestures[g_recEditIdx].commandId:CMDID_NONE);
        SendMessageW(g_hRecCmdEdit, WM_SETFONT, (WPARAM)hf, TRUE); y+=SCALE(28);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Enabled", L"启用"), WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            SCALE(12),y+SCALE(2),SCALE(80),SCALE(20), hWnd, (HMENU)CID_REC_ENABLED, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"OK", L"确定"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(184),y+SCALE(2),SCALE(88),SCALE(26), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Cancel", L"取消"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(88),y+SCALE(2),SCALE(88),SCALE(26), hWnd, (HMENU)102, g_hInst, NULL);

        if (g_recEditIdx >= 0 && g_recEditIdx < g_cfg.gestureCount) {
            GestureDef *g = &g_cfg.gestures[g_recEditIdx];
            UI_SetUtf8(g_hRecName,g->name);
            { int cnt=(int)SendMessageW(g_hRecMod,CB_GETCOUNT,0,0);
              for (int i=0;i<cnt;i++) if ((int)SendMessageW(g_hRecMod,CB_GETITEMDATA,i,0)==g->modifier)
                  { SendMessageW(g_hRecMod,CB_SETCURSEL,i,0); break; } }
            if (g->zone >= 0 && g->zone < ZONE_COUNT)
                SendMessageW(g_hRecZone, CB_SETCURSEL, (WPARAM)g->zone, 0);

            SendMessageW(GetDlgItem(hWnd,CID_REC_ENABLED),BM_SETCHECK,g->enabled?BST_CHECKED:BST_UNCHECKED,0);
            /* 加载已有采样链 + 点数据（从内存 ptsStr） */
            if (g->chainCount > 0) {
                g_recTotalSamples = g->chainCount>3?3:g->chainCount;
                for (int s = 0; s < g_recTotalSamples; s++) {
                    SC_CopyUtf8(g_recSampleChains[s],sizeof(g_recSampleChains[s]),g->chains[s]);
                    g_recSampleChainLens[s] = (int)strlen(g->chains[s]);
                    /* 从 ptsStr 恢复点数据 */
                    if (g->ptsStr[s][0]) {
                        int p = 0;
                        const char *cur = g->ptsStr[s];
                        while (p < 500 && *cur) {
                            char *end; errno=0; long x=strtol(cur,&end,10);
                            if(errno || end==cur || *end!=',' || x<-32768 || x>32767)break;
                            cur=end+1;errno=0;long y=strtol(cur,&end,10);
                            if(errno || end==cur || (*end && *end!=';') || y<-32768 || y>32767)break;
                            cur=*end?end+1:end;
                            g_recSamples[s][p].x=(LONG)x;g_recSamples[s][p].y=(LONG)y;p++;
                        }

                        g_recSampleCounts[s] = p;
                    } else g_recSampleCounts[s] = 0;
                }
                RecDlg_ShowChains();
            }
            wchar_t wb[32]; swprintf(wb, 32, SC_Text(L"Sample: %d/3", L"采样：%d/3"), g_recTotalSamples);
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
                { MessageBoxW(hWnd, SC_Text(L"Max 3 samples reached.", L"最多可录制 3 个采样。"), L"ShortCutter", MB_OK|MB_ICONINFORMATION); return 0; }
            SetWindowTextW(g_hRecSampLabel, SC_Text(L"Right-drag on canvas...", L"右键拖动绘制"));
            SetFocus(g_hRecCanvas);
            return 0; }
        if (LOWORD(wp) == 104) { /* Clear */
            g_recTotalSamples = 0; g_recPtCount = 0; g_recording = 0;
            memset(g_recSampleCounts, 0, sizeof(g_recSampleCounts));
            memset(g_recSampleChains, 0, sizeof(g_recSampleChains));
            SetWindowTextA(g_hRecChain, "");
            SetWindowTextW(g_hRecSampLabel, SC_Text(L"Sample: 0/3", L"采样：0/3"));
            InvalidateRect(g_hRecCanvas, NULL, TRUE);
            return 0; }
        if (LOWORD(wp) == 101) { /* OK */
            GestureDef candidate={0};
            if(g_recEditIdx>=0)candidate=g_cfg.gestures[g_recEditIdx];
            if(!UI_ReadUtf8(hWnd,g_hRecName,candidate.name,sizeof(candidate.name)) || !candidate.name[0]){SetFocus(g_hRecName);return 0;}
            if(g_recTotalSamples==0){MessageBoxW(hWnd,SC_Text(L"Draw a gesture first.",L"请先绘制手势。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}
            int command=UI_ChoiceCommand(hWnd,g_hRecCmdEdit);if(command==0)return 0;
            candidate.commandId=command;
            int idx=g_recEditIdx;if(idx<0 && g_cfg.gestureCount>=MAX_GESTURES){MessageBoxW(hWnd,SC_Text(L"Gesture capacity reached.",L"手势数量已达上限。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}
            GestureDef *g=&candidate;g->chainCount=g_recTotalSamples;

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

            g->enabled = (SendMessageW(GetDlgItem(hWnd,CID_REC_ENABLED),BM_GETCHECK,0,0)==BST_CHECKED);
            if(idx<0)idx=g_cfg.gestureCount++;
            g_cfg.gestures[idx]=candidate;UI_Dirty();
            PostMessageW(GetParent(hWnd), WM_SC_REFRESH, 0, 0);
            DestroyWindow(hWnd); return 0;
        }
        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_DPICHANGED:UI_ScaleDialog(hWnd,wp,lp);return 0;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        g_recording=0;if(GetCapture()==g_hRecCanvas)ReleaseCapture();UI_EndEditor(hWnd);
        g_hRecDlg = NULL; g_hRecCanvas = NULL; g_hRecSampLabel = NULL; return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void RecDlg_Show(HWND hParent, int idx)
{
    if (g_hRecDlg) { SetForegroundWindow(g_hRecDlg); return; }
    if(!UI_BeginEditor(hParent))return;
    Gesture_Cancel();
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
        idx < 0 ? SC_Text(L"Record Gesture", L"录制手势") : SC_Text(L"Edit Gesture", L"编辑手势"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
    if(g_hRecDlg){EnableWindow(hParent,FALSE);EnumChildWindows(g_hRecDlg,UI_SetFont,GetWindowLongPtrW(g_hRecDlg,GWLP_USERDATA));UI_CenterOwned(g_hRecDlg,hParent);}
}

/* ══════════ Hotkey 编辑弹窗 ═══════════════════ */
static HWND g_hHkDlg = NULL, g_hHkDlgHK, g_hHkDlgDesc, g_hHkDlgCmdEdit;
static int  g_hkEditIdx = -1;
static BYTE g_hkCapMods, g_hkCapVk;
static char g_hkCapLabel[SC_LABEL_CAP];

static BYTE UI_CurrentMods(void)
{
    BYTE mods=0;
    if(GetKeyState(VK_CONTROL)&0x8000)mods|=SC_MOD_CTRL;
    if(GetKeyState(VK_MENU)&0x8000)mods|=SC_MOD_ALT;
    if(GetKeyState(VK_SHIFT)&0x8000)mods|=SC_MOD_SHIFT;
    if((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)mods|=SC_MOD_WIN;
    return mods;
}
static int UI_ModifierKey(int vk)
{return vk==VK_CONTROL || vk==VK_MENU || vk==VK_SHIFT || vk==VK_LWIN || vk==VK_RWIN || (vk>=VK_LSHIFT && vk<=VK_RMENU);}
static void UI_KeyLabel(BYTE mods,BYTE vk,LPARAM keyData,char *out,size_t cap)
{
    wchar_t key[64]={0},label[SC_LABEL_CAP];
    if(!GetKeyNameTextW((LONG)keyData,key,64))swprintf(key,64,L"VK %02X",vk);
    swprintf(label,SC_LABEL_CAP,L"%ls%ls%ls%ls%ls",mods&SC_MOD_CTRL?L"Ctrl+":L"",mods&SC_MOD_ALT?L"Alt+":L"",mods&SC_MOD_SHIFT?L"Shift+":L"",mods&SC_MOD_WIN?L"Win+":L"",key);
    if(!SC_Utf8FromWide(out,cap,label))snprintf(out,cap,"VK %02X",vk);
}
static LRESULT CALLBACK HkDlgHKProc(HWND hWnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    (void)id;(void)data;
    if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN){
        BYTE vk=(BYTE)wp;if(vk==VK_ESCAPE)return 0;if(UI_ModifierKey(vk))return 0;
        g_hkCapMods=UI_CurrentMods();g_hkCapVk=vk;UI_KeyLabel(g_hkCapMods,vk,lp,g_hkCapLabel,sizeof(g_hkCapLabel));UI_SetUtf8(hWnd,g_hkCapLabel);return 0;
    }
    if(msg==WM_RBUTTONUP){g_hkCapMods=g_hkCapVk=0;g_hkCapLabel[0]=0;SetWindowTextW(hWnd,SC_Text(L"Click to bind",L"点击绑定"));return 0;}
    if(msg==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
    return DefSubclassProc(hWnd,msg,wp,lp);
}

static LRESULT CALLBACK HkDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = UI_CreateFont();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hf);
        RECT client;GetClientRect(hWnd,&client);const int right=client.right-SCALE(15);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Hotkey:", L"热键："), WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(15),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgHK = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(75),SCALE(13),right-SCALE(75),SCALE(22), hWnd, (HMENU)CID_HK_DLG_HK, g_hInst, NULL);
        SendMessageW(g_hHkDlgHK, WM_SETFONT, (WPARAM)hf, TRUE);
        SetWindowSubclass(g_hHkDlgHK, HkDlgHKProc, CID_HK_DLG_HK, 0);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Desc:", L"说明："), WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(45),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgDesc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            SCALE(75),SCALE(43),right-SCALE(75),SCALE(22), hWnd, (HMENU)CID_HK_DLG_DESC, g_hInst, NULL);
        SendMessageW(g_hHkDlgDesc, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"STATIC", SC_Text(L"Cmd:", L"命令"), WS_CHILD|WS_VISIBLE,
            SCALE(15),SCALE(75),SCALE(55),SCALE(20), hWnd, NULL, g_hInst, NULL);
        g_hHkDlgCmdEdit = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,
            SCALE(75),SCALE(73),right-SCALE(75),SCALE(180), hWnd, (HMENU)5302, g_hInst, NULL);
        UI_FillCommandChoice(g_hHkDlgCmdEdit,g_hkEditIdx>=0?g_cfg.hotkeys[g_hkEditIdx].commandId:CMDID_NONE);
        SendMessageW(g_hHkDlgCmdEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Enabled", L"启用"), WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            SCALE(15),SCALE(103),SCALE(80),SCALE(20), hWnd, (HMENU)CID_HK_DLG_EN, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"OK", L"确定"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(184),SCALE(103),SCALE(88),SCALE(26), hWnd, (HMENU)101, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", SC_Text(L"Cancel", L"取消"), WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            right-SCALE(88),SCALE(103),SCALE(88),SCALE(26), hWnd, (HMENU)102, g_hInst, NULL);
        if (g_hkEditIdx >= 0 && g_hkEditIdx < g_cfg.hotkeyCount) {
            HotkeyDef *h = &g_cfg.hotkeys[g_hkEditIdx];
            if (h->label[0]) UI_SetUtf8(g_hHkDlgHK,h->label);
            else SetWindowTextW(g_hHkDlgHK, SC_Text(L"Click to bind", L"点击绑定"));
            g_hkCapMods = h->mods; g_hkCapVk = h->vk;
            SC_CopyUtf8(g_hkCapLabel,sizeof(g_hkCapLabel),h->label);
            UI_SetUtf8(g_hHkDlgDesc,h->description);

            SendMessageW(GetDlgItem(hWnd,CID_HK_DLG_EN),BM_SETCHECK,h->enabled?BST_CHECKED:BST_UNCHECKED,0);
        } else {
            SetWindowTextW(g_hHkDlgHK, SC_Text(L"Click to bind", L"点击绑定")); g_hkCapMods=0; g_hkCapVk=0; g_hkCapLabel[0]=0;
            SendMessageW(GetDlgItem(hWnd,CID_HK_DLG_EN),BM_SETCHECK,BST_CHECKED,0);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 101) {
            HotkeyDef candidate={0};int idx=g_hkEditIdx;
            candidate.mods=g_hkCapMods;candidate.vk=g_hkCapVk;
            candidate.enabled=SendDlgItemMessageW(hWnd,CID_HK_DLG_EN,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!candidate.vk){MessageBoxW(hWnd,SC_Text(L"Bind a key first.",L"请先绑定按键。"),L"ShortCutter",MB_OK|MB_ICONWARNING);SetFocus(g_hHkDlgHK);return 0;}
            if(candidate.enabled && UI_HotkeyConflict(candidate.mods,candidate.vk,idx)){MessageBoxW(hWnd,SC_Text(L"This shortcut conflicts with another enabled binding or the pause key.",L"此快捷键与其他已启用的绑定或暂停快捷键冲突。"),L"ShortCutter",MB_OK|MB_ICONWARNING);SetFocus(g_hHkDlgHK);return 0;}
            if(!UI_ReadUtf8(hWnd,g_hHkDlgDesc,candidate.description,sizeof(candidate.description)))return 0;
            candidate.commandId=UI_ChoiceCommand(hWnd,g_hHkDlgCmdEdit);if(!candidate.commandId)return 0;
            SC_CopyUtf8(candidate.label,sizeof(candidate.label),g_hkCapLabel);
            if(idx<0){if(g_cfg.hotkeyCount>=MAX_HOTKEYS){MessageBoxW(hWnd,SC_Text(L"Hotkey capacity reached.",L"热键数量已达上限。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;}idx=g_cfg.hotkeyCount++;}
            g_cfg.hotkeys[idx]=candidate;UI_Dirty();PostMessageW(GetParent(hWnd),WM_SC_REFRESH,0,0);DestroyWindow(hWnd);return 0;
        }

        if (LOWORD(wp) == 102) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_DPICHANGED:UI_ScaleDialog(hWnd,wp,lp);return 0;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY: {
        HFONT hf = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        UI_EndEditor(hWnd);g_hHkDlg = NULL; return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void HkDlg_Show(HWND hParent, int idx)
{
    if (g_hHkDlg) { SetForegroundWindow(g_hHkDlg); return; }
    if(!UI_BeginEditor(hParent))return;
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
        idx < 0 ? SC_Text(L"New Hotkey", L"新建热键") : SC_Text(L"Edit Hotkey", L"编辑热键"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, hParent, NULL, g_hInst, NULL);
    if(g_hHkDlg){EnableWindow(hParent,FALSE);EnumChildWindows(g_hHkDlg,UI_SetFont,GetWindowLongPtrW(g_hHkDlg,GWLP_USERDATA));UI_CenterOwned(g_hHkDlg,hParent);}
}

/* ══════════ Tab 切换 ══════════════════════════ */
static void Tab_Refresh(HWND hParent)
{
    /* All page controls are siblings of the tab. Batch visibility changes,
       then erase the complete old page, including the tab's background. */
    BOOL repaint=IsWindowVisible(hParent);
    if(repaint)SendMessageW(hParent,WM_SETREDRAW,FALSE,0);
    int s;
    /* Tab 0: General */
    s = (g_activeTab == TAB_GENERAL) ? SW_SHOW : SW_HIDE;
    { static const int genCids[] = {3010,3002,3011,3003,3012,CID_MOUSE_BTN,3013,3015,
        CID_TOGGLE_HK,3004,CID_AUTOSTART,3005,
        CID_ZONE_COLOR,CID_ZONE_WIDTH,CID_GLOW_COLOR,CID_GLOW_WIDTH,
        CID_INNER_COLOR,CID_INNER_WIDTH,
        CID_APPEAR_COLOR,CID_APPEAR_WIDTH,CID_MIN_SIZE_UNIT,3046,3047,
        CID_GROUP_STARTUP,CID_GROUP_RECOGNITION,CID_GROUP_GESTURE_APPEAR,
        3026,3028,3029,3030,3031,3032,0};
      for (int i = 0; genCids[i]; i++)
          ShowWindow(GetDlgItem(hParent, genCids[i]), s); }
    /* Dashboard settings */
    s = (g_activeTab == TAB_DASHBOARD) ? SW_SHOW : SW_HIDE;
    { static const int dashCids[] = {CID_DASH_OPACITY,CID_DASH_OPACITY_VALUE,
        CID_DASH_THEME,3037,3035,CID_DASH_QUICK,CID_DASH_QUICK_DESKTOP,
        CID_DASH_QUICK_EXPLORER,CID_DASH_QUICK_HINT,CID_DASH_QUICK_SCOPE,
        CID_GROUP_DASH_APPEAR,CID_GROUP_DASH_QUICK,CID_DASH_COMPACT,0};
      for (int i = 0; dashCids[i]; i++)
          ShowWindow(GetDlgItem(hParent, dashCids[i]), s); }
    ShowWindow(GetDlgItem(hParent,CID_LIST_SEARCH),g_activeTab>=TAB_COMMANDS?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(hParent,CID_LIST_DETAIL),g_activeTab>=TAB_COMMANDS?SW_SHOW:SW_HIDE);
    /* Commands */
    s = (g_activeTab == TAB_COMMANDS) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hParent, 5005), s); /* Type: label */
    ShowWindow(GetDlgItem(hParent, 5002), s); /* Type filter */
    ShowWindow(GetDlgItem(hParent, 5009), s); /* Group: label */
    ShowWindow(GetDlgItem(hParent, 5008), s); /* Group filter */
    ShowWindow(g_hCmdList, s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_DEL), s);
    ShowWindow(GetDlgItem(hParent, CID_CMD_SCAN), s);
    /* Gestures */
    s = (g_activeTab == TAB_GESTURES) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hParent, 5006), s); /* Zone: label */
    ShowWindow(GetDlgItem(hParent, 5007), s); /* Zone filter combo */
    ShowWindow(g_hGesList, s);
    ShowWindow(GetDlgItem(hParent, CID_GES_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_GES_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_GES_DEL), s);
    /* Hotkeys */
    s = (g_activeTab == TAB_HOTKEYS) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hHkList, s);
    ShowWindow(GetDlgItem(hParent, CID_HK_NEW), s);
    ShowWindow(GetDlgItem(hParent, CID_HK_EDIT), s);
    ShowWindow(GetDlgItem(hParent, CID_HK_DEL), s);
    if(repaint){
        SendMessageW(hParent,WM_SETREDRAW,TRUE,0);
        RedrawWindow(hParent,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    }
}

/* ── Toggle 快捷键捕获子类化 ────────────────── */
static int g_tglCap = 0;
static LRESULT CALLBACK TglHKProc(HWND hWnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    (void)id;(void)data;
    if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN){
        if(!g_tglCap)return DefSubclassProc(hWnd,msg,wp,lp);
        BYTE vk=(BYTE)wp;
        if(vk==VK_ESCAPE){g_tglCap=0;UI_SetUtf8(hWnd,g_cfg.toggleLabel);return 0;}
        if(UI_ModifierKey(vk))return 0;
        BYTE mods=UI_CurrentMods();
        for(int i=0;i<g_cfg.hotkeyCount;i++)if(g_cfg.hotkeys[i].enabled && g_cfg.hotkeys[i].vk==vk && g_cfg.hotkeys[i].mods==mods){
            SetWindowTextW(g_hStatus,SC_Text(L"Shortcut already in use; choose another key",L"快捷键已被占用，请选择其他按键"));return 0;
        }
        g_cfg.toggleVk=vk;g_cfg.toggleMods=mods;UI_KeyLabel(mods,vk,lp,g_cfg.toggleLabel,sizeof(g_cfg.toggleLabel));UI_SetUtf8(hWnd,g_cfg.toggleLabel);g_tglCap=0;UI_Dirty();return 0;
    }
    if(msg==WM_RBUTTONUP){g_cfg.toggleVk=g_cfg.toggleMods=0;g_cfg.toggleLabel[0]=0;g_tglCap=0;SetWindowTextW(hWnd,SC_Text(L"Click to bind",L"点击绑定"));UI_Dirty();return 0;}
    if(msg==WM_KILLFOCUS && g_tglCap){g_tglCap=0;UI_SetUtf8(hWnd,g_cfg.toggleLabel);}
    if(msg==WM_GETDLGCODE)return g_tglCap?DLGC_WANTALLKEYS:DLGC_BUTTON;
    return DefSubclassProc(hWnd,msg,wp,lp);
}
int UI_IsCapturingInput(void)
{
    HWND foreground=GetForegroundWindow();
    for(HWND w=foreground;w;w=GetWindow(w,GW_OWNER))if(w==g_hDlg && g_hDlg)return 1;
    return 0;
}

/* ══════════ 设置窗口过程 ══════════════════════ */
/* ══════════ Help 弹窗（可滚动） ══════════════════ */
static HWND g_hHelpDlg = NULL;

#include "ui_help.h"
#include "ui_scan.h"

static BOOL CALLBACK SettingsDialog_TranslateChild(HWND child, LPARAM lp)
{
    (void)lp;
    wchar_t className[32], text[80];
    GetClassNameW(child, className, 32);
    if (lstrcmpiW(className, L"STATIC") != 0 &&
        lstrcmpiW(className, L"BUTTON") != 0) return TRUE;
    GetWindowTextW(child, text, 80);
    static const wchar_t *labels[][2] = {
        {L"Name:", L"名称："}, {L"Way:", L"方式："},
        {L"Group:", L"分组："}, {L"Path:", L"路径："},
        {L"Browse", L"浏览"}, {L"OK", L"确定"}, {L"Cancel", L"取消"},
        {L"Record", L"录制"}, {L"Clear", L"清空"},
        {L"Chain:", L"方向："}, {L"Mod:", L"修饰"},
        {L"Zone:", L"区域"}, {L"Cmd:", L"命令"},
        {L"Enabled", L"启用"}, {L"Click to bind", L"点击绑定"},
        {L"Hotkey:", L"热键："}, {L"Desc:", L"说明："},
        {L"Toggle All", L"全选/取消"}, {L"Detail", L"详情"}
    };
    for (int i = 0; i < (int)(sizeof(labels)/sizeof(labels[0])); i++) {
        if (lstrcmpW(text, labels[i][0]) == 0 || lstrcmpW(text, labels[i][1]) == 0) {
            SetWindowTextW(child, SC_Text(labels[i][0], labels[i][1]));
            break;
        }
    }
    return TRUE;
}

static void SettingsDialog_SetComboText(HWND combo, const wchar_t **items, int count)
{
    if (!combo) return;
    int selected = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    LPARAM data[16];
    if (count > 16 || (int)SendMessageW(combo, CB_GETCOUNT, 0, 0) != count) return;
    for (int i = 0; i < count; i++)
        data[i] = SendMessageW(combo, CB_GETITEMDATA, i, 0);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++) {
        int index = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)items[i]);
        SendMessageW(combo, CB_SETITEMDATA, index, data[i]);
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
}

static void SettingsDialogs_ApplyLanguage(void)
{
    HWND dialogs[] = {g_hCmdDlg, g_hRecDlg, g_hHkDlg, g_hScanDlg, g_hHelpDlg};
    for (int i = 0; i < (int)(sizeof(dialogs)/sizeof(dialogs[0])); i++)
        if (dialogs[i]) EnumChildWindows(dialogs[i], SettingsDialog_TranslateChild, 0);
    if (g_hCmdDlg) {
        SetWindowTextW(g_hCmdDlg, g_cmdEditIdx < 0 ?
            SC_Text(L"New Command", L"新建命令") : SC_Text(L"Edit Command", L"编辑命令"));
        const wchar_t *types[] = {SC_Text(L"Run", L"运行"), SC_Text(L"Keys", L"按键"),SC_Text(L"Script",L"脚本")};
        SettingsDialog_SetComboText(g_hCmdDlgType, types, 3);
    }
    if (g_hRecDlg) {
        SetWindowTextW(g_hRecDlg, g_recEditIdx < 0 ?
            SC_Text(L"Record Gesture", L"录制手势") : SC_Text(L"Edit Gesture", L"编辑手势"));
        const wchar_t *zones[ZONE_COUNT];
        for (int i = 0; i < ZONE_COUNT; i++) zones[i] = ZoneName(i);
        SettingsDialog_SetComboText(g_hRecZone, zones, ZONE_COUNT);
        const wchar_t *mods[] = {SC_Text(L"None", L"无"), L"Ctrl", L"Shift", L"Alt", L"Win",
            L"Ctrl+Shift", L"Ctrl+Alt", L"Alt+Shift", L"X1", L"X2"};
        SettingsDialog_SetComboText(g_hRecMod, mods, 10);
        wchar_t status[80]; GetWindowTextW(g_hRecSampLabel, status, 80);
        if (lstrcmpW(status, L"Right-drag on canvas...") == 0 ||
            lstrcmpW(status, L"右键拖动绘制") == 0) {
            SetWindowTextW(g_hRecSampLabel, SC_Text(L"Right-drag on canvas...", L"右键拖动绘制"));
        } else {
            swprintf(status, 80, SC_Text(L"Sample: %d/3", L"采样：%d/3"), g_recTotalSamples);
            SetWindowTextW(g_hRecSampLabel, status);
        }
    }
    if (g_hHkDlg)
        SetWindowTextW(g_hHkDlg, g_hkEditIdx < 0 ?
            SC_Text(L"New Hotkey", L"新建热键") : SC_Text(L"Edit Hotkey", L"编辑热键"));
    if (g_hScanDlg) {
        SetWindowTextW(g_hScanDlg, SC_Text(L"Scan Installed Apps", L"扫描已安装应用"));
        const wchar_t *sources[] = {SC_Text(L"All", L"全部"), SC_Text(L"Desktop", L"桌面"),
            L"Program Files", L"Program Files (x86)", SC_Text(L"Other", L"其他")};
        SettingsDialog_SetComboText(g_hScanCbo, sources, 5);
        const wchar_t *columns[] = {SC_Text(L"Name", L"名称"), SC_Text(L"Path", L"路径")};
        for (int i = 0; i < 2; i++) {
            LVCOLUMNW column = {0}; column.mask = LVCF_TEXT;
            column.pszText = (LPWSTR)columns[i];
            SendMessageW(g_hScanList, LVM_SETCOLUMNW, i, (LPARAM)&column);
        }
        wchar_t status[32];
        swprintf(status, 32, SC_Text(L"Found %d apps", L"找到 %d 个应用"), g_scanCount);
        SetWindowTextW(GetDlgItem(g_hScanDlg, 5010), status);
    }
    if (g_hHelpDlg) {
        SetWindowTextW(g_hHelpDlg, SC_Text(L"ShortCutter - Help", L"ShortCutter - 帮助"));
        Help_Fill();
    }
}

/* ══════════ 设置窗口过程 ══════════════════════ */
static HWND Settings_Control(HWND parent, const wchar_t *className, const wchar_t *text,
    DWORD style, DWORD exStyle, int cid, int x, int y, int width, int height)
{
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | style,
        SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, (HMENU)(INT_PTR)cid,
        g_hInst, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return control;
}

static void Settings_CreatePages(HWND parent)
{
    /* General: two compact sections above the appearance controls. */
    Settings_Control(parent,L"BUTTON",L"Preferences",BS_GROUPBOX,0,
        CID_GROUP_STARTUP,18,40,190,136);
    Settings_Control(parent,L"STATIC",L"Language:",SS_LEFT,0,
        CID_LANGUAGE_LABEL,30,67,76,18);
    HWND language = Settings_Control(parent,L"COMBOBOX",L"",
        CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,0,CID_LANGUAGE,112,64,84,110);
    SendMessageW(language,CB_ADDSTRING,0,(LPARAM)L"中文");
    SendMessageW(language,CB_SETITEMDATA,0,UI_LANG_CHINESE);
    SendMessageW(language,CB_ADDSTRING,0,(LPARAM)L"English");
    SendMessageW(language,CB_SETITEMDATA,1,UI_LANG_ENGLISH);
    SendMessageW(language,CB_SETCURSEL,g_cfg.uiLanguage == UI_LANG_CHINESE ? 0 : 1,0);
    Settings_Control(parent,L"BUTTON",L"Start with Windows",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        CID_AUTOSTART,30,92,166,22);
    Settings_Control(parent,L"STATIC",L"Enable / pause hotkey:",SS_LEFT,0,
        3013,30,118,166,18);
    g_hToggleHK = Settings_Control(parent,L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,0,
        CID_TOGGLE_HK,30,141,166,24);

    Settings_Control(parent,L"BUTTON",L"Gesture recognition",BS_GROUPBOX,0,
        CID_GROUP_RECOGNITION,220,40,242,136);
    Settings_Control(parent,L"STATIC",L"Mouse button:",SS_LEFT,0,3012,232,67,100,18);
    g_hMouseBtn = Settings_Control(parent,L"COMBOBOX",NULL,
        CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,0,CID_MOUSE_BTN,334,64,116,120);
    Settings_Control(parent,L"STATIC",L"Minimum size:",SS_LEFT,0,3010,232,97,112,18);
    Settings_Control(parent,L"EDIT",L"",ES_NUMBER|WS_TABSTOP,WS_EX_CLIENTEDGE,
        3002,348,94,50,22);
    Settings_Control(parent,L"STATIC",L"px",SS_LEFT,0,CID_MIN_SIZE_UNIT,404,97,28,18);
    Settings_Control(parent,L"STATIC",L"Match threshold:",SS_LEFT,0,3011,232,127,112,18);
    Settings_Control(parent,L"EDIT",L"",ES_NUMBER|WS_TABSTOP,WS_EX_CLIENTEDGE,
        3003,348,124,50,22);
    Settings_Control(parent,L"STATIC",L"%",SS_LEFT,0,3029,404,127,28,18);
    Settings_Control(parent,L"STATIC",L"50–95%; higher is stricter",SS_LEFT,0,3015,232,152,218,18);

    Settings_Control(parent,L"BUTTON",L"Gesture appearance",BS_GROUPBOX,0,
        CID_GROUP_GESTURE_APPEAR,18,188,444,130);
    Settings_Control(parent,L"BUTTON",L"Show mouse trail",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        3004,30,206,186,22);
    Settings_Control(parent,L"STATIC",L"Color",SS_LEFT,0,CID_APPEAR_COLOR,244,210,122,18);
    Settings_Control(parent,L"STATIC",L"Width",SS_LEFT,0,CID_APPEAR_WIDTH,378,210,70,18);
    Settings_Control(parent,L"STATIC",L"Outer glow",SS_LEFT,0,3026,48,237,176,18);
    Settings_Control(parent,L"STATIC",L"Core line",SS_LEFT,0,3028,48,263,176,18);
    Settings_Control(parent,L"BUTTON",L"Show zone boundaries",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        3005,30,286,186,22);
    const int colors[] = {CID_GLOW_COLOR,CID_INNER_COLOR,CID_ZONE_COLOR};
    const int widths[] = {CID_GLOW_WIDTH,CID_INNER_WIDTH,CID_ZONE_WIDTH};
    const int units[] = {3030,3031,3032};
    for (int i = 0; i < 3; i++) {
        int y = 234 + i * 26;
        Settings_Control(parent,L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,0,
            colors[i],244,y,122,22);
        Settings_Control(parent,L"EDIT",L"",ES_NUMBER|WS_TABSTOP,WS_EX_CLIENTEDGE,
            widths[i],378,y,42,22);
        Settings_Control(parent,L"STATIC",L"px",SS_LEFT,0,units[i],426,y+3,22,18);
    }

    /* Dashboard: appearance and quick-input scope have their own page. */
    Settings_Control(parent,L"BUTTON",L"Appearance",BS_GROUPBOX,0,
        CID_GROUP_DASH_APPEAR,18,40,444,132);
    Settings_Control(parent,L"STATIC",L"Theme:",SS_LEFT,0,3037,30,67,106,18);
    g_hDashTheme = Settings_Control(parent,L"COMBOBOX",L"",
        CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,0,CID_DASH_THEME,148,64,160,110);
    const wchar_t *names[] = {L"Black",L"White",L"Gray"};
    const int themes[] = {DASH_THEME_BLACK,DASH_THEME_WHITE,DASH_THEME_GRAY};
    for (int i = 0; i < 3; i++) {
        LRESULT row = SendMessageW(g_hDashTheme,CB_ADDSTRING,0,(LPARAM)names[i]);
        SendMessageW(g_hDashTheme,CB_SETITEMDATA,row,themes[i]);
        if (themes[i] == g_cfg.dashboardTheme) SendMessageW(g_hDashTheme,CB_SETCURSEL,row,0);
    }
    Settings_Control(parent,L"STATIC",L"Opacity:",SS_LEFT,0,3035,30,103,106,18);
    g_hDashOpacity = Settings_Control(parent,TRACKBAR_CLASSW,L"",
        WS_TABSTOP|TBS_HORZ,0,CID_DASH_OPACITY,148,96,224,30);
    g_hDashOpacityValue = Settings_Control(parent,L"STATIC",L"100%",SS_RIGHT,0,
        CID_DASH_OPACITY_VALUE,386,103,62,18);

    Settings_Control(parent,L"BUTTON",L"Compact icon layout",BS_AUTOCHECKBOX|WS_TABSTOP,0,CID_DASH_COMPACT,30,136,420,22);
    SendDlgItemMessageW(parent,CID_DASH_COMPACT,BM_SETCHECK,g_cfg.dashboardCompact?BST_CHECKED:BST_UNCHECKED,0);
    Settings_Control(parent,L"BUTTON",L"Quick input",BS_GROUPBOX,0,
        CID_GROUP_DASH_QUICK,18,184,444,134);
    Settings_Control(parent,L"BUTTON",L"Type letters to open Dashboard",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        CID_DASH_QUICK,30,208,420,22);
    Settings_Control(parent,L"STATIC",L"Activate in:",SS_LEFT,0,
        CID_DASH_QUICK_SCOPE,50,246,100,18);
    Settings_Control(parent,L"BUTTON",L"Desktop",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        CID_DASH_QUICK_DESKTOP,158,242,102,22);
    Settings_Control(parent,L"BUTTON",L"File Explorer",BS_AUTOCHECKBOX|WS_TABSTOP,0,
        CID_DASH_QUICK_EXPLORER,280,242,156,22);
    Settings_Control(parent,L"STATIC",L"Letters only; text fields keep normal input.",SS_LEFT,0,
        CID_DASH_QUICK_HINT,50,286,398,18);
    const int quickIds[] = {CID_DASH_QUICK,CID_DASH_QUICK_DESKTOP,CID_DASH_QUICK_EXPLORER};
    const int quickValues[] = {g_cfg.dashboardQuickInput,g_cfg.dashboardQuickDesktop,g_cfg.dashboardQuickExplorer};
    for (int i = 0; i < 3; i++)
        SendMessageW(GetDlgItem(parent,quickIds[i]),BM_SETCHECK,
            quickValues[i] ? BST_CHECKED : BST_UNCHECKED,0);
}

static void Settings_UpdateEnabled(HWND parent)
{
    const int trailIds[] = {CID_GLOW_COLOR,CID_GLOW_WIDTH,CID_INNER_COLOR,CID_INNER_WIDTH,
        3026,3028,3030,3031};
    const int zoneIds[] = {CID_ZONE_COLOR,CID_ZONE_WIDTH,3032};
    const int quickIds[] = {CID_DASH_QUICK_DESKTOP,CID_DASH_QUICK_EXPLORER,
        CID_DASH_QUICK_SCOPE,CID_DASH_QUICK_HINT};
    const int *ids[] = {trailIds,zoneIds,quickIds};
    const int counts[] = {8,3,4};
    const int masters[] = {3004,3005,CID_DASH_QUICK};
    for (int group = 0; group < 3; group++) {
        BOOL enabled = SendMessageW(GetDlgItem(parent,masters[group]),BM_GETCHECK,0,0) == BST_CHECKED;
        for (int i = 0; i < counts[group]; i++)
            EnableWindow(GetDlgItem(parent,ids[group][i]),enabled);
    }
}

static void Settings_UpdateColorLabels(HWND parent)
{
    const int ids[] = {CID_GLOW_COLOR,CID_INNER_COLOR,CID_ZONE_COLOR};
    const COLORREF colors[] = {g_glowColor,g_innerColor,g_zoneColor};
    for (int i = 0; i < 3; i++) {
        wchar_t text[16];
        swprintf(text,16,L"#%02X%02X%02X",GetRValue(colors[i]),GetGValue(colors[i]),GetBValue(colors[i]));
        SetWindowTextW(GetDlgItem(parent,ids[i]),text);
    }
}

static BOOL CALLBACK Settings_SetControlFont(HWND control, LPARAM font)
{
    SendMessageW(control,WM_SETFONT,(WPARAM)font,TRUE);
    return TRUE;
}

static void Settings_TranslateChoices(HWND parent, int cid, const wchar_t *const *names, int count)
{
    HWND combo = GetDlgItem(parent,cid);
    int selected = (int)SendMessageW(combo,CB_GETCURSEL,0,0);
    LPARAM values[8];
    for (int i = 0; i < count; i++) values[i] = SendMessageW(combo,CB_GETITEMDATA,i,0);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    for (int i = 0; i < count; i++) {
        LRESULT row = SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)names[i]);
        SendMessageW(combo,CB_SETITEMDATA,row,values[i]);
    }
    SendMessageW(combo,CB_SETCURSEL,selected,0);
}

static void Settings_TranslateColumns(HWND list, const wchar_t *const *names, int count)
{
    LVCOLUMNW column = {0}; column.mask = LVCF_TEXT;
    for (int i = 0; i < count; i++) {
        column.pszText = (wchar_t*)names[i];
        SendMessageW(list,LVM_SETCOLUMNW,i,(LPARAM)&column);
    }
}

static void Settings_ApplyLanguage(HWND parent)
{
    static const struct { int cid; const wchar_t *english, *chinese; } captions[] = {
        {CID_GROUP_STARTUP,L"Preferences",L"常规选项"},
        {CID_LANGUAGE_LABEL,L"Language:",L"语言："},
        {CID_AUTOSTART,L"Start with Windows",L"开机启动"},
        {3013,L"Enable / pause hotkey:",L"启用 / 暂停快捷键："},
        {CID_GROUP_RECOGNITION,L"Gesture recognition",L"手势识别"},
        {3012,L"Mouse button:",L"触发鼠标键："},
        {3010,L"Minimum size:",L"最小手势尺寸："},
        {3011,L"Match threshold:",L"识别阈值："},
        {3015,L"50–95%; higher is stricter",L"50–95%，越高越严格"},
        {CID_GROUP_GESTURE_APPEAR,L"Gesture appearance",L"手势显示"},
        {3004,L"Show mouse trail",L"显示鼠标轨迹"},
        {3005,L"Show zone boundaries",L"显示区域边界"},
        {CID_APPEAR_COLOR,L"Color",L"颜色"},
        {CID_APPEAR_WIDTH,L"Width",L"宽度"},
        {3026,L"Outer glow",L"外层光晕"},
        {3028,L"Core line",L"核心线条"},
        {CID_GROUP_DASH_APPEAR,L"Appearance",L"外观"},
        {3037,L"Theme:",L"主题："},
        {3035,L"Opacity:",L"不透明度："},
        {CID_GROUP_DASH_QUICK,L"Quick input",L"快捷输入"},
        {CID_DASH_QUICK,L"Type letters to open Dashboard",L"输入字母打开仪表盘"},
        {CID_DASH_QUICK_SCOPE,L"Activate in:",L"启用位置："},
        {CID_DASH_QUICK_DESKTOP,L"Desktop",L"桌面"},
        {CID_DASH_QUICK_EXPLORER,L"File Explorer",L"资源管理器"},
        {CID_DASH_QUICK_HINT,L"Letters only; text fields keep normal input.",L"仅限英文字母；文本输入框保留正常输入。"},
        {5005,L"Type:",L"类型："}, {5009,L"Group:",L"分组："}, {5006,L"Zone:",L"区域："},
        {CID_CMD_NEW,L"New...",L"新建..."}, {CID_CMD_EDIT,L"Edit...",L"编辑..."},
        {CID_CMD_DEL,L"Delete",L"删除"}, {CID_CMD_SCAN,L"Scan Apps...",L"扫描应用..."},
        {CID_GES_NEW,L"Record...",L"录制..."}, {CID_GES_EDIT,L"Edit...",L"编辑..."},
        {CID_GES_DEL,L"Delete",L"删除"},
        {CID_HK_NEW,L"New...",L"新建..."}, {CID_HK_EDIT,L"Edit...",L"编辑..."},
        {CID_HK_DEL,L"Delete",L"删除"},
        {CID_DASH_COMPACT,L"Compact icon layout",L"紧凑图标布局"},
        {CID_CANCEL,L"Cancel",L"取消"}, {5001,L"Help",L"帮助"}, {CID_BTN_SAVE,L"Save && Apply",L"保存并应用"}
    };
    SetWindowTextW(parent,SC_Text(L"ShortCutter - Settings",L"ShortCutter - 设置"));
    for (int i = 0; i < (int)(sizeof(captions)/sizeof(captions[0])); i++)
        SetWindowTextW(GetDlgItem(parent,captions[i].cid),SC_Text(captions[i].english,captions[i].chinese));
    const wchar_t *tabs[] = {SC_Text(L"General",L"常规"),SC_Text(L"Dashboard",L"仪表盘"),
        SC_Text(L"Commands",L"命令"),SC_Text(L"Gestures",L"手势"),SC_Text(L"Hotkeys",L"热键")};
    TCITEMW item = {0}; item.mask = TCIF_TEXT;
    for (int i = 0; i < 5; i++) {
        item.pszText = (wchar_t*)tabs[i];
        SendMessageW(g_hTab,TCM_SETITEMW,i,(LPARAM)&item);
    }
    const wchar_t *mouse[] = {SC_Text(L"Right Button",L"右键"),SC_Text(L"Middle Button",L"中键")};
    const wchar_t *themes[] = {SC_Text(L"Black",L"黑色"),SC_Text(L"White",L"白色"),SC_Text(L"Gray",L"灰色")};
    const wchar_t *types[] = {SC_Text(L"All",L"全部"),SC_Text(L"Custom",L"自定义"),
        SC_Text(L"Built-in",L"内置"),SC_Text(L"Scripts",L"脚本")};
    const wchar_t *zones[ZONE_COUNT+1];zones[0]=SC_Text(L"All",L"全部");
    for(int i=0;i<ZONE_COUNT;i++)zones[i+1]=ZoneName(i);
    Settings_TranslateChoices(parent,CID_MOUSE_BTN,mouse,2);
    Settings_TranslateChoices(parent,CID_DASH_THEME,themes,3);
    Settings_TranslateChoices(parent,5002,types,4);
    Settings_TranslateChoices(parent,5007,zones,ZONE_COUNT+1);
    const wchar_t *commands[] = {SC_Text(L"Name",L"名称"),SC_Text(L"Type",L"类型"),
        SC_Text(L"Group",L"分组"),SC_Text(L"Target",L"目标"),SC_Text(L"Source",L"来源")};
    const wchar_t *gestures[] = {SC_Text(L"Name",L"名称"),SC_Text(L"Chain",L"方向链"),
        SC_Text(L"Mod",L"修饰"),SC_Text(L"Zone",L"区域"),SC_Text(L"Command",L"命令"),SC_Text(L"On",L"启用")};
    const wchar_t *hotkeys[] = {SC_Text(L"Hotkey",L"快捷键"),SC_Text(L"Description",L"说明"),
        SC_Text(L"Command",L"命令"),SC_Text(L"On",L"启用")};
    Settings_TranslateColumns(g_hCmdList,commands,5);
    Settings_TranslateColumns(g_hGesList,gestures,6);
    Settings_TranslateColumns(g_hHkList,hotkeys,4);

    const HWND lists[] = {g_hCmdList,g_hGesList,g_hHkList};
    int selected[3], tops[3];
    BOOL visible[3];
    for (int i = 0; i < 3; i++) {
        selected[i] = ListView_GetNextItem(lists[i],-1,LVNI_SELECTED);
        tops[i] = ListView_GetTopIndex(lists[i]);
        visible[i] = IsWindowVisible(lists[i]);
        if (visible[i]) SendMessageW(lists[i],WM_SETREDRAW,FALSE,0);
    }
    CmdList_Fill(); GesList_Fill(); HkList_Fill();
    for (int i = 0; i < 3; i++) {
        if (selected[i] >= 0)
            ListView_SetItemState(lists[i],selected[i],LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        RECT row;
        if (tops[i] > 0 && ListView_GetItemRect(lists[i],0,&row,LVIR_BOUNDS))
            ListView_Scroll(lists[i],0,tops[i]*(row.bottom-row.top));
        if (visible[i]) SendMessageW(lists[i],WM_SETREDRAW,TRUE,0);
        InvalidateRect(lists[i],NULL,TRUE);
    }
    if (!g_cfg.toggleLabel[0] && !g_tglCap)
        SetWindowTextW(g_hToggleHK,SC_Text(L"Click to bind",L"点击绑定"));
    SetWindowTextW(g_hStatus,g_tglCap ? SC_Text(L"⌨ Press key for toggle...",L"⌨ 请按启用 / 暂停快捷键...") :
        (g_settingsDirty?SC_Text(L"Changes not yet applied",L"更改尚未应用"):SC_GetInputStatusText()));
    SettingsDialogs_ApplyLanguage();
}

#include "ui_apply.h"
static void Settings_LayoutLists(HWND hWnd)
{
    RECT client;GetClientRect(hWnd,&client);int width=client.right,height=client.bottom;
    MoveWindow(g_hTab,SCALE(8),SCALE(8),width-SCALE(16),height-SCALE(50),TRUE);
    const HWND lists[]={g_hCmdList,g_hGesList,g_hHkList};
    int bottom=height-SCALE(76);
    for(int i=0;i<3;i++)MoveWindow(lists[i],SCALE(18),SCALE(100),width-SCALE(36),bottom-SCALE(40)-SCALE(100),TRUE);
    MoveWindow(GetDlgItem(hWnd,CID_LIST_SEARCH),SCALE(18),SCALE(72),width-SCALE(36),SCALE(22),TRUE);
    MoveWindow(GetDlgItem(hWnd,CID_LIST_DETAIL),SCALE(18),bottom-SCALE(34),width-SCALE(36),SCALE(28),TRUE);
    const int buttons[]={CID_CMD_NEW,CID_CMD_EDIT,CID_CMD_DEL,CID_CMD_SCAN,CID_GES_NEW,CID_GES_EDIT,CID_GES_DEL,CID_HK_NEW,CID_HK_EDIT,CID_HK_DEL};
    for(int i=0;i<10;i++){HWND button=GetDlgItem(hWnd,buttons[i]);RECT r;GetWindowRect(button,&r);MapWindowPoints(NULL,hWnd,(POINT*)&r,2);SetWindowPos(button,NULL,r.left,bottom,SCALE(buttons[i]==CID_CMD_SCAN?95:75),SCALE(26),SWP_NOZORDER|SWP_NOACTIVATE);}
    int footer=height-SCALE(34);
    MoveWindow(g_hStatus,SCALE(18),footer+SCALE(4),width-SCALE(268),SCALE(22),TRUE);
    MoveWindow(GetDlgItem(hWnd,5001),width-SCALE(246),footer,SCALE(55),SCALE(26),TRUE);
    MoveWindow(GetDlgItem(hWnd,CID_CANCEL),width-SCALE(184),footer,SCALE(60),SCALE(26),TRUE);
    MoveWindow(GetDlgItem(hWnd,CID_BTN_SAVE),width-SCALE(117),footer,SCALE(100),SCALE(26),TRUE);
}
static void Settings_UpdateDetail(HWND list)
{
    int row=ListView_GetNextItem(list,-1,LVNI_SELECTED);wchar_t detail[SC_PATH_CAP+SC_KEYS_CAP+64]=L"";
    if(row>=0){
        if(list==g_hCmdList){int i=CmdListRowToIdx(row);if(i>=0){Command *c=&g_cfg.commands[i];SC_WideFromUtf8(detail,sizeof(detail)/sizeof(detail[0]),c->type==CMD_KEYS?c->keySeq:c->runPath);}}
        else {int i=UI_ListData(list,row);if(i>=0){int id=list==g_hGesList?g_cfg.gestures[i].commandId:g_cfg.hotkeys[i].commandId;char name[SC_NAME_CAP];CmdIdToName(id,name,sizeof(name));SC_WideFromUtf8(detail,sizeof(detail)/sizeof(detail[0]),name);}}
    }
    SetWindowTextW(GetDlgItem(g_hDlg,CID_LIST_DETAIL),detail);
}

static void Settings_CaptureSize(HWND window)
{
    RECT client;
    if(IsIconic(window) || IsZoomed(window) || !GetClientRect(window,&client) ||
       client.right<=0 || client.bottom<=0)return;
    g_settingsWindowWidth=MulDiv(client.right,96,g_dpiScale);
    g_settingsWindowHeight=MulDiv(client.bottom,96,g_dpiScale);
    if(g_settingsWindowWidth<SETS_W)g_settingsWindowWidth=SETS_W;
    if(g_settingsWindowHeight<SETS_H)g_settingsWindowHeight=SETS_H;
    if(g_settingsWindowWidth>SC_SETTINGS_MAX_SIZE)g_settingsWindowWidth=SC_SETTINGS_MAX_SIZE;
    if(g_settingsWindowHeight>SC_SETTINGS_MAX_SIZE)g_settingsWindowHeight=SC_SETTINGS_MAX_SIZE;
    g_uiDraft.settingsWidth=g_settingsWindowWidth;
    g_uiDraft.settingsHeight=g_settingsWindowHeight;
}

static void Settings_CaptureColumns(void)
{
    if(!g_settingsColumnsReady || g_settingsRestoringColumns)return;
    const HWND lists[]={g_hCmdList,g_hGesList,g_hHkList};const int counts[]={5,6,4};
    for(int row=0;row<SC_SETTINGS_LIST_COUNT;row++) {
        if(!IsWindow(lists[row]) || Header_GetItemCount(ListView_GetHeader(lists[row]))!=counts[row])continue;
        for(int col=0;col<counts[row];col++) {
            int encoded=MulDiv(ListView_GetColumnWidth(lists[row],col),96,g_dpiScale)+1;
            if(encoded!=g_settingsColumnBaseline[row][col]) {
                g_uiDraft.settingsColumnWidths[row][col]=encoded;
                g_settingsColumnBaseline[row][col]=encoded;
            }
        }
    }
}

static void Settings_RestoreColumns(int initial)
{
    const HWND lists[]={g_hCmdList,g_hGesList,g_hHkList};const int counts[]={5,6,4};
    g_settingsRestoringColumns=1;
    for(int row=0;row<SC_SETTINGS_LIST_COUNT;row++)for(int col=0;col<counts[row];col++) {
        int encoded=initial?g_cfg.settingsColumnWidths[row][col]:g_settingsColumnBaseline[row][col];
        if(encoded>0)ListView_SetColumnWidth(lists[row],col,SCALE(encoded-1));
        g_settingsColumnBaseline[row][col]=MulDiv(ListView_GetColumnWidth(lists[row],col),96,g_dpiScale)+1;
    }
    g_settingsRestoringColumns=0;g_settingsColumnsReady=1;
}

static void Settings_SaveSize(void)
{
    AppConfig *actual=UI_ActualConfig();
    if(!g_windowSizeSaveFailed && actual->settingsWidth==g_settingsWindowWidth &&
       actual->settingsHeight==g_settingsWindowHeight &&
       !memcmp(actual->settingsColumnWidths,g_uiDraft.settingsColumnWidths,sizeof(actual->settingsColumnWidths)))return;
    /* Geometry is independent of unapplied commands and preferences. */
    actual->settingsWidth=g_settingsWindowWidth;
    actual->settingsHeight=g_settingsWindowHeight;
    memcpy(actual->settingsColumnWidths,g_uiDraft.settingsColumnWidths,sizeof(actual->settingsColumnWidths));
    if(!Config_Save()) {
        g_windowSizeSaveFailed=1;
        SC_ReportError(SC_Text(L"Save settings window layout",L"保存设置窗口布局"),Config_GetLastError());
    }else g_windowSizeSaveFailed=0;
}

static RECT Settings_RestoreFrame(DWORD style,DWORD exStyle,const RECT *work)
{
    int width=g_cfg.settingsWidth,height=g_cfg.settingsHeight;
    if(width<SETS_W || width>SC_SETTINGS_MAX_SIZE)width=SETS_W;
    if(height<SETS_H || height>SC_SETTINGS_MAX_SIZE)height=SETS_H;
    RECT frame={0,0,SCALE(width),SCALE(height)};
    AdjustWindowRectEx(&frame,style,FALSE,exStyle);
    int w=frame.right-frame.left,h=frame.bottom-frame.top;
    if(w>work->right-work->left)w=work->right-work->left;
    if(h>work->bottom-work->top)h=work->bottom-work->top;
    frame.left=work->left+(work->right-work->left-w)/2;
    frame.top=work->top+(work->bottom-work->top-h)/2;
    frame.right=frame.left+w;frame.bottom=frame.top+h;
    return frame;
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hDlg=hWnd;g_hFont=UI_CreateFont();
        g_hWhiteBr = CreateSolidBrush(RGB(255,255,255));
        /* Tab 控件 */
        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|TCS_FIXEDWIDTH,
            SCALE(8),SCALE(8), SCALE(SETS_W-16), SCALE(322),
            hWnd, (HMENU)CID_TAB, g_hInst, NULL);
        SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hTab, TCM_SETITEMSIZE, 0, MAKELPARAM(SCALE(90),SCALE(24)));
        SetWindowTheme(g_hTab, L"", L"");
        { TCITEMW tci={0}; tci.mask=TCIF_TEXT;
          tci.pszText=L"General"; SendMessageW(g_hTab,TCM_INSERTITEMW,TAB_GENERAL,(LPARAM)&tci);
          tci.pszText=L"Dashboard"; SendMessageW(g_hTab,TCM_INSERTITEMW,TAB_DASHBOARD,(LPARAM)&tci);
          tci.pszText=L"Commands"; SendMessageW(g_hTab,TCM_INSERTITEMW,TAB_COMMANDS,(LPARAM)&tci);
          tci.pszText=L"Gestures"; SendMessageW(g_hTab,TCM_INSERTITEMW,TAB_GESTURES,(LPARAM)&tci);
          tci.pszText=L"Hotkeys"; SendMessageW(g_hTab,TCM_INSERTITEMW,TAB_HOTKEYS,(LPARAM)&tci); }

        int tabY = SCALE(34), btnY;

        Settings_CreatePages(hWnd);

        /* Commands Tab */
        { int y = tabY + SCALE(6);
          /* 筛选下拉 */
          { CreateWindowExW(0, L"STATIC", L"Type:", WS_CHILD|SS_LEFT,
                SCALE(18),y+SCALE(3),SCALE(44),SCALE(18), hWnd, (HMENU)5005, g_hInst, NULL);
            { HWND hC = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(70),y,SCALE(80),SCALE(120), hWnd,(HMENU)5002, g_hInst,NULL);
            SendMessageW(hC, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"All");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Custom");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Built-in");
            SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"Scripts");
            SendMessageW(hC, CB_SETCURSEL, 0, 0); }
            CreateWindowExW(0, L"STATIC", L"Group:", WS_CHILD|SS_LEFT,
                SCALE(162),y+SCALE(3),SCALE(44),SCALE(18), hWnd, (HMENU)5009, g_hInst, NULL);
            { HWND hG = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(214),y,SCALE(112),SCALE(120), hWnd,(HMENU)5008, g_hInst,NULL);
            SendMessageW(hG, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hG, CB_ADDSTRING, 0, (LPARAM)L"All");
            SendMessageW(hG, CB_SETCURSEL, 0, 0); } }
          int listY = y + SCALE(24);
          g_hCmdList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
              SCALE(18), listY, SCALE(SETS_W-36), SCALE(160), hWnd, (HMENU)CID_CMD_LIST, g_hInst, NULL);
          SendMessageW(g_hCmdList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hCmdList, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
          SetWindowTheme((HWND)SendMessageW(g_hCmdList, LVM_GETHEADER, 0, 0), L"", L"");
          LVCOLUMNW col={0}; col.mask=LVCF_TEXT|LVCF_WIDTH;
          col.cx=SCALE(120); col.pszText=L"Name"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,0,(LPARAM)&col);
          col.cx=SCALE(45);  col.pszText=L"Type"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,1,(LPARAM)&col);
          col.cx=SCALE(70);  col.pszText=L"Group"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,2,(LPARAM)&col);
          col.cx=SCALE(135); col.pszText=L"Target"; SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,3,(LPARAM)&col);
          col.cx=SCALE(70); col.pszText=L"Source";SendMessageW(g_hCmdList,LVM_INSERTCOLUMNW,4,(LPARAM)&col);
          btnY = listY + SCALE(168);
          CreateWindowExW(0, L"BUTTON", L"New...", WS_CHILD|WS_TABSTOP,
              SCALE(18),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(98),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(178),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_CMD_DEL, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Scan Apps...", WS_CHILD|WS_TABSTOP,
              SCALE(262),btnY,SCALE(95),SCALE(24), hWnd,(HMENU)CID_CMD_SCAN, g_hInst,NULL); }

        /* Gestures Tab */
        { int y = tabY + SCALE(6);
          /* Zone 筛选下拉 */
          { CreateWindowExW(0, L"STATIC", L"Zone:", WS_CHILD|SS_LEFT,
                SCALE(18),y+SCALE(3),SCALE(44),SCALE(18), hWnd, (HMENU)5006, g_hInst, NULL);
            { HWND hZ = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                SCALE(68),y,SCALE(100),SCALE(160), hWnd,(HMENU)5007, g_hInst,NULL);
            SendMessageW(hZ, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(hZ, CB_ADDSTRING, 0, (LPARAM)L"All");
            for(int zi=0;zi<ZONE_COUNT;zi++)SendMessageW(hZ,CB_ADDSTRING,0,(LPARAM)ZoneName(zi));
            SendMessageW(hZ, CB_SETCURSEL, 0, 0); } }
          int listY = y + SCALE(24);
          g_hGesList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,
              SCALE(18), listY, SCALE(SETS_W-36), SCALE(165), hWnd, (HMENU)CID_GES_LIST, g_hInst, NULL);
          SendMessageW(g_hGesList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hGesList, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
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
          col2.cx=SCALE(40);  col2.pszText=L"On"; SendMessageW(g_hGesList,LVM_INSERTCOLUMNW,5,(LPARAM)&col2);
          btnY = listY + SCALE(173);
          CreateWindowExW(0, L"BUTTON", L"Record...", WS_CHILD|WS_TABSTOP,
              SCALE(18),btnY,SCALE(80),SCALE(24), hWnd,(HMENU)CID_GES_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(103),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_GES_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(183),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_GES_DEL, g_hInst,NULL); }

        /* Hotkeys Tab */
        { int y = tabY + SCALE(6);
          g_hHkList = CreateWindowExW(0, WC_LISTVIEWW, L"",
              WS_CHILD|WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
              SCALE(18), y, SCALE(SETS_W-36), SCALE(185), hWnd, (HMENU)CID_HK_LIST, g_hInst, NULL);
          SendMessageW(g_hHkList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
          ListView_SetExtendedListViewStyle(g_hHkList, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
          SetWindowTheme((HWND)SendMessageW(g_hHkList, LVM_GETHEADER, 0, 0), L"", L"");
          LVCOLUMNW col3={0}; col3.mask=LVCF_TEXT|LVCF_WIDTH;
          col3.cx=SCALE(150); col3.pszText=L"Hotkey"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,0,(LPARAM)&col3);
          col3.cx=SCALE(125); col3.pszText=L"Description"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,1,(LPARAM)&col3);
          col3.cx=SCALE(108); col3.pszText=L"Command"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,2,(LPARAM)&col3);
          col3.cx=SCALE(40);  col3.pszText=L"On"; SendMessageW(g_hHkList,LVM_INSERTCOLUMNW,3,(LPARAM)&col3);
          btnY = y + SCALE(192);
          CreateWindowExW(0, L"BUTTON", L"New...", WS_CHILD|WS_TABSTOP,
              SCALE(18),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_NEW, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Edit...", WS_CHILD|WS_TABSTOP,
              SCALE(98),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_EDIT, g_hInst,NULL);
          CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD|WS_TABSTOP,
              SCALE(178),btnY,SCALE(75),SCALE(24), hWnd,(HMENU)CID_HK_DEL, g_hInst,NULL); }

        CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,SCALE(18),SCALE(72),SCALE(444),SCALE(22),hWnd,(HMENU)CID_LIST_SEARCH,g_hInst,NULL);
        SendDlgItemMessageW(hWnd,CID_LIST_SEARCH,EM_SETCUEBANNER,TRUE,(LPARAM)SC_Text(L"Search name, command or target",L"搜索名称、命令或目标"));
        CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,SCALE(18),SCALE(262),SCALE(444),SCALE(28),hWnd,(HMENU)CID_LIST_DETAIL,g_hInst,NULL);
        CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,SCALE(294),SCALE(340),SCALE(60),SCALE(26),hWnd,(HMENU)CID_CANCEL,g_hInst,NULL);
        g_hStatus = CreateWindowExW(0, L"STATIC", L"● Listening",
            WS_CHILD|WS_VISIBLE|SS_LEFT, SCALE(18), SCALE(344), SCALE(SETS_W-228), SCALE(18),
            hWnd,(HMENU)CID_STATUS, g_hInst,NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        /* Save + Help 按钮固定在右下角，所有标签页可见 */
        CreateWindowExW(0, L"BUTTON", L"Help",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(SETS_W-200), SCALE(340), SCALE(55), SCALE(24),
            hWnd,(HMENU)5001, g_hInst,NULL);
        CreateWindowExW(0, L"BUTTON", L"Save && Apply",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            SCALE(SETS_W-138), SCALE(340), SCALE(120), SCALE(24),
            hWnd,(HMENU)CID_BTN_SAVE, g_hInst,NULL);
        EnumChildWindows(hWnd,Settings_SetControlFont,(LPARAM)g_hFont);

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
        if (g_cfg.toggleLabel[0]) UI_SetUtf8(g_hToggleHK,g_cfg.toggleLabel);
        else SetWindowTextW(g_hToggleHK, L"Click to bind");
        SendMessageW(GetDlgItem(hWnd,CID_AUTOSTART), BM_SETCHECK, g_cfg.autoStart?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hWnd,3004), BM_SETCHECK, g_cfg.trailEnabled?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(hWnd,3005), BM_SETCHECK, g_cfg.zoneLinesEnabled?BST_CHECKED:BST_UNCHECKED,0);
        /* 初始化外观颜色变量 */
        g_zoneColor  = (COLORREF)g_cfg.zoneLineColor;
        g_glowColor  = (COLORREF)g_cfg.trailGlowColor;
        g_innerColor = (COLORREF)g_cfg.trailInnerColor;
        Settings_UpdateColorLabels(hWnd);
        Settings_UpdateEnabled(hWnd);
        /* 填充宽度输入框 */
        { wchar_t ws[8];
          swprintf(ws,8,L"%d",g_cfg.trailGlowWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_GLOW_WIDTH), ws);
          swprintf(ws,8,L"%d",g_cfg.trailInnerWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_INNER_WIDTH), ws);
          swprintf(ws,8,L"%d",g_cfg.zoneLineWidth);
          SetWindowTextW(GetDlgItem(hWnd,CID_ZONE_WIDTH), ws);
        }
        /* Script synchronization runs before the draft is opened. */
        SendMessageW(GetDlgItem(hWnd,5002), CB_SETCURSEL, (WPARAM)g_cmdFilter, 0);
        SendMessageW(GetDlgItem(hWnd,5007), CB_SETCURSEL, (WPARAM)g_gesFilter, 0);
        CmdList_Fill(); GesList_Fill(); HkList_Fill();

        g_activeTab = 0;
        SendMessageW(g_hTab, TCM_SETCURSEL, 0, 0);
        g_settingsDirty=0;
        Settings_ApplyLanguage(hWnd);
        Settings_RestoreColumns(1);
        Settings_LayoutLists(hWnd);
        Tab_Refresh(hWnd);
        return 0;
    }

    case WM_SC_DEFER_SAVE:
        UI_Apply(hWnd);return 0;

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
        /* Script synchronization runs before the draft is opened. */
        SendMessageW(GetDlgItem(hWnd,5002), CB_SETCURSEL, (WPARAM)g_cmdFilter, 0);
        { HWND hG = GetDlgItem(hWnd, 5008);
          if (hG) SendMessageW(hG, CB_SETCURSEL, (WPARAM)g_cmdGrpFilter, 0); }
        SendMessageW(GetDlgItem(hWnd,5007), CB_SETCURSEL, (WPARAM)g_gesFilter, 0);
        /* 刷新前排序 */
        if (g_cfg.commandCount > 1) qsort(g_cfg.commands, g_cfg.commandCount, sizeof(Command), CmpCmd);
        if (g_cfg.gestureCount > 1) qsort(g_cfg.gestures, g_cfg.gestureCount, sizeof(GestureDef), CmpGes);
        if (g_cfg.hotkeyCount  > 1) qsort(g_cfg.hotkeys,  g_cfg.hotkeyCount,  sizeof(HotkeyDef), CmpHk);
        CmdList_Fill(); GesList_Fill(); HkList_Fill();
        UI_Dirty();
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == g_hDashOpacity) {
            int opacity = (int)SendMessageW(g_hDashOpacity, TBM_GETPOS, 0, 0);
            wchar_t opacityText[16];
            swprintf(opacityText, 16, L"%d%%", opacity);
            SetWindowTextW(g_hDashOpacityValue, opacityText);
            UI_Dirty();return 0;
        }
        break;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR*)lp;
        if(g_settingsColumnsReady && !g_settingsRestoringColumns &&
           (nm->code==HDN_ENDTRACKW || nm->code==HDN_ENDTRACKA ||
            nm->code==HDN_ITEMCHANGEDW || nm->code==HDN_ITEMCHANGEDA)) {
            NMHEADERW *header=(NMHEADERW*)lp;
            const HWND lists[]={g_hCmdList,g_hGesList,g_hHkList};
            if(header->pitem && (header->pitem->mask&HDI_WIDTH))
                for(int i=0;i<SC_SETTINGS_LIST_COUNT;i++)if(nm->hwndFrom==ListView_GetHeader(lists[i])) {
                    SetTimer(hWnd,SETTINGS_VIEW_TIMER,250,NULL);return 0;
                }
        }
        if(nm->code==LVN_COLUMNCLICK && nm->idFrom==CID_CMD_LIST){NMLISTVIEW *event=(NMLISTVIEW*)lp;g_commandSortDescending=event->iSubItem==g_commandSortColumn?!g_commandSortDescending:0;g_commandSortColumn=event->iSubItem;CmdList_Fill();return 0;}
        if(nm->code==LVN_ITEMCHANGED && (nm->idFrom==CID_CMD_LIST || nm->idFrom==CID_GES_LIST || nm->idFrom==CID_HK_LIST)){Settings_UpdateDetail(nm->hwndFrom);return 0;}
        if(nm->code==NM_DBLCLK){if(nm->idFrom==CID_CMD_LIST)SendMessageW(hWnd,WM_COMMAND,CID_CMD_EDIT,0);if(nm->idFrom==CID_GES_LIST)SendMessageW(hWnd,WM_COMMAND,CID_GES_EDIT,0);if(nm->idFrom==CID_HK_LIST)SendMessageW(hWnd,WM_COMMAND,CID_HK_EDIT,0);return 0;}
        if (nm->idFrom == CID_TAB && nm->code == TCN_SELCHANGE) {
            g_activeTab = (int)SendMessageW(g_hTab, TCM_GETCURSEL, 0, 0);
            Tab_Refresh(hWnd); return 0;
        }
        break;
    }

    case WM_COMMAND: {
        if(!IsWindowEnabled(hWnd))return 0;
        WORD cid = LOWORD(wp), code = HIWORD(wp);
        if(cid==CID_CANCEL){DestroyWindow(hWnd);return 0;}
        if(cid==CID_LIST_SEARCH && code==EN_CHANGE){wchar_t text[SC_NAME_CAP];GetDlgItemTextW(hWnd,CID_LIST_SEARCH,text,SC_NAME_CAP);lstrcpynW(g_listQuery,text,SC_NAME_CAP);CmdList_Fill();GesList_Fill();HkList_Fill();return 0;}
        if(code==EN_CHANGE || code==BN_CLICKED || code==CBN_SELCHANGE) {if((cid>=3000 && cid<4000) || cid==CID_AUTOSTART)UI_Dirty();}
        if (cid == CID_LANGUAGE && code == CBN_SELCHANGE) {
            HWND combo = GetDlgItem(hWnd,CID_LANGUAGE);
            LRESULT row = SendMessageW(combo,CB_GETCURSEL,0,0);
            int language = (int)SendMessageW(combo,CB_GETITEMDATA,row,0);
            if (language >= 0 && language < UI_LANG_COUNT && language != g_cfg.uiLanguage) {
                g_cfg.uiLanguage = language;
                Settings_ApplyLanguage(hWnd);
                UI_Dirty();
            }
            return 0;
        }
        if ((cid == CID_DASH_QUICK || cid == 3004 || cid == 3005) && code == BN_CLICKED) {
            Settings_UpdateEnabled(hWnd);
            return 0;
        }
        if (cid == CID_DASH_THEME && code == CBN_SELCHANGE) {
            LRESULT row = SendMessageW(g_hDashTheme, CB_GETCURSEL, 0, 0);
            int theme = (int)SendMessageW(g_hDashTheme, CB_GETITEMDATA, row, 0);
            if (theme >= 0 && theme < DASH_THEME_COUNT) {
                g_cfg.dashboardTheme = theme;
                UI_Dirty();
            }
            return 0;
        }
        /* Commands */
        if (cid == 5002 && code == CBN_SELCHANGE) {
            g_cmdFilter = (int)SendMessageW(GetDlgItem(hWnd,5002), CB_GETCURSEL, 0, 0);
            CmdList_Fill(); return 0; }
        if (cid == 5008 && code == CBN_SELCHANGE) {
            g_cmdGrpFilter = (int)SendMessageW(GetDlgItem(hWnd,5008), CB_GETCURSEL, 0, 0);
            if(g_cmdGrpFilter>0 && g_cmdGrpFilter<=g_cmdGrpCount)SC_CopyUtf8(g_selectedGroup,sizeof(g_selectedGroup),g_cmdGrpNames[g_cmdGrpFilter-1]);else g_selectedGroup[0]=0;
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
                            SC_Text(L"Scripts cannot be deleted. Remove the file from scripts\\ folder.",L"无法在此删除脚本。请从 scripts 文件夹中移除对应文件。") :
                            SC_Text(L"Built-in commands cannot be deleted.",L"无法删除内置命令。"),
                            L"ShortCutter", MB_OK|MB_ICONINFORMATION); return 0; }
                    Config_UnbindCommand(&g_cfg,c->id);UI_Dirty();
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
                g_cfg.gestureCount--;UI_Dirty();GesList_Fill();
            } return 0; }
        /* Hotkeys */
        if (cid == CID_HK_NEW && code == BN_CLICKED) { HkDlg_Show(hWnd, -1); return 0; }
        if (cid == CID_HK_EDIT && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hHkList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) { HkDlg_Show(hWnd,HkListRowToIdx(sel)); } return 0; }
        if (cid == CID_HK_DEL && code == BN_CLICKED) {
            int sel = (int)SendMessageW(g_hHkList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0) {
                sel=HkListRowToIdx(sel);if(sel<0)return 0;UI_Dirty();
                for (int i = sel; i < g_cfg.hotkeyCount - 1; i++) g_cfg.hotkeys[i] = g_cfg.hotkeys[i+1];
                g_cfg.hotkeyCount--; HkList_Fill();
            } return 0; }
        /* 开关快捷键 */
        if (cid == CID_TOGGLE_HK && code == BN_CLICKED) {
            g_tglCap = 1; SetWindowTextA(g_hToggleHK, "...");
            SetWindowTextW(g_hStatus, SC_Text(L"⌨ Press key for toggle...",L"⌨ 请按启用 / 暂停快捷键...")); SetFocus(g_hToggleHK);
            return 0; }
        if(cid==CID_AUTOSTART && code==BN_CLICKED){UI_Dirty();return 0;}

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
                    *pClr = cc.rgbResult;UI_Dirty();
                    Settings_UpdateColorLabels(hWnd);
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
            if(!UI_BeginEditor(hWnd))return 0;
            int w = SCALE(600), h = SCALE(458);
            g_hHelpDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SC_HelpDlg",
                SC_Text(L"ShortCutter - Help",L"ShortCutter - 帮助"),
                WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
                (GetSystemMetrics(SM_CXSCREEN)-w)/2,
                (GetSystemMetrics(SM_CYSCREEN)-h)/2,
                w, h, hWnd, NULL, g_hInst, NULL);
            if(g_hHelpDlg){EnableWindow(hWnd,FALSE);UI_CenterOwned(g_hHelpDlg,hWnd);}
            return 0; }
        if(cid==CID_BTN_SAVE && code==BN_CLICKED){UI_Apply(hWnd);return 0;}

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
        if (ds->itemState & ODS_DISABLED) df |= DFCS_INACTIVE;
        DrawFrameControl(ds->hDC, &ds->rcItem, DFC_BUTTON, df);
        /* A color chip and readable value share the same picker button. */
        { HBRUSH hbr = CreateSolidBrush(clr);
          HPEN   hpn = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
          RECT rc = ds->rcItem;
          InflateRect(&rc, -SCALE(5), -SCALE(4));
          rc.right = rc.left + SCALE(18);
          if (ds->itemState & ODS_SELECTED) OffsetRect(&rc, 1, 1);
          HBRUSH oldBr = SelectObject(ds->hDC, hbr);
          HPEN   oldPn = SelectObject(ds->hDC, hpn);
          Rectangle(ds->hDC, rc.left, rc.top, rc.right, rc.bottom);
          SelectObject(ds->hDC, oldBr);
          SelectObject(ds->hDC, oldPn);
          DeleteObject(hpn); DeleteObject(hbr); }
        { wchar_t text[16];
          GetWindowTextW(ds->hwndItem,text,16);
          RECT rc = ds->rcItem;
          rc.left += SCALE(30); rc.right -= SCALE(5);
          if (ds->itemState & ODS_SELECTED) OffsetRect(&rc,1,1);
          HFONT old = SelectObject(ds->hDC,g_hFont);
          int oldMode = SetBkMode(ds->hDC,TRANSPARENT);
          COLORREF oldColor = SetTextColor(ds->hDC,GetSysColor(
              ds->itemState & ODS_DISABLED ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
          DrawTextW(ds->hDC,text,-1,&rc,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
          SetTextColor(ds->hDC,oldColor); SetBkMode(ds->hDC,oldMode);
          SelectObject(ds->hDC,old); }
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
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp,GetSysColor(IsWindowEnabled((HWND)lp) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_SIZE:
        if(wp!=SIZE_MINIMIZED && g_hTab)Settings_LayoutLists(hWnd);
        if(wp==SIZE_RESTORED)Settings_CaptureSize(hWnd);
        return 0;
    case WM_EXITSIZEMOVE:
        Settings_CaptureSize(hWnd);Settings_CaptureColumns();Settings_SaveSize();return 0;
    case WM_TIMER:
        if(wp==SETTINGS_VIEW_TIMER) {
            KillTimer(hWnd,SETTINGS_VIEW_TIMER);Settings_CaptureColumns();Settings_SaveSize();return 0;
        }
        break;
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;RECT r={0,0,SCALE(SETS_W),SCALE(SETS_H)};AdjustWindowRectEx(&r,(DWORD)GetWindowLongPtrW(hWnd,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(hWnd,GWL_EXSTYLE));info->ptMinTrackSize.x=r.right-r.left;info->ptMinTrackSize.y=r.bottom-r.top;return 0;}
    case WM_DPICHANGED:{
        Settings_CaptureColumns();
        UINT previous=g_dpiScale;g_dpiScale=HIWORD(wp);RECT *r=(RECT*)lp;
        for(HWND child=GetWindow(hWnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT cr;GetWindowRect(child,&cr);MapWindowPoints(NULL,hWnd,(POINT*)&cr,2);MoveWindow(child,MulDiv(cr.left,g_dpiScale,previous),MulDiv(cr.top,g_dpiScale,previous),MulDiv(cr.right-cr.left,g_dpiScale,previous),MulDiv(cr.bottom-cr.top,g_dpiScale,previous),FALSE);}
        HFONT old=g_hFont;g_hFont=UI_CreateFont();EnumChildWindows(hWnd,UI_SetFont,(LPARAM)g_hFont);if(old)DeleteObject(old);
        Settings_RestoreColumns(0);
        SetWindowPos(hWnd,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Settings_LayoutLists(hWnd);return 0;
    }
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        KillTimer(hWnd,SETTINGS_VIEW_TIMER);Settings_CaptureColumns();
        Settings_SaveSize();
        if(g_hCmdDlg)DestroyWindow(g_hCmdDlg);
        if(g_hRecDlg)DestroyWindow(g_hRecDlg);
        if(g_hHkDlg)DestroyWindow(g_hHkDlg);
        if(g_hScanDlg)DestroyWindow(g_hScanDlg);
        if(g_hHelpDlg)DestroyWindow(g_hHelpDlg);
        g_uiEditing=0;g_tglCap=0;g_settingsDirty=0;
        g_settingsColumnsReady=0;
        if(g_gestureImages){ImageList_Destroy(g_gestureImages);g_gestureImages=NULL;}
        g_hDlg = NULL;
        g_hDashOpacity = NULL;
        g_hDashOpacityValue = NULL;
        g_hDashTheme = NULL;
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


void UI_Show(HWND hParent)
{
    (void)hParent;
    if (g_hDlg) { SetForegroundWindow(g_hDlg); ShowWindow(g_hDlg, SW_SHOW); return; }
    Scripts_ScanAndSync();g_uiDraft=*UI_ActualConfig();g_uiEditing=1;g_settingsDirty=0;g_listQuery[0]=0;g_selectedGroup[0]=0;
    g_settingsColumnsReady=0;
    g_settingsWindowWidth=g_cfg.settingsWidth;g_settingsWindowHeight=g_cfg.settingsHeight;
    UI_RegisterClass();
    DWORD style = WS_OVERLAPPEDWINDOW|WS_VISIBLE;
    DWORD exStyle = WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT;
    RECT work = {0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    RECT frame=Settings_RestoreFrame(style,exStyle,&work);
    g_hDlg = CreateWindowExW(exStyle, L"ShortCutterSettings",
        SC_Text(L"ShortCutter - Settings",L"ShortCutter - 设置"), style,
        frame.left,frame.top,frame.right-frame.left,frame.bottom-frame.top, NULL, NULL, g_hInst, NULL);
    if (g_hWnd) SetWindowLongPtrW(g_hDlg, GWLP_HWNDPARENT, (LONG_PTR)g_hWnd);
}

BOOL UI_ProcessDialogMessage(MSG *message)
{
    HWND windows[]={g_hCmdDlg,g_hRecDlg,g_hHkDlg,g_hScanDlg,g_hHelpDlg,g_hDlg};
    for(int i=0;i<6;i++)if(windows[i] && IsWindowEnabled(windows[i]) &&
       (message->hwnd==windows[i] || IsChild(windows[i],message->hwnd))){
        if(message->message==WM_KEYDOWN){
            UINT code=(UINT)SendMessageW(message->hwnd,WM_GETDLGCODE,message->wParam,(LPARAM)message);
            if(!(code&DLGC_WANTALLKEYS) && message->wParam==VK_ESCAPE){SendMessageW(windows[i],WM_CLOSE,0,0);return TRUE;}
            if(!(code&(DLGC_WANTALLKEYS|DLGC_WANTMESSAGE)) && message->wParam==VK_RETURN){SendMessageW(windows[i],WM_COMMAND,i==5?CID_BTN_SAVE:i==4?102:101,0);return TRUE;}
        }
        return IsDialogMessageW(windows[i],message);
    }
    return FALSE;
}
