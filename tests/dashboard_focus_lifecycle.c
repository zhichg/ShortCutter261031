/* Actual Win32 Dashboard lifecycle with private configuration and actions.
   No hooks, injected input, system settings or application execution. */
#include "../src/main.h"
AppConfig g_cfg;int g_enabled=1,g_dpiScale=96;HINSTANCE g_hInst;HWND g_hWnd;HMENU g_hTrayMenu;wchar_t g_exeDir[SC_PATH_CAP];
static int checks,failures,executed,visibleAtExecute,errors,denyFocus,attachCalls,detachCalls;
static HWND foreignWindow;static HANDLE foreignReady;static BYTE keys[256];
BOOL Config_Save(void){return TRUE;}DWORD Config_GetLastError(void){return 0;}
void Scripts_ScanAndSync(void){}
void SC_ReportError(const wchar_t *op,DWORD err){(void)op;(void)err;errors++;}
void Action_ExecuteCommand(int id){executed=id;visibleAtExecute=Dashboard_IsVisible();}
static SHORT TestKey(int vk){return keys[vk]?(SHORT)0x8000:0;}
static HWND TestForeground(void){return denyFocus?foreignWindow:GetForegroundWindow();}
static BOOL TestSetForeground(HWND w){return denyFocus?FALSE:SetForegroundWindow(w);}
static BOOL TestAttach(DWORD a,DWORD b,BOOL attach)
{
    if(attach)attachCalls++;else detachCalls++;
    return denyFocus?FALSE:AttachThreadInput(a,b,attach);
}
#define GetKeyState TestKey
#define GetAsyncKeyState TestKey
#define GetForegroundWindow TestForeground
#define SetForegroundWindow TestSetForeground
#define AttachThreadInput TestAttach
#include "../src/dashboard.c"
#undef GetKeyState
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef SetForegroundWindow
#undef AttachThreadInput
static void Check(int result,const char *text){checks++;printf("%s: %s\n",result?"PASS":"FAIL",text);if(!result)failures++;}
static void Pump(void){MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static void PumpFor(DWORD duration){DWORD start=GetTickCount();do{Pump();Sleep(5);}while(GetTickCount()-start<duration);Pump();}
static LRESULT CALLBACK Sink(HWND w,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_SC_DASHICONS){Dashboard_AcceptIcons();return 0;}
    return DefWindowProcW(w,message,wp,lp);
}
static LRESULT CALLBACK ForeignProc(HWND w,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,message,wp,lp);
}
static DWORD WINAPI ForeignThread(void *unused)
{
    (void)unused;WNDCLASSW wc={0};wc.lpfnWndProc=ForeignProc;wc.hInstance=g_hInst;wc.lpszClassName=L"SC_FocusForeign";RegisterClassW(&wc);
    foreignWindow=CreateWindowW(wc.lpszClassName,L"Focus fixture",WS_OVERLAPPEDWINDOW,10,10,180,120,NULL,NULL,g_hInst,NULL);
    ShowWindow(foreignWindow,SW_SHOWNOACTIVATE);SetEvent(foreignReady);MSG m;
    while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
}
static int SearchReady(void)
{
    return Dashboard_IsVisible() && GetForegroundWindow()==g_hDash && GetFocus()==g_hSearchEdit &&
        (GetWindowLongPtrW(g_hDash,GWL_EXSTYLE)&WS_EX_TOPMOST) && !g_dashOpening;
}
static POINT Outside(void){RECT r;GetWindowRect(g_hDash,&r);POINT point={r.right+2,r.bottom+2};return point;}
static void Press(UINT vk){Dashboard_HandlePhysicalKey(WM_KEYDOWN,vk,0,0);Dashboard_HandlePhysicalKey(WM_KEYUP,vk,0,0);}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc={0};wc.lpfnWndProc=Sink;wc.hInstance=g_hInst;wc.lpszClassName=L"SC_FocusOwner";RegisterClassW(&wc);
    g_hWnd=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,NULL,NULL,g_hInst,NULL);
    g_cfg.dashboardOpacity=100;g_cfg.commandCount=1;g_cfg.commands[0].id=91;g_cfg.commands[0].type=CMD_KEYS;
    SC_CopyUtf8(g_cfg.commands[0].name,SC_NAME_CAP,"Notepad");g_cfg.dockCount=g_cfg.dockPageCount=1;g_cfg.dockItems[0].commandId=91;Dashboard_AddHistItem("Notepad");
    foreignReady=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,ForeignThread,NULL,0,NULL);
    Check(thread && WaitForSingleObject(foreignReady,3000)==WAIT_OBJECT_0 && IsWindow(foreignWindow),"foreign GUI input queue is available for focus transfer checks");
    SetForegroundWindow(foreignWindow);Dashboard_Show();PumpFor(80);
    Check(SearchReady(),"opening from a foreign GUI queue makes Dashboard topmost and focuses its search on its own queue");
    GUITHREADINFO info={0};info.cbSize=sizeof(info);GetGUIThreadInfo(GetCurrentThreadId(),&info);
    Check(info.hwndFocus==g_hSearchEdit && ImmGetContext(g_hSearchEdit)==NULL,
        "GUI thread focus belongs to the search and IME remains disabled after activation");
    SetFocus(g_hDockPane);Dashboard_Show();Pump();
    Check(SearchReady(),"repeated Show uses the same topmost and search-focus activation path");
    SetWindowPos(g_hDash,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    Check((GetWindowLongPtrW(g_hDash,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"visible Dashboard stays in the topmost window group");
    SetFocus(g_hDockAdd);Press('A');
    Check(GetFocus()==g_hSearchEdit && !wcscmp(g_sbText,L"a"),"physical typing recovers search focus from a button");
    SetFocus(NULL);Press('B');
    Check(GetFocus()==g_hSearchEdit && !wcscmp(g_sbText,L"ab"),"physical typing recovers missing child focus");
    SetFocus(g_hDockPane);Press('C');
    Check(GetFocus()==g_hSearchEdit && !wcscmp(g_sbText,L"abc"),"typing letters in the grid returns directly to search");
    RECT bounds;GetWindowRect(g_hDash,&bounds);POINT inside={bounds.left+20,bounds.top+20};
    Check(!Dashboard_HandleOutsideClick(inside) && Dashboard_IsVisible(),"a click inside Dashboard keeps it visible");
    Check(Dashboard_HandleOutsideClick(Outside()) && !Dashboard_IsVisible() && !g_dashOpening,
        "outside click closes Dashboard and cancels focus retry state");
    Dashboard_Show();Pump();executed=0;Press(VK_RETURN);
    Check(executed==91 && !visibleAtExecute && !Dashboard_IsVisible(),"reopen and Enter execute history only after hiding Dashboard");
    Dashboard_Show();Pump();executed=0;Press(VK_SPACE);
    Check(executed==91 && !visibleAtExecute && !Dashboard_IsVisible(),"reopen and Space execute history only after hiding Dashboard");
    Dashboard_Show();Pump();Press('N');
    Check(!wcscmp(g_sbText,L"n") && GetFocus()==g_hSearchEdit,"another reopen accepts the first physical English letter immediately");
    SearchBox_SetText(L"");Dash_ActivateCommand(91,1);g_dashOpening=1;g_dashFocusAttempts=8;SetTimer(g_hDash,DASH_FOCUS_TIMER,40,NULL);
    Check(Dashboard_HandleOutsideClick(Outside()) && !g_pendingCount && !g_dashOpening,
        "outside click during activation cancels pending commands and delayed focus attempts");
    SendMessageW(g_hDash,WM_TIMER,DASH_FOCUS_TIMER,0);Check(!Dashboard_IsVisible(),"stale focus timer cannot reopen a dismissed Dashboard");
    Dashboard_Show();Pump();HWND popup=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP|WS_VISIBLE,bounds.left+10,bounds.top+10,20,20,g_hDash,NULL,g_hInst,NULL);
    Check(GetWindow(g_hDash,GW_ENABLEDPOPUP)==popup && Dashboard_HandleOutsideClick(Outside()) && !Dashboard_IsVisible(),
        "an enabled auxiliary owned window cannot block outside-click dismissal");DestroyWindow(popup);
    Dashboard_Show();Pump();g_dockDialog++;SetFocus(g_hDockAdd);Dashboard_Show();
    Check(!Dashboard_HandleOutsideClick(Outside()) && !Dashboard_HandlePhysicalKey(WM_KEYDOWN,'D',0,0) && GetFocus()==g_hDockAdd,
        "modal dialog guard preserves the current input and rejects outside dismissal or reactivation");g_dockDialog--;
    EnableWindow(g_hDash,FALSE);Check(!Dashboard_HandleOutsideClick(Outside()),"disabled Dashboard parent leaves its picker alone");EnableWindow(g_hDash,TRUE);
    Dashboard_Show();Pump();Check(SearchReady(),"search focus recovers after a guarded dialog returns");
    SendMessageW(g_hDash,WM_ACTIVATE,WA_INACTIVE,(LPARAM)foreignWindow);
    Check(!Dashboard_IsVisible(),"activation moving to an unrelated window closes Dashboard");
    SetForegroundWindow(foreignWindow);SendMessageW(g_hDash,WM_SC_REFOCUS,0,0);
    Check(GetForegroundWindow()==foreignWindow && !Dashboard_IsVisible(),"stale refocus messages do not steal input from another application");
    denyFocus=1;int beforeErrors=errors,beforeAttach=attachCalls,beforeDetach=detachCalls;Dashboard_Show();
    Check(g_dashOpening && attachCalls>beforeAttach && detachCalls==beforeDetach,
        "failed input-queue attachment starts bounded retries without an invalid detach");
    for(int i=0;i<8;i++)SendMessageW(g_hDash,WM_TIMER,DASH_FOCUS_TIMER,0);
    Check(!Dashboard_IsVisible() && !g_dashOpening && errors==beforeErrors+1,
        "focus denial reports an error and closes the unusable launcher after bounded retries");
    denyFocus=0;Dashboard_Show();PumpFor(80);Check(SearchReady(),"launcher remains usable after a focus denial clears");
    Dashboard_Shutdown();DestroyWindow(g_hWnd);if(thread){PostMessageW(foreignWindow,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);}CloseHandle(foreignReady);
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
