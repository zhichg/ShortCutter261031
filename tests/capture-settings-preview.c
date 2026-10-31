/* Reuses the isolated Settings host. Persistence uses a private config copy;
   startup registration, command execution and global input are not enabled. */
#define main SettingsRegressionMain
#include "settings_resilience.c"
#undef main

int main(void)
{
    g_hInst=GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    HDC desktop=GetDC(NULL);
    g_dpiScale=GetDeviceCaps(desktop,LOGPIXELSX);ReleaseDC(NULL,desktop);
    if(g_dpiScale<96)g_dpiScale=96;
    GetCurrentDirectoryW(SC_PATH_CAP,g_exeDir);
    swprintf(ini,SC_PATH_CAP,L"%ls\\design-review\\implementation-2026-10-07\\preview-state.ini",g_exeDir);
    wchar_t source[SC_PATH_CAP];swprintf(source,SC_PATH_CAP,L"%ls\\ShortCutter.ini",g_exeDir);
    if(!CopyFileW(source,ini,FALSE))return 1;
    Config_Load();Open();Language(UI_LANG_CHINESE);Pump();
    const int tabs[]={TAB_GENERAL,TAB_DASHBOARD,TAB_COMMANDS,TAB_GESTURES,TAB_HOTKEYS};
    const wchar_t *files[]={L"settings-general",L"settings-dashboard",L"settings-commands",L"settings-gestures",L"settings-hotkeys"};
    for(int i=0;i<5;i++) {
        g_activeTab=tabs[i];TabCtrl_SetCurSel(g_hTab,g_activeTab);Tab_Refresh(g_hDlg);Pump();
        wchar_t file[SC_PATH_CAP];swprintf(file,SC_PATH_CAP,L"design-review\\implementation-2026-10-07\\%ls.bmp",files[i]);
        Capture(g_hDlg,file);
    }
    for(int i=0;i<g_uiDraft.commandCount;i++)if(!g_uiDraft.commands[i].locked && g_uiDraft.commands[i].type==CMD_RUN) {
        CmdDlg_Show(g_hDlg,i);Pump();Capture(g_hCmdDlg,L"design-review\\implementation-2026-10-07\\command-editor.bmp");DestroyWindow(g_hCmdDlg);break;
    }
    RecDlg_Show(g_hDlg,g_uiDraft.gestureCount?0:-1);Pump();Capture(g_hRecDlg,L"design-review\\implementation-2026-10-07\\gesture-recorder.bmp");DestroyWindow(g_hRecDlg);
    SendMessageW(g_hDlg,WM_COMMAND,5001,0);Pump();Capture(g_hHelpDlg,L"design-review\\implementation-2026-10-07\\help.bmp");DestroyWindow(g_hHelpDlg);Close();
    DeleteFileW(ini);
    printf("Preview DPI=%d; persistence calls=%d; startup calls=%d\n",g_dpiScale,saved,startupCalls);
    return saved || startupCalls || reported;
}
