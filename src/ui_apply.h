/* Settings use a private draft. File and startup changes occur only on Apply. */
typedef struct { wchar_t from[SC_PATH_CAP], to[SC_PATH_CAP]; int moved; } UI_ScriptMove;
static UI_ScriptMove g_uiMoves[MAX_COMMANDS];
static AppConfig g_uiBackup;

static int UI_Number(HWND parent, int id, int minimum, int maximum, int *value)
{
    wchar_t text[32], *end; GetWindowTextW(GetDlgItem(parent,id),text,32);
    errno=0; long number=wcstol(text,&end,10);
    if (!text[0] || *end || errno || number<minimum || number>maximum) {
        wchar_t message[160]; swprintf(message,160,SC_Text(L"Enter a whole number between %d and %d.",L"请输入 %d 到 %d 之间的整数。"),minimum,maximum);
        SetWindowTextW(g_hStatus,message); SetFocus(GetDlgItem(parent,id));
        SendDlgItemMessageW(parent,id,EM_SETSEL,0,-1); return 0;
    }
    *value=(int)number; return 1;
}
static int UI_ReadPreferences(HWND parent)
{
    if (!UI_Number(parent,3002,10,10000,&g_cfg.minGesture) ||
        !UI_Number(parent,3003,50,95,&g_cfg.matchThreshold) ||
        !UI_Number(parent,CID_GLOW_WIDTH,0,15,&g_cfg.trailGlowWidth) ||
        !UI_Number(parent,CID_INNER_WIDTH,1,15,&g_cfg.trailInnerWidth) ||
        !UI_Number(parent,CID_ZONE_WIDTH,1,15,&g_cfg.zoneLineWidth)) return 0;
    g_cfg.mouseButton=(int)SendMessageW(g_hMouseBtn,CB_GETCURSEL,0,0)+1;
    g_cfg.dashboardOpacity=(int)SendMessageW(g_hDashOpacity,TBM_GETPOS,0,0);
    const int ids[]={CID_DASH_QUICK,CID_DASH_QUICK_DESKTOP,CID_DASH_QUICK_EXPLORER,
        CID_AUTOSTART,3004,3005,CID_DASH_COMPACT};
    int *values[]={&g_cfg.dashboardQuickInput,&g_cfg.dashboardQuickDesktop,&g_cfg.dashboardQuickExplorer,
        &g_cfg.autoStart,&g_cfg.trailEnabled,&g_cfg.zoneLinesEnabled,&g_cfg.dashboardCompact};
    for(int i=0;i<7;i++) *values[i]=SendDlgItemMessageW(parent,ids[i],BM_GETCHECK,0,0)==BST_CHECKED;
    g_cfg.trailGlowColor=(int)g_glowColor; g_cfg.trailInnerColor=(int)g_innerColor; g_cfg.zoneLineColor=(int)g_zoneColor;
    return 1;
}
static int UI_PrepareScriptMoves(void)
{
    memset(g_uiMoves,0,sizeof(g_uiMoves));
    wchar_t scriptDir[SC_PATH_CAP], fullDir[SC_PATH_CAP];
    if(swprintf(scriptDir,SC_PATH_CAP,L"%ls\\scripts",GetExeDir())<0 ||
       !GetFullPathNameW(scriptDir,SC_PATH_CAP,fullDir,NULL)) return 0;
    for(int i=0;i<g_cfg.commandCount;i++) {
        Command *candidate=&g_cfg.commands[i]; const Command *old=NULL;
        for(int j=0;j<g_uiBackup.commandCount;j++) if(g_uiBackup.commands[j].id==candidate->id) {old=&g_uiBackup.commands[j];break;}
        if(!old || old->type!=CMD_SCRIPT || !strcmp(old->name,candidate->name))continue;
        wchar_t source[SC_PATH_CAP], absolute[SC_PATH_CAP], name[SC_NAME_CAP];
        if(!SC_WideFromUtf8(source,SC_PATH_CAP,old->runPath) || !GetFullPathNameW(source,SC_PATH_CAP,absolute,NULL))return 0;
        wchar_t *file=wcsrchr(absolute,L'\\'); if(!file)continue;
        wchar_t *extension=wcsrchr(file,L'.'); if(!extension)continue;
        wchar_t ext[32]; if(wcslen(extension)>=32)return 0; lstrcpyW(ext,extension);
        *file=0;
        /* A script outside the program's scripts folder gets a display name only. */
        if(lstrcmpiW(absolute,fullDir))continue;
        if(!SC_WideFromUtf8(name,SC_NAME_CAP,candidate->name) || !UI_ValidFileName(name))return 0;
        UI_ScriptMove *move=&g_uiMoves[i]; lstrcpyW(move->from,source);
        if(swprintf(move->to,SC_PATH_CAP,L"%ls\\%ls%ls",fullDir,name,ext)<0)return 0;
        if(!lstrcmpW(move->from,move->to)) {move->from[0]=0;continue;}
        if(GetFileAttributesW(move->to)!=INVALID_FILE_ATTRIBUTES && lstrcmpiW(move->from,move->to)) {SetLastError(ERROR_FILE_EXISTS);return 0;}
        if(!SC_Utf8FromWide(candidate->runPath,sizeof(candidate->runPath),move->to))return 0;
    }
    return 1;
}
static void UI_RollbackScriptMoves(void)
{
    for(int i=MAX_COMMANDS-1;i>=0;i--) if(g_uiMoves[i].moved) {
        if(!MoveFileW(g_uiMoves[i].to,g_uiMoves[i].from))SC_ReportError(SC_Text(L"Restore script name",L"还原脚本名称"),GetLastError());
        g_uiMoves[i].moved=0;
    }
}
static int UI_Apply(HWND parent)
{
    Settings_CaptureColumns();
    if(!UI_ReadPreferences(parent))return 0;
    AppConfig *actual=UI_ActualConfig(); g_uiBackup=*actual;
    /* Keep runtime usage and Dashboard edits made while Settings was open. */
    memcpy(g_cfg.usage,actual->usage,sizeof(g_cfg.usage));
    memcpy(g_cfg.dockItems,actual->dockItems,sizeof(g_cfg.dockItems));
    g_cfg.dockCount=actual->dockCount; g_cfg.dockPageCount=actual->dockPageCount; g_cfg.enabled=g_enabled;
    if(g_cfg.nextCmdId<actual->nextCmdId)g_cfg.nextCmdId=actual->nextCmdId;
    for(int i=0;i<actual->commandCount;i++)if(UI_FindCommand(actual->commands[i].id)<0)Config_UnbindCommand(&g_cfg,actual->commands[i].id);
    Config_Validate(&g_cfg);
    int requestedStartup=g_cfg.autoStart, startupChanged=requestedStartup!=actual->autoStart;
    DWORD failure=ERROR_SUCCESS; int startupFailed=0;
    if(!UI_PrepareScriptMoves())failure=GetLastError()?GetLastError():ERROR_INVALID_NAME;
    for(int i=0;i<g_cfg.commandCount && !failure;i++)if(g_uiMoves[i].from[0]) {
        if(!MoveFileW(g_uiMoves[i].from,g_uiMoves[i].to))failure=GetLastError();else g_uiMoves[i].moved=1;
    }
    if(!failure && startupChanged && !Config_AutoStart(requestedStartup)) {
        startupFailed=1;
        failure=Config_GetLastError()?Config_GetLastError():ERROR_ACCESS_DENIED;
    }
    if(!failure) {
        *actual=g_cfg;
        if(!Config_Save())failure=Config_GetLastError()?Config_GetLastError():ERROR_WRITE_FAULT;
    }
    if(failure) {
        /* Reverse system state before restoring the saved configuration. */
        if(startupChanged && actual->autoStart==requestedStartup && !Config_AutoStart(g_uiBackup.autoStart))
            SC_ReportError(SC_Text(L"Restore startup preference",L"还原开机启动设置"),Config_GetLastError());
        *actual=g_uiBackup; UI_RollbackScriptMoves();
        /* Script paths in the draft must remain original so retry is safe. */
        for(int i=0;i<g_cfg.commandCount;i++)if(g_uiMoves[i].from[0])SC_Utf8FromWide(g_cfg.commands[i].runPath,sizeof(g_cfg.commands[i].runPath),g_uiMoves[i].from);
        SetWindowTextW(g_hStatus,failure==ERROR_CANCELLED?
            SC_Text(L"Save cancelled; changes have not been applied",L"已取消保存，更改尚未应用"):
            SC_Text(L"Save failed; changes have not been applied",L"保存失败，更改尚未应用"));
        SC_ReportError(startupFailed?SC_Text(L"Change Windows startup setting",L"更改开机启动设置"):
            SC_Text(L"Apply settings",L"应用设置"),failure); return 0;
    }
    g_uiDraft=*actual; g_settingsDirty=0;g_windowSizeSaveFailed=0;
    Gesture_Cancel(); QuickInput_Cancel(); Hotkeys_Register();
    Dashboard_ApplyOpacity(); Dashboard_ApplyTheme(); Dashboard_ApplyLayout(); Dashboard_ApplyLanguage(); Tray_ApplyLanguage();
    SetWindowTextW(g_hStatus,SC_Text(L"Saved and applied",L"已保存并应用")); return 1;
}
