/* Exercise the original native settings controls with a private configuration.
   No global input hooks, real input injection, UI Automation clients or writes
   to the user's configuration are used. Timings are diagnostic, not proof of
   responsiveness on a different machine or under a different input device. */
#define main SettingsRegressionMain
#include "settings_resilience.c"
#undef main

static double Elapsed(LARGE_INTEGER start,LARGE_INTEGER frequency)
{
    LARGE_INTEGER end;QueryPerformanceCounter(&end);
    return 1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);GetCurrentDirectoryW(SC_PATH_CAP,g_exeDir);
    wchar_t temp[SC_PATH_CAP];
    if(!GetTempPathW(SC_PATH_CAP,temp) || !GetTempFileNameW(temp,L"scs",0,ini))return 1;
    if(!CopyFileW(L"ShortCutter.ini",ini,FALSE)){DeleteFileW(ini);return 1;}
    Config_SetDefaults();Config_Load();Open();
    g_activeTab=TAB_COMMANDS;TabCtrl_SetCurSel(g_hTab,g_activeTab);Tab_Refresh(g_hDlg);Pump();
    LARGE_INTEGER frequency,start,tick;QueryPerformanceFrequency(&frequency);
    int count=ListView_GetItemCount(g_hCmdList),moved=0;double worst=0;
    printf("Native list: %d rows\n",count);
    for(int block=0;block<10;block++){
        QueryPerformanceCounter(&start);
        for(int i=0;i<100;i++){
            MSG message={0};message.hwnd=g_hCmdList;message.message=WM_MOUSEWHEEL;
            message.wParam=MAKEWPARAM(0,(i/20)%2?WHEEL_DELTA:-WHEEL_DELTA);
            message.time=GetTickCount();QueryPerformanceCounter(&tick);
            if(!UI_ProcessDialogMessage(&message))DispatchMessageW(&message);
            Pump();double elapsed=Elapsed(tick,frequency);if(elapsed>worst)worst=elapsed;
            if(ListView_GetTopIndex(g_hCmdList)>0)moved=1;
        }
        printf("100 scroll events: %.2f ms\n",Elapsed(start,frequency));
    }
    QueryPerformanceCounter(&start);
    HWND button=GetDlgItem(g_hDlg,CID_CANCEL);
    for(int i=0;i<100;i++){
        SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(8,8));
        RedrawWindow(button,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
        SendMessageW(button,WM_MOUSELEAVE,0,0);Pump();
    }
    printf("100 native button hover/paint cycles: %.2f ms\n",Elapsed(start,frequency));
    printf("Longest scroll + message-pump iteration: %.2f ms\n",worst);
    Check(moved || count<=ListView_GetCountPerPage(g_hCmdList),"wheel moves the native list");
    Check(!saved && !startupCalls,"scroll and hover do not persist settings or change startup");
    Close();DeleteFileW(ini);return failures?1:0;
}
