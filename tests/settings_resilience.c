/* Actual Settings + configuration; system changes and execution are replaced. */
#include "../src/main.h"
#include <stdlib.h>
AppConfig g_cfg;
int g_enabled=1,g_dpiScale=96;
HINSTANCE g_hInst;HWND g_hWnd;HMENU g_hTrayMenu;
wchar_t g_exeDir[SC_PATH_CAP];static wchar_t ini[SC_PATH_CAP];
static int checks,failures,saved,startupCalls,startupFail,saveFail,reported,boxes,applies;
static BYTE keys[256];
const wchar_t *GetExeDir(void){return g_exeDir;}
const wchar_t *GetIniPath(void){return ini;}
int Dashboard_GetHistCount(void){return 0;}
const char *Dashboard_GetHistItem(int i){(void)i;return NULL;}
void Dashboard_ClearHist(void){}
void Dashboard_AddHistItem(const char *text){(void)text;}
int Dashboard_GetDockPos(int i){return i;}
void Dashboard_SetDockPos(int i,int pos){(void)i;(void)pos;}
#define Config_Save Fixture_SaveDisk
#define Config_AutoStart Fixture_UnusedAutoStart
#define Scripts_ScanAndSync Fixture_UnusedScriptScan
#include "../src/config.c"
#undef Config_Save
#undef Config_AutoStart
#undef Scripts_ScanAndSync
BOOL Config_Save(void){saved++;if(saveFail){Config_Error(ERROR_WRITE_FAULT);return FALSE;}return Fixture_SaveDisk();}
BOOL Config_AutoStart(int enabled){startupCalls++;if(startupFail){Config_Error((DWORD)startupFail);return FALSE;}g_cfg.autoStart=enabled;return TRUE;}
void Scripts_ScanAndSync(void){}
void Dashboard_ApplyOpacity(void){applies++;}
void Dashboard_ApplyTheme(void){}
void Dashboard_ApplyLayout(void){}
void Dashboard_ApplyLanguage(void){}
void Tray_ApplyLanguage(void){}
void Action_ExecuteCustom(const GestureDef *gesture){(void)gesture;}
void Action_NoteUserInput(void){}
int Dashboard_HandleOutsideClick(POINT point){(void)point;return 0;}
void QuickInput_Cancel(void){}
void Hotkeys_Register(void){}
#include "../src/gesture.c"
int Action_ValidateKeySeq(const char *seq,wchar_t *out,size_t count){if(strstr(seq,"BAD")){lstrcpynW(out,L"Invalid key",(int)count);return 0;}return 1;}
void Action_FormatKeySeq(const char *seq,wchar_t *out,size_t count){SC_WideFromUtf8(out,count,seq);}
const wchar_t *SC_GetInputStatusText(void){return SC_Text(L"Listening",L"监听中");}
void SC_ReportError(const wchar_t *operation,DWORD error){(void)operation;(void)error;reported++;}
static int TestBox(HWND window,LPCWSTR text,LPCWSTR title,UINT flags){(void)window;(void)text;(void)title;(void)flags;boxes++;return IDYES;}
static SHORT TestKeys(int vk){return keys[vk]?(SHORT)0x8000:0;}
#define MessageBoxW TestBox
#define GetKeyState TestKeys
#ifndef SETTINGS_UI_SOURCE
#define SETTINGS_UI_SOURCE "../src/ui.c"
#endif
#include SETTINGS_UI_SOURCE
#undef MessageBoxW
#undef GetKeyState
#undef g_cfg
#undef SC_Text
static void Check(int condition,const char *name){checks++;printf("%s: %s\n",condition?"PASS":"FAIL",name);if(!condition)failures++;}
static void Pump(void){MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){if(!UI_ProcessDialogMessage(&m)){TranslateMessage(&m);DispatchMessageW(&m);}}}
static Command *Add(int id,const char *name){Command *c=&g_cfg.commands[g_cfg.commandCount++];memset(c,0,sizeof(*c));c->id=id;c->type=CMD_RUN;SC_CopyUtf8(c->name,sizeof(c->name),name);SC_CopyUtf8(c->runPath,sizeof(c->runPath),"fixture.exe");SC_CopyUtf8(c->group,sizeof(c->group),"Apps");g_cfg.nextCmdId=id+1;return c;}
static void Open(void){UI_Show(NULL);Pump();}
static void Close(void){if(g_hDlg)DestroyWindow(g_hDlg);Pump();}
static void Language(int language){HWND combo=GetDlgItem(g_hDlg,CID_LANGUAGE);SendMessageW(combo,CB_SETCURSEL,language==UI_LANG_CHINESE?0:1,0);SendMessageW(g_hDlg,WM_COMMAND,MAKEWPARAM(CID_LANGUAGE,CBN_SELCHANGE),(LPARAM)combo);}
static int ActualIndex(int id){for(int i=0;i<g_cfg.commandCount;i++)if(g_cfg.commands[i].id==id)return i;return -1;}
static void Capture(HWND w,const wchar_t *file){RECT r;GetClientRect(w,&r);HDC dc=GetDC(w),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom),old=SelectObject(mem,bitmap);PrintWindow(w,mem,PW_CLIENTONLY);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;size_t size=(size_t)r.right*r.bottom*4;void *pixels=malloc(size);GetDIBits(mem,bitmap,0,r.bottom,pixels,&info,DIB_RGB_COLORS);BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=(DWORD)(header.bfOffBits+size);FILE *out=_wfopen(file,L"wb");if(out){fwrite(&header,sizeof(header),1,out);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,out);fwrite(pixels,size,1,out);fclose(out);}free(pixels);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(w,dc);}
static void ResizeClient(int width,int height)
{
    RECT frame={0,0,MulDiv(width,g_dpiScale,96),MulDiv(height,g_dpiScale,96)};
    AdjustWindowRectEx(&frame,(DWORD)GetWindowLongPtrW(g_hDlg,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(g_hDlg,GWL_EXSTYLE));
    SetWindowPos(g_hDlg,NULL,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER);Pump();
}
static void TestSizeMemory(void)
{
    int actualMinimum=g_cfg.minGesture,actualLanguage=g_cfg.uiLanguage;
    Open();SetDlgItemTextW(g_hDlg,3002,L"288");Language(actualLanguage==UI_LANG_CHINESE?UI_LANG_ENGLISH:UI_LANG_CHINESE);
    int before=saved;ResizeClient(640,480);
    Check(saved==before,"dragging updates dimensions without saving on every size message");
    SendMessageW(g_hDlg,WM_EXITSIZEMOVE,0,0);
    Check(saved==before+1 && g_cfg.settingsWidth==640 && g_cfg.settingsHeight==480,"drag end saves the normal client width and height");
    Check(g_cfg.minGesture==actualMinimum && g_cfg.uiLanguage==actualLanguage,"saving geometry excludes unapplied preferences and language preview");Close();
    Check(saved==before+1,"closing after drag end avoids an unnecessary second save");
    g_cfg.settingsWidth=g_cfg.settingsHeight=0;Config_Load();Open();RECT client;GetClientRect(g_hDlg,&client);
    Check(client.right==640 && client.bottom==480,"reloading persisted configuration restores width and height on reopen");
    ResizeClient(620,460);before=saved;Check(UI_Apply(g_hDlg) && g_cfg.settingsWidth==620 && g_cfg.settingsHeight==460,"Apply includes a pending resize in the same configuration transaction");Close();
    Check(saved==before+1,"Apply followed by close saves the geometry only once");
    Open();before=saved;ShowWindow(g_hDlg,SW_MAXIMIZE);Pump();Close();
    Check(saved==before && g_cfg.settingsWidth==620 && g_cfg.settingsHeight==460,"closing a maximized window retains its normal dimensions");
    Open();before=saved;ShowWindow(g_hDlg,SW_MINIMIZE);Pump();Close();
    Check(saved==before && g_cfg.settingsWidth==620 && g_cfg.settingsHeight==460,"closing a minimized window retains its normal dimensions");
    g_dpiScale=144;Open();GetClientRect(g_hDlg,&client);
    Check(client.right==930 && client.bottom==690,"stored logical dimensions restore correctly at 150 percent scaling");Close();g_dpiScale=96;
    Open();before=saved;int oldReported=reported;ResizeClient(740,540);saveFail=1;SendMessageW(g_hDlg,WM_EXITSIZEMOVE,0,0);
    Check(g_windowSizeSaveFailed && reported==oldReported+1 && GetPrivateProfileIntW(L"Settings",L"SettingsWindowWidth",0,ini)==620,"geometry save failure reports an error and preserves the previous disk value");
    saveFail=0;SendMessageW(g_hDlg,WM_EXITSIZEMOVE,0,0);
    Check(!g_windowSizeSaveFailed && saved==before+2 && GetPrivateProfileIntW(L"Settings",L"SettingsWindowWidth",0,ini)==740,"a failed geometry save can retry even when the dimensions have not changed");Close();
    Open();ResizeClient(700,510);SetDlgItemTextW(g_hDlg,3002,L"999");Close();Config_Load();
    Check(g_cfg.settingsWidth==700 && g_cfg.settingsHeight==510 && g_cfg.minGesture==actualMinimum,"closing without a drag-end message saves size and discards unapplied preferences");
    Open();g_uiDraft.settingsWidth=5000;g_uiDraft.settingsHeight=4000;RECT smallWork={-1024,-720,-424,-220};
    RECT frame=Settings_RestoreFrame((DWORD)GetWindowLongPtrW(g_hDlg,GWL_STYLE),(DWORD)GetWindowLongPtrW(g_hDlg,GWL_EXSTYLE),&smallWork);
    Check(frame.left>=smallWork.left && frame.top>=smallWork.top && frame.right<=smallWork.right && frame.bottom<=smallWork.bottom,"restoring a large saved size fits a smaller workspace with a negative origin");Close();
}
static void PumpFor(DWORD duration)
{
    DWORD start=GetTickCount();do {Pump();Sleep(5);}while(GetTickCount()-start<duration);Pump();
}
static void TestColumnMemory(void)
{
    int minimum=g_cfg.minGesture,language=g_cfg.uiLanguage;
    Open();int before=saved;SetDlgItemTextW(g_hDlg,3002,L"288");Language(language==UI_LANG_CHINESE?UI_LANG_ENGLISH:UI_LANG_CHINESE);
    ListView_SetColumnWidth(g_hCmdList,0,212);ListView_SetColumnWidth(g_hCmdList,3,384);
    ListView_SetColumnWidth(g_hGesList,5,172);ListView_SetColumnWidth(g_hHkList,2,264);
    Check(saved==before,"column resizing batches persistence instead of writing on every header update");PumpFor(330);
    Check(saved==before+1 && g_cfg.settingsColumnWidths[0][0]==213 && g_cfg.settingsColumnWidths[0][3]==385 &&
        g_cfg.settingsColumnWidths[1][5]==173 && g_cfg.settingsColumnWidths[2][2]==265,
        "native header notifications automatically persist independent widths for all three lists");
    Check(g_cfg.minGesture==minimum && g_cfg.uiLanguage==language,
        "column autosave excludes unapplied preferences and language preview");Close();
    Check(saved==before+1,"closing after column autosave avoids a second configuration write");Config_Load();Open();
    Check(ListView_GetColumnWidth(g_hCmdList,0)==212 && ListView_GetColumnWidth(g_hCmdList,3)==384 &&
        ListView_GetColumnWidth(g_hGesList,5)==172 && ListView_GetColumnWidth(g_hHkList,2)==264,
        "disk reload and reopen restore all saved list widths");
    Language(language==UI_LANG_CHINESE?UI_LANG_ENGLISH:UI_LANG_CHINESE);SetDlgItemTextW(g_hDlg,CID_LIST_SEARCH,L"B");
    Check(ListView_GetColumnWidth(g_hCmdList,0)==212 && ListView_GetColumnWidth(g_hGesList,5)==172,
        "language preview and list filtering preserve column widths");
    before=saved;ListView_SetColumnWidth(g_hCmdList,4,0);Close();Config_Load();Open();
    Check(saved==before+1 && g_cfg.settingsColumnWidths[0][4]==1 && ListView_GetColumnWidth(g_hCmdList,4)==0,
        "immediate close saves a hidden zero-width column before the debounce timer fires");
    before=saved;ListView_SetColumnWidth(g_hCmdList,0,240);
    Check(UI_Apply(g_hDlg) && g_cfg.settingsColumnWidths[0][0]==241,
        "Apply includes pending column widths in its configuration transaction");Close();
    Check(saved==before+1,"Apply and subsequent close save pending columns exactly once");
    Open();RECT window;GetWindowRect(g_hDlg,&window);
    RECT scaled={window.left,window.top,window.left+MulDiv(window.right-window.left,144,96),window.top+MulDiv(window.bottom-window.top,144,96)};
    SendMessageW(g_hDlg,WM_DPICHANGED,MAKEWPARAM(144,144),(LPARAM)&scaled);
    Check(ListView_GetColumnWidth(g_hCmdList,0)==360 && ListView_GetColumnWidth(g_hCmdList,3)==576 &&
        ListView_GetColumnWidth(g_hCmdList,4)==0 && ListView_GetColumnWidth(g_hGesList,5)==258,
        "DPI change scales remembered columns and preserves hidden columns");Close();Config_Load();Open();
    Check(ListView_GetColumnWidth(g_hCmdList,0)==360 && g_cfg.settingsColumnWidths[0][0]==241,
        "reopen at 150 percent DPI retains logical column width without compounding scaling");
    SendMessageW(g_hDlg,WM_DPICHANGED,MAKEWPARAM(96,96),(LPARAM)&window);Close();
    Open();before=saved;int errors=reported;ListView_SetColumnWidth(g_hCmdList,0,300);saveFail=1;
    SendMessageW(g_hDlg,WM_TIMER,SETTINGS_VIEW_TIMER,0);
    Check(g_windowSizeSaveFailed && reported==errors+1 && GetPrivateProfileIntW(L"Settings",L"CommandColumn0",0,ini)==240,
        "column save failure reports the error and preserves the last disk layout");
    saveFail=0;SendMessageW(g_hDlg,WM_TIMER,SETTINGS_VIEW_TIMER,0);
    Check(!g_windowSizeSaveFailed && saved==before+2 && GetPrivateProfileIntW(L"Settings",L"CommandColumn0",0,ini)==300,
        "unchanged columns can retry persistence after a save failure");Close();
}
static void DrawRecorderPath(void)
{
    const POINT points[]={{30,100},{30,80},{30,60},{60,60},{90,60},{120,60}};
    SendMessageW(g_hRecCanvas,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(points[0].x,points[0].y));
    for(int i=1;i<6;i++)SendMessageW(g_hRecCanvas,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(points[i].x,points[i].y));
    SendMessageW(g_hRecCanvas,WM_RBUTTONUP,0,MAKELPARAM(points[5].x,points[5].y));
    Pump();
}
static void TestRecorderCaptureLifecycle(void)
{
    static AppConfig original;original=g_cfg;
    int originalCount=g_cfg.gestureCount;
    Open();Language(UI_LANG_CHINESE);RecDlg_Show(g_hDlg,-1);Pump();
    DrawRecorderPath();
    Check(!g_recording && GetCapture()!=g_hRecCanvas && g_recTotalSamples==1 &&
        g_recPtCount==6 && g_recSampleCounts[0]==6,
        "right-button release retains the completed trace and first sample after native capture notification");
    wchar_t directions[128],label[64];GetWindowTextW(g_hRecChain,directions,128);GetWindowTextW(g_hRecSampLabel,label,64);
    Check(!wcscmp(directions,L"↑ →") && !wcscmp(label,L"采样：1/3"),
        "completed recording displays encoded directions and the updated sample count");
    SendMessageW(g_hRecCanvas,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(20,20));
    SendMessageW(g_hRecCanvas,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(100,20));
    SetCapture(g_hDlg); /* Real WM_CAPTURECHANGED interrupts this unfinished stroke. */
    Check(!g_recording && !g_recPtCount && g_recTotalSamples==1 && g_recSampleCounts[0]==6,
        "capture stolen during a new stroke discards only that stroke and retains completed samples");
    if(GetCapture()==g_hDlg)ReleaseCapture();
    SendMessageW(g_hRecCanvas,WM_RBUTTONUP,0,MAKELPARAM(100,20));
    Check(g_recTotalSamples==1,"late right-button release after cancellation does not add a sample");
    DrawRecorderPath();DrawRecorderPath();
    Check(g_recTotalSamples==3 && g_recSampleCounts[1]==6 && g_recSampleCounts[2]==6,
        "three consecutive right-drag recordings retain every completed sample");
    DrawRecorderPath();
    Check(g_recTotalSamples==3 && !g_recording && GetCapture()!=g_hRecCanvas,
        "a fourth recording cannot replace saved samples or acquire capture");
    SetWindowTextW(g_hRecName,L"Recorder capture regression");UI_FillCommandChoice(g_hRecCmdEdit,901);
    SendMessageW(g_hRecDlg,WM_COMMAND,101,0);Pump();
    int created=!g_hRecDlg && g_uiDraft.gestureCount==originalCount+1;
    Check(created && g_uiDraft.gestures[originalCount].chainCount==3 &&
        !strcmp(g_uiDraft.gestures[originalCount].chains[0],"03") &&
        g_uiDraft.gestures[originalCount].ptsStr[0][0] && g_cfg.gestureCount==originalCount,
        "confirming a recorded gesture preserves directions and points in the settings draft");
    int applied=created && UI_Apply(g_hDlg);Close();if(applied)Config_Load();
    Check(applied && g_cfg.gestureCount==originalCount+1 &&
        g_cfg.gestures[originalCount].chainCount==3 && g_cfg.gestures[originalCount].enabled &&
        !strcmp(g_cfg.gestures[originalCount].chains[0],"03") &&
        !strcmp(g_cfg.gestures[originalCount].ptsStr[0],"30,100;30,80;30,60;60,60;90,60;120,60;"),
        "Apply and disk reload preserve all recorded samples, directions and trace points");
    if(applied){
        Open();RecDlg_Show(g_hDlg,originalCount);Pump();
        Check(g_recTotalSamples==3 && g_recSampleCounts[0]==6 &&
            g_recSamples[0][5].x==120 && g_recSamples[0][5].y==60,
            "reopening the saved gesture restores its recorded paths");
        SendMessageW(g_hRecDlg,WM_COMMAND,104,0);
        Check(!g_recTotalSamples && !g_recPtCount && !GetWindowTextLengthW(g_hRecChain),
            "Clear still removes completed paths and directions");
        DestroyWindow(g_hRecDlg);Close();
    }
    g_cfg=original;Fixture_SaveDisk();
}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    GetCurrentDirectoryW(SC_PATH_CAP,g_exeDir);wcscat(g_exeDir,L"\\tests\\settings-fixture");CreateDirectoryW(g_exeDir,NULL);swprintf(ini,SC_PATH_CAP,L"%ls\\configuration.ini",g_exeDir);
    Config_SetDefaults();g_cfg.autoStart=0;Add(901,"A");Add(902,"B");Add(903,"C");Fixture_SaveDisk();
    Open();Language(UI_LANG_CHINESE);SetDlgItemTextW(g_hDlg,3002,L"144");
    Check(g_hDlg && UI_GetLanguage()==UI_LANG_CHINESE && g_cfg.uiLanguage==UI_LANG_ENGLISH && !saved,"language preview and settings edits stay in a private draft");Close();
    Check(g_cfg.minGesture==30 && g_cfg.uiLanguage==UI_LANG_ENGLISH && !startupCalls,"closing discards draft without startup or persistence writes");
    Open();SetDlgItemTextW(g_hDlg,3002,L"0");Check(!UI_Apply(g_hDlg) && !saved && g_cfg.minGesture==30 && GetFocus()==GetDlgItem(g_hDlg,3002),"invalid number blocks Apply and focuses the offending field");SetDlgItemTextW(g_hDlg,3002,L"144");
    saveFail=1;Check(!UI_Apply(g_hDlg) && g_cfg.minGesture==30 && g_uiDraft.minGesture==144 && reported==1,"save failure restores live configuration and keeps the draft for retry");saveFail=0;
    Check(UI_Apply(g_hDlg) && g_cfg.minGesture==144 && !startupCalls && applies==1,"successful Apply persists values without touching unchanged startup");Close();
    Open();SendDlgItemMessageW(g_hDlg,CID_AUTOSTART,BM_SETCHECK,BST_CHECKED,0);startupFail=ERROR_ACCESS_DENIED;
    Check(!UI_Apply(g_hDlg) && !g_cfg.autoStart && g_uiDraft.autoStart && startupCalls==1,"startup denial preserves live state and keeps requested draft");
    startupFail=ERROR_CANCELLED;int savedBeforeCancel=saved;
    Check(!UI_Apply(g_hDlg) && !g_cfg.autoStart && g_uiDraft.autoStart && saved==savedBeforeCancel &&
        GetPrivateProfileIntW(L"Settings",L"AutoStart",-1,ini)==0,
        "cancelled administrator approval keeps live and persisted startup unchanged and retains the draft");
    wchar_t cancelStatus[160];GetWindowTextW(g_hStatus,cancelStatus,160);
    Check(!wcscmp(cancelStatus,L"Save cancelled; changes have not been applied"),"cancelled approval is distinguished from a save error in settings");
    startupFail=0;saveFail=1;
    Check(!UI_Apply(g_hDlg) && !g_cfg.autoStart && startupCalls==4,"save failure reverses an already-applied startup change through the mock boundary");saveFail=0;
    Check(UI_Apply(g_hDlg) && g_cfg.autoStart && g_uiDraft.autoStart &&
        GetPrivateProfileIntW(L"Settings",L"AutoStart",0,ini)==1,"retry after cancelled approval saves and applies the retained startup preference");
    SendDlgItemMessageW(g_hDlg,CID_AUTOSTART,BM_SETCHECK,BST_UNCHECKED,0);startupFail=ERROR_CANCELLED;
    Check(!UI_Apply(g_hDlg) && g_cfg.autoStart && !g_uiDraft.autoStart &&
        GetPrivateProfileIntW(L"Settings",L"AutoStart",0,ini)==1,"cancelled disable retains the enabled live and disk state and unchecked draft");
    startupFail=0;Check(UI_Apply(g_hDlg) && !g_cfg.autoStart && !g_uiDraft.autoStart &&
        GetPrivateProfileIntW(L"Settings",L"AutoStart",1,ini)==0,"approved disable commits the unchecked startup preference");Close();
    Open();int draftB=UI_FindCommand(902);CmdDlg_Show(g_hDlg,draftB);Pump();
    Check(g_hCmdDlg && !IsWindowEnabled(g_hDlg),"command editor disables parent mutation while editing");
    Command temp=g_uiDraft.commands[draftB];g_uiDraft.commands[draftB]=g_uiDraft.commands[draftB+1];g_uiDraft.commands[draftB+1]=temp;
    wchar_t longName[81];for(int i=0;i<80;i++)longName[i]=L'中';longName[80]=0;SetWindowTextW(g_hCmdDlgName,longName);SendMessageW(g_hCmdDlg,WM_COMMAND,101,0);Pump();
    int b=UI_FindCommand(902),c=UI_FindCommand(903);wchar_t name[SC_NAME_CAP];SC_WideFromUtf8(name,SC_NAME_CAP,g_uiDraft.commands[b].name);
    Check(!g_hCmdDlg && IsWindowEnabled(g_hDlg) && !wcscmp(name,longName) && !strcmp(g_uiDraft.commands[c].name,"C"),"stable ID edits the intended command after reordering and preserves 80 Chinese characters");
    Check(!strcmp(g_cfg.commands[ActualIndex(902)].name,"B"),"confirming an editor does not mutate the running command");Close();
    Open();g_uiDraft.hotkeyCount=1;HotkeyDef *existing=&g_uiDraft.hotkeys[0];memset(existing,0,sizeof(*existing));existing->enabled=1;existing->mods=SC_MOD_CTRL;existing->vk='K';existing->commandId=901;
    HkDlg_Show(g_hDlg,-1);Pump();g_hkCapMods=SC_MOD_CTRL;g_hkCapVk='K';SC_CopyUtf8(g_hkCapLabel,sizeof(g_hkCapLabel),"Ctrl+K");int beforeBoxes=boxes;SendMessageW(g_hHkDlg,WM_COMMAND,101,0);
    Check(g_hHkDlg && boxes==beforeBoxes+1 && g_uiDraft.hotkeyCount==1,"enabled duplicate shortcut is rejected before any binding mutation");
    g_hkCapVk='L';UI_FillCommandChoice(g_hHkDlgCmdEdit,902);SendMessageW(g_hHkDlg,WM_COMMAND,101,0);Pump();
    int foundBinding=0;for(int i=0;i<g_uiDraft.hotkeyCount;i++)if(g_uiDraft.hotkeys[i].vk=='L' && g_uiDraft.hotkeys[i].commandId==902)foundBinding=1;
    Check(!g_hHkDlg && foundBinding,"command selector stores a stable command ID");
    keys[VK_RWIN]=1;Check(UI_CurrentMods()==SC_MOD_WIN,"right Win key is recognized when capturing a binding");keys[VK_RWIN]=0;Close();
    Open();RecDlg_Show(g_hDlg,-1);Pump();int modifier=2;SendMessageW(g_hRecMod,CB_SETCURSEL,modifier,0);
    Check(SendMessageW(g_hRecMod,CB_GETITEMDATA,modifier,0)==SC_MOD_SHIFT,"recorder Shift label uses the Shift modifier bit");
    SendMessageW(g_hRecCanvas,WM_RBUTTONDOWN,0,MAKELPARAM(5,5));SendMessageW(g_hRecCanvas,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(-15,-20));
    Check(g_recPts[g_recPtCount-1].x>=0 && g_recPts[g_recPtCount-1].y>=0,"recording clamps signed coordinates when dragging outside the canvas");
    SendMessageW(g_hRecCanvas,WM_CANCELMODE,0,0);Check(!g_recording && !g_recPtCount,"lost capture cancels unfinished recording");DestroyWindow(g_hRecDlg);Close();
    TestRecorderCaptureLifecycle();
    Open();Language(UI_LANG_CHINESE);g_activeTab=TAB_COMMANDS;Tab_Refresh(g_hDlg);SetDlgItemTextW(g_hDlg,CID_LIST_SEARCH,L"B");
    Check(ListView_GetItemCount(g_hCmdList)==1 && UI_ListData(g_hCmdList,0)==902,"text search retains the intended command ID");
    SetWindowTextW(GetDlgItem(g_hDlg,CID_LIST_SEARCH),L"");
    RECT initial;GetWindowRect(g_hCmdList,&initial);RECT client;GetClientRect(g_hDlg,&client);SetWindowPos(g_hDlg,NULL,0,0,client.right+180,client.bottom+180,SWP_NOMOVE|SWP_NOZORDER);RECT larger;GetWindowRect(g_hCmdList,&larger);
    Check(larger.right-larger.left>initial.right-initial.left && larger.bottom-larger.top>initial.bottom-initial.top,"resizing expands the list in both directions");Close();

    Open();g_activeTab=TAB_GENERAL;Tab_Refresh(g_hDlg);HWND number=GetDlgItem(g_hDlg,3002);SetFocus(number);
    MSG navigation={0};navigation.hwnd=number;navigation.message=WM_KEYDOWN;navigation.wParam=VK_TAB;
    Check(UI_ProcessDialogMessage(&navigation) && GetFocus()!=number && IsChild(g_hDlg,GetFocus()),"Tab moves focus between visible settings controls");
    SetDlgItemTextW(g_hDlg,3002,L"166");navigation.hwnd=GetDlgItem(g_hDlg,CID_BTN_SAVE);navigation.wParam=VK_RETURN;
    Check(UI_ProcessDialogMessage(&navigation) && g_cfg.minGesture==166,"Enter on Apply commits validated preferences");
    SetDlgItemTextW(g_hDlg,3002,L"177");navigation.hwnd=number;navigation.wParam=VK_ESCAPE;
    Check(UI_ProcessDialogMessage(&navigation) && !g_hDlg && g_cfg.minGesture==166,"Escape closes settings and discards later draft edits");Pump();
    Open();CmdDlg_Show(g_hDlg,UI_FindCommand(902));Pump();navigation.hwnd=g_hCmdDlgName;navigation.wParam=VK_ESCAPE;
    Check(UI_ProcessDialogMessage(&navigation) && !g_hCmdDlg && IsWindowEnabled(g_hDlg),"Escape closes an editor and restores its parent");Close();

    Open();RECT field,window;GetWindowRect(GetDlgItem(g_hDlg,3002),&field);GetWindowRect(g_hDlg,&window);
    RECT scaled={window.left,window.top,window.left+MulDiv(window.right-window.left,144,96),window.top+MulDiv(window.bottom-window.top,144,96)};
    SendMessageW(g_hDlg,WM_DPICHANGED,MAKEWPARAM(144,144),(LPARAM)&scaled);RECT changed;GetWindowRect(GetDlgItem(g_hDlg,3002),&changed);
    Check(g_dpiScale==144 && changed.right-changed.left==MulDiv(field.right-field.left,144,96),"DPI change resizes settings fields without compressing their text");
    SendMessageW(g_hDlg,WM_DPICHANGED,MAKEWPARAM(96,96),(LPARAM)&window);Close();

    g_cfg.gestureCount=1;GestureDef *gesture=&g_cfg.gestures[0];memset(gesture,0,sizeof(*gesture));SC_CopyUtf8(gesture->name,sizeof(gesture->name),"Sample gesture");SC_CopyUtf8(gesture->chains[0],sizeof(gesture->chains[0]),"0302");gesture->chainCount=1;gesture->enabled=1;gesture->commandId=901;
    Open();g_activeTab=TAB_GESTURES;Tab_Refresh(g_hDlg);DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);for(int i=0;i<32;i++)GesList_Fill();
    Check(ImageList_GetImageCount(g_gestureImages)==1 && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdiBefore+1,"gesture thumbnail refresh does not accumulate GDI objects");
    RecDlg_Show(g_hDlg,0);Pump();wchar_t directions[512];GetWindowTextW(g_hRecChain,directions,512);
    Check(!wcscmp(directions,L"↑ → ↑ ←"),"recorder shows the same readable direction arrows as its list");DestroyWindow(g_hRecDlg);Close();

    /* A .lnk result's selection remains set across source filtering. */
    g_scanCount=2;g_scanFilter=0;memset(g_scanEntries,0,sizeof(ScanEntry)*2);g_scanEntries[0].source=0;g_scanEntries[0].selected=1;g_scanEntries[1].source=1;
    HWND owner=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,400,200,NULL,NULL,g_hInst,NULL);g_hScanList=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|LVS_REPORT,0,0,400,200,owner,NULL,g_hInst,NULL);ListView_SetExtendedListViewStyle(g_hScanList,LVS_EX_CHECKBOXES);
    ScanDlg_Fill();g_scanFilter=2;ScanDlg_Fill();g_scanFilter=0;ScanDlg_Fill();Check(g_scanEntries[0].selected && ListView_GetCheckState(g_hScanList,0),"scanner selection survives source filtering");DestroyWindow(owner);g_hScanList=NULL;g_scanCount=0;
    Open();g_scanCount=2;memset(g_scanEntries,0,sizeof(ScanEntry)*2);for(int i=0;i<2;i++){g_scanEntries[i].selected=1;lstrcpyW(g_scanEntries[i].shortcut,i?L"different-arguments.lnk":L"original-arguments.lnk");}lstrcpyW(g_scanEntries[0].name,L"Scanner Entry");for(int i=0;i<200;i++)g_scanEntries[1].name[i]=L'中';
    int countBefore=g_uiDraft.commandCount,idBefore=g_uiDraft.nextCmdId;
    Check(!ScanDlg_Import(g_hDlg) && g_uiDraft.commandCount==countBefore && g_uiDraft.nextCmdId==idBefore && !g_scanEntries[0].added,"invalid later scan entry leaves the entire selection and ID allocator unchanged");
    lstrcpyW(g_scanEntries[1].name,L"Scanner Entry");g_uiDraft.nextCmdId=INT_MAX-1;
    Check(!ScanDlg_Import(g_hDlg) && g_uiDraft.commandCount==countBefore && g_uiDraft.nextCmdId==INT_MAX-1 && !g_scanEntries[0].added,"ID exhaustion rejects the whole import without a partial first command");g_uiDraft.nextCmdId=idBefore;
    Check(ScanDlg_Import(g_hDlg) && g_uiDraft.commandCount==countBefore+2 && strcmp(g_uiDraft.commands[countBefore].name,g_uiDraft.commands[countBefore+1].name) && !strcmp(g_uiDraft.commands[countBefore].runPath,"original-arguments.lnk") && !strcmp(g_uiDraft.commands[countBefore+1].runPath,"different-arguments.lnk") && !g_uiDraft.commands[countBefore].activateExisting,"scanner keeps different shortcut entries and gives duplicate names distinct suffixes");
    g_scanEntries[0].added=0;g_scanEntries[1].selected=0;g_uiDraft.commandCount=MAX_COMMANDS;
    Check(!ScanDlg_Import(g_hDlg) && g_uiDraft.commandCount==MAX_COMMANDS && !g_scanEntries[0].added,"scanner reports capacity exhaustion before changing the draft");Close();g_scanCount=0;
    wchar_t scriptDir[SC_PATH_CAP],oldScript[SC_PATH_CAP],newScript[SC_PATH_CAP];swprintf(scriptDir,SC_PATH_CAP,L"%ls\\scripts",g_exeDir);CreateDirectoryW(scriptDir,NULL);swprintf(oldScript,SC_PATH_CAP,L"%ls\\Before.ps1",scriptDir);swprintf(newScript,SC_PATH_CAP,L"%ls\\After.ps1",scriptDir);FILE *file=_wfopen(oldScript,L"wb");if(file){fputs("fixture only",file);fclose(file);}Command *script=Add(904,"Before");script->type=CMD_SCRIPT;script->locked=1;SC_Utf8FromWide(script->runPath,sizeof(script->runPath),oldScript);
    Open();SC_CopyUtf8(g_uiDraft.commands[UI_FindCommand(904)].name,SC_NAME_CAP,"After");saveFail=1;
    Check(!UI_Apply(g_hDlg) && GetFileAttributesW(oldScript)!=INVALID_FILE_ATTRIBUTES && GetFileAttributesW(newScript)==INVALID_FILE_ATTRIBUTES,"failed Apply reverses project script rename");saveFail=0;
    Check(UI_Apply(g_hDlg) && GetFileAttributesW(newScript)!=INVALID_FILE_ATTRIBUTES && GetFileAttributesW(oldScript)==INVALID_FILE_ATTRIBUTES,"successful Apply commits the script filename and configuration together");Close();
    Open();Language(UI_LANG_CHINESE);g_activeTab=TAB_GENERAL;Tab_Refresh(g_hDlg);Capture(g_hDlg,L"design-review\\implementation-2026-10-07\\settings-general.bmp");g_activeTab=TAB_DASHBOARD;Tab_Refresh(g_hDlg);Capture(g_hDlg,L"design-review\\implementation-2026-10-07\\settings-dashboard.bmp");g_activeTab=TAB_COMMANDS;Tab_Refresh(g_hDlg);Capture(g_hDlg,L"design-review\\implementation-2026-10-07\\settings-commands.bmp");
    CmdDlg_Show(g_hDlg,UI_FindCommand(902));Pump();Capture(g_hCmdDlg,L"design-review\\implementation-2026-10-07\\command-editor.bmp");DestroyWindow(g_hCmdDlg);
    g_activeTab=TAB_GESTURES;Tab_Refresh(g_hDlg);Capture(g_hDlg,L"design-review\\implementation-2026-10-07\\settings-gestures.bmp");RecDlg_Show(g_hDlg,0);Pump();Capture(g_hRecDlg,L"design-review\\implementation-2026-10-07\\gesture-recorder.bmp");DestroyWindow(g_hRecDlg);
    SendMessageW(g_hDlg,WM_COMMAND,5001,0);Pump();Capture(g_hHelpDlg,L"design-review\\implementation-2026-10-07\\help.bmp");DestroyWindow(g_hHelpDlg);Close();
    TestSizeMemory();
    TestColumnMemory();
    DeleteFileW(newScript);RemoveDirectoryW(scriptDir);DeleteFileW(ini);wchar_t backup[SC_PATH_CAP];swprintf(backup,SC_PATH_CAP,L"%ls.bak",ini);DeleteFileW(backup);RemoveDirectoryW(g_exeDir);
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
