/* Real matcher, editor and persistence with project-local settings only. */
#define main SettingsFixtureMain
#include "settings_resilience.c"
#undef main

static void SetGesture(GestureDef *gesture,int zone)
{
    memset(gesture,0,sizeof(*gesture));gesture->enabled=1;gesture->chainCount=1;
    gesture->commandId=901;gesture->zone=zone;strcpy(gesture->chains[0],"3");
    snprintf(gesture->name,sizeof(gesture->name),"Zone %d",zone);
}
static void TestZoneMatching(void)
{
    g_cfg.matchThreshold=70;g_cfg.gestureCount=1;
    for(int scope=ZONE_MAIN;scope<ZONE_ALL;scope++){
        SetGesture(&g_cfg.gestures[0],scope);
        for(int start=ZONE_MAIN;start<ZONE_ALL;start++){
            char description[100];snprintf(description,sizeof(description),"scope=%d starting zone=%d matches only its own region",scope,start);
            Check(Gesture_Match("3",1,0,start)==(scope==start?0:-1),description);
        }
    }
    SetGesture(&g_cfg.gestures[0],ZONE_ALL);
    for(int start=ZONE_MAIN;start<ZONE_ALL;start++){
        char description[100];snprintf(description,sizeof(description),"All areas matches starting zone=%d",start);
        Check(Gesture_Match("3",1,0,start)==0,description);
    }
    Check(Gesture_Match("3",1,0,-1)==-1 && Gesture_Match("3",1,0,ZONE_ALL)==-1,"All areas is a binding scope, not a physical starting region");
    g_cfg.gestureCount=2;SetGesture(&g_cfg.gestures[1],ZONE_TL);
    Check(Gesture_Match("3",1,0,ZONE_TL)==1,"equal-score local binding wins over an earlier All areas binding");
    SetGesture(&g_cfg.gestures[0],ZONE_TL);SetGesture(&g_cfg.gestures[1],ZONE_ALL);
    Check(Gesture_Match("3",1,0,ZONE_TL)==0,"equal-score local binding also wins when ordered before All areas");
    Check(Gesture_Match("3",1,0,ZONE_TR)==1,"All areas remains available in a region without a matching local binding");
    g_cfg.gestureCount=1;SetGesture(&g_cfg.gestures[0],ZONE_ALL);g_cfg.gestures[0].modifier=SC_MOD_CTRL;
    Check(Gesture_Match("3",1,0,ZONE_BR)==-1 && Gesture_Match("3",1,SC_MOD_CTRL,ZONE_BR)==0,"All areas still requires the configured modifier");
    g_cfg.gestures[0].enabled=0;
    Check(Gesture_Match("3",1,SC_MOD_CTRL,ZONE_BR)==-1,"disabled All areas gestures cannot fire");
    Check(Gesture_GetZone(450,450,900,900)==ZONE_MAIN && Gesture_GetZone(10,10,900,900)==ZONE_TL &&
          Gesture_GetZone(890,10,900,900)==ZONE_TR && Gesture_GetZone(10,890,900,900)==ZONE_BL &&
          Gesture_GetZone(890,890,900,900)==ZONE_BR,"screen points retain the existing five distinct physical regions");
}
static int FindZone(int zone)
{
    for(int i=0;i<g_cfg.gestureCount;i++)if(g_cfg.gestures[i].zone==zone)return i;
    return -1;
}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    wchar_t project[SC_PATH_CAP];GetCurrentDirectoryW(SC_PATH_CAP,project);
    swprintf(g_exeDir,SC_PATH_CAP,L"%ls\\tests\\gesture-zones-%lu",project,GetCurrentProcessId());
    CreateDirectoryW(g_exeDir,NULL);swprintf(ini,SC_PATH_CAP,L"%ls\\configuration.ini",g_exeDir);
    Config_SetDefaults();TestZoneMatching();
    Config_SetDefaults();g_cfg.autoStart=0;Add(901,"Zone fixture");g_cfg.gestureCount=ZONE_COUNT;
    for(int zone=0;zone<ZONE_COUNT;zone++)SetGesture(&g_cfg.gestures[zone],zone);
    g_cfg.gestures[ZONE_TL].zone=ZONE_COUNT;Config_Validate(&g_cfg);
    Check(g_cfg.gestures[ZONE_TL].zone==ZONE_MAIN && g_cfg.gestures[ZONE_ALL].zone==ZONE_ALL,"invalid persisted scopes recover to Main while an explicit All areas scope remains valid");
    g_cfg.gestures[ZONE_TL].zone=ZONE_TL;
    Check(Fixture_SaveDisk(),"all six binding scopes save to a private project-local configuration");
    g_cfg.gestureCount=0;Config_Load();int all=FindZone(ZONE_ALL),mainZone=FindZone(ZONE_MAIN),valid=1;
    for(int zone=0;zone<ZONE_COUNT;zone++)if(FindZone(zone)<0)valid=0;
    Check(valid && all>=0 && mainZone>=0,"configuration reload preserves all scopes including the original Main value");
    if(all<0 || mainZone<0)return 1;
    Open();g_activeTab=TAB_GESTURES;Tab_Refresh(g_hDlg);
    wchar_t text[80]=L"";
    SendDlgItemMessageW(g_hDlg,5007,CB_GETLBTEXT,ZONE_ALL+1,(LPARAM)text);
    Check(SendDlgItemMessageW(g_hDlg,5007,CB_GETCOUNT,0,0)==ZONE_COUNT+1 && !wcscmp(text,L"All areas"),"English region filter distinguishes all gestures from the All areas scope");
    SendDlgItemMessageW(g_hDlg,5007,CB_SETCURSEL,ZONE_ALL+1,0);
    SendMessageW(g_hDlg,WM_COMMAND,MAKEWPARAM(5007,CBN_SELCHANGE),0);
    Check(ListView_GetItemCount(g_hGesList)==1 && UI_ListData(g_hGesList,0)==all,"All areas filter shows only globally scoped gestures");
    Language(UI_LANG_CHINESE);SendDlgItemMessageW(g_hDlg,5007,CB_GETLBTEXT,ZONE_ALL+1,(LPARAM)text);
    Check(!wcscmp(text,L"全部区域") && ListView_GetItemCount(g_hGesList)==1,"Chinese filter labels the new scope and retains the current filter");
    RecDlg_Show(g_hDlg,all);Pump();
    SendMessageW(g_hRecZone,CB_GETLBTEXT,ZONE_ALL,(LPARAM)text);
    Check(SendMessageW(g_hRecZone,CB_GETCOUNT,0,0)==ZONE_COUNT && !wcscmp(text,L"全部区域") && SendMessageW(g_hRecZone,CB_GETCURSEL,0,0)==ZONE_ALL,"recording editor offers and restores the All areas scope");
    Capture(g_hRecDlg,L"tests\\gesture-zones-editor.bmp");
    SendMessageW(g_hRecZone,CB_SHOWDROPDOWN,TRUE,0);Pump();
    COMBOBOXINFO info={0};info.cbSize=sizeof(info);GetComboBoxInfo(g_hRecZone,&info);
    if(info.hwndList)Capture(info.hwndList,L"tests\\gesture-zones-dropdown.bmp");
    SendMessageW(g_hRecZone,CB_SHOWDROPDOWN,FALSE,0);
    SendMessageW(g_hRecZone,CB_SETCURSEL,ZONE_MAIN,0);SendMessageW(g_hRecDlg,WM_COMMAND,101,0);Pump();
    Check(!g_hRecDlg && g_uiDraft.gestures[all].zone==ZONE_MAIN && g_cfg.gestures[all].zone==ZONE_ALL,"editing a region changes only the draft until Apply");
    RecDlg_Show(g_hDlg,all);Pump();SendMessageW(g_hRecZone,CB_SETCURSEL,ZONE_ALL,0);SendMessageW(g_hRecDlg,WM_COMMAND,101,0);Pump();
    Check(!g_hRecDlg && g_uiDraft.gestures[all].zone==ZONE_ALL,"editor saves the new All areas selection without remapping existing scopes");
    Check(UI_Apply(g_hDlg),"Apply persists the selected scope through the normal settings transaction");Close();Config_Load();
    Check(FindZone(ZONE_ALL)>=0,"All areas remains selected after Apply and configuration reload");
    DeleteFileW(ini);wchar_t backup[SC_PATH_CAP];swprintf(backup,SC_PATH_CAP,L"%ls.bak",ini);DeleteFileW(backup);RemoveDirectoryW(g_exeDir);
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
