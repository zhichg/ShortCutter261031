#define main SettingsRegressionMain
#include "settings_resilience.c"
#undef main
static WNDPROC oldParent,oldTab;static int erases,tabPaints;
static LRESULT CALLBACK ParentPaint(HWND w,UINT m,WPARAM a,LPARAM b){if(m==WM_ERASEBKGND)erases++;return CallWindowProcW(oldParent,w,m,a,b);}
static LRESULT CALLBACK TabPaint(HWND w,UINT m,WPARAM a,LPARAM b){if(m==WM_PAINT)tabPaints++;return CallWindowProcW(oldTab,w,m,a,b);}
static void SelectPage(int page){TabCtrl_SetCurSel(g_hTab,page);NMHDR changed={g_hTab,CID_TAB,TCN_SELCHANGE};SendMessageW(g_hDlg,WM_NOTIFY,CID_TAB,(LPARAM)&changed);}
int main(void){
 g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 GetCurrentDirectoryW(SC_PATH_CAP,g_exeDir);wchar_t temp[SC_PATH_CAP];GetTempPathW(SC_PATH_CAP,temp);GetTempFileNameW(temp,L"sct",0,ini);Config_SetDefaults();Add(901,"Visible command");Open();Pump();
 oldParent=(WNDPROC)SetWindowLongPtrW(g_hDlg,GWLP_WNDPROC,(LONG_PTR)ParentPaint);oldTab=(WNDPROC)SetWindowLongPtrW(g_hTab,GWLP_WNDPROC,(LONG_PTR)TabPaint);
 int visibleOK=1,cleared=1,painted=1;DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(int cycle=0;cycle<12;cycle++)for(int page=0;page<5;page++){
   SelectPage(TAB_COMMANDS);
   RECT r;GetWindowRect(g_hCmdList,&r);MapWindowPoints(NULL,g_hDlg,(POINT*)&r,2);
   /* Mark the old page's uncovered lower-right corner, then switch through
      the real notification path. The new page must erase it immediately. */
   RECT mark={r.right-7,r.bottom-7,r.right-2,r.bottom-2};HDC dc=GetDC(g_hDlg);HBRUSH brush=CreateSolidBrush(RGB(255,0,255));FillRect(dc,&mark,brush);DeleteObject(brush);ReleaseDC(g_hDlg,dc);
   int e=erases,t=tabPaints;SelectPage(page);
   if(erases<=e || tabPaints<=t)painted=0;
   if(IsWindowVisible(g_hCmdList)!=(page==TAB_COMMANDS) || IsWindowVisible(g_hGesList)!=(page==TAB_GESTURES) || IsWindowVisible(g_hHkList)!=(page==TAB_HOTKEYS) || IsWindowVisible(GetDlgItem(g_hDlg,CID_GROUP_STARTUP))!=(page==TAB_GENERAL) || IsWindowVisible(GetDlgItem(g_hDlg,CID_GROUP_DASH_APPEAR))!=(page==TAB_DASHBOARD))visibleOK=0;
   if(page<TAB_COMMANDS){dc=GetDC(g_hDlg);COLORREF pixel=GetPixel(dc,mark.left+1,mark.top+1);ReleaseDC(g_hDlg,dc);if(pixel!=GetSysColor(COLOR_BTNFACE))cleared=0;}
 }
 Check(visibleOK,"60 tab switches keep only the selected page visible");Check(painted,"each switch erases the old background and repaints the native tab immediately");Check(cleared,"switching from Commands removes stale list pixels from General and Dashboard");
 Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+1,"tab switches do not accumulate drawing resources");
 Check(!saved && !startupCalls,"tab switching does not save settings or change startup");Close();DeleteFileW(ini);printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
