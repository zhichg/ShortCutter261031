/* Actual Dashboard UI with private key and clipboard state. No real input. */
#include "../src/main.h"
AppConfig g_cfg;int g_enabled=1,g_dpiScale=96;HINSTANCE g_hInst;HWND g_hWnd;HMENU g_hTrayMenu;wchar_t g_exeDir[SC_PATH_CAP];
static int checks,failures,executed,visibleAtExecute;static BYTE keys[256];static HGLOBAL clipboard;
BOOL Config_Save(void){return TRUE;}DWORD Config_GetLastError(void){return 0;}
void SC_ReportError(const wchar_t *op,DWORD err){(void)op;(void)err;}
void Scripts_ScanAndSync(void){}
void Action_ExecuteCommand(int id){executed=id;visibleAtExecute=Dashboard_IsVisible();}
static SHORT TestKey(int vk){return keys[vk]?(SHORT)0x8000:0;}
static BOOL TestOpenClip(HWND window){(void)window;return TRUE;}
static BOOL TestCloseClip(void){return TRUE;}
static BOOL TestEmptyClip(void){if(clipboard)GlobalFree(clipboard);clipboard=NULL;return TRUE;}
static HANDLE TestGetClip(UINT kind){(void)kind;return clipboard;}
static HANDLE TestSetClip(UINT kind,HANDLE data){(void)kind;clipboard=data;return data;}
#define GetKeyState TestKey
#define GetAsyncKeyState TestKey
#define OpenClipboard TestOpenClip
#define CloseClipboard TestCloseClip
#define EmptyClipboard TestEmptyClip
#define GetClipboardData TestGetClip
#define SetClipboardData TestSetClip
#include "../src/dashboard.c"
#undef GetKeyState
#undef GetAsyncKeyState
static void Check(int result,const char *text){checks++;printf("%s: %s\n",result?"PASS":"FAIL",text);if(!result)failures++;}
static void Clip(const wchar_t *text){TestEmptyClip();clipboard=GlobalAlloc(GMEM_MOVEABLE,(wcslen(text)+1)*sizeof(wchar_t));wchar_t *data=GlobalLock(clipboard);lstrcpyW(data,text);GlobalUnlock(clipboard);}
static void Ctrl(UINT vk){keys[VK_CONTROL]=1;SendMessageW(g_hSearchEdit,WM_KEYDOWN,vk,0);keys[VK_CONTROL]=0;}
static LRESULT CALLBACK Sink(HWND w,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_SC_DASHICONS){Dashboard_AcceptIcons();return 0;}return DefWindowProcW(w,msg,wp,lp);}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW wc={0};wc.lpfnWndProc=Sink;wc.hInstance=g_hInst;wc.lpszClassName=L"SC_InputSafety";RegisterClassW(&wc);g_hWnd=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,NULL,NULL,g_hInst,NULL);
    g_cfg.dashboardOpacity=100;g_cfg.commandCount=1;g_cfg.commands[0].id=1;g_cfg.commands[0].type=CMD_KEYS;SC_CopyUtf8(g_cfg.commands[0].name,SC_NAME_CAP,"Notepad++");g_cfg.dockCount=g_cfg.dockPageCount=1;g_cfg.dockItems[0].commandId=1;Dashboard_Show();
    Check(ImmGetContext(g_hSearchEdit)==NULL,"search keeps IME disabled");
    Check(Dashboard_HandlePhysicalKey(WM_KEYDOWN,'A',30,0)==1 && !wcscmp(g_sbText,L"a"),"physical A key directly inserts English without layout translation");
    Check(Dashboard_HandlePhysicalKey(WM_KEYUP,'A',30,0)==1,"consumed physical key-up is paired exactly once");
    keys[VK_SHIFT]=1;Dashboard_HandlePhysicalKey(WM_KEYDOWN,'B',48,0);keys[VK_SHIFT]=0;Check(!wcscmp(g_sbText,L"aB"),"physical Shift modifies English case");Dashboard_HandlePhysicalKey(WM_KEYUP,'B',48,0);
    SendMessageW(g_hSearchEdit,WM_CHAR,L'中',0);SendMessageW(g_hSearchEdit,WM_IME_CHAR,L'文',0);SendMessageW(g_hSearchEdit,WM_CHAR,L'!',0);
    Check(!wcscmp(g_sbText,L"aB"),"translated non-English and unsupported punctuation cannot enter search");
    Ctrl('A');SendMessageW(g_hSearchEdit,WM_CHAR,'C',0);Check(!wcscmp(g_sbText,L"C"),"typing replaces the selected search text");Ctrl('Z');Check(!wcscmp(g_sbText,L"aB"),"undo restores search content and selection");
    Ctrl('A');Ctrl('C');const wchar_t *copied=GlobalLock(clipboard);Check(copied && !wcscmp(copied,L"aB"),"copy preserves selected text through a private clipboard");GlobalUnlock(clipboard);Ctrl('X');Check(g_sbLen==0,"cut removes the selected text");Ctrl('V');Check(!wcscmp(g_sbText,L"aB"),"paste restores English letters");
    Clip(L"中A1b\r\nC_");Ctrl('A');Ctrl('V');Check(!wcscmp(g_sbText,L"A1bC"),"paste preserves letters and digits while filtering other scripts and line breaks");
    SearchBox_SetText(L"");
    for(UINT vk='0';vk<='9';vk++){Check(Dashboard_HandlePhysicalKey(WM_KEYDOWN,vk,0,0)==1,"main keyboard digit is consumed");Check(Dashboard_HandlePhysicalKey(WM_KEYUP,vk,0,0)==1,"digit key-up remains paired");}
    Check(!wcscmp(g_sbText,L"0123456789"),"main keyboard inserts every digit exactly once");
    SearchBox_SetText(L"");SetFocus(g_hDockPane);
    for(UINT vk=VK_NUMPAD0;vk<=VK_NUMPAD9;vk++){Dashboard_HandlePhysicalKey(WM_KEYDOWN,vk,0,0);Dashboard_HandlePhysicalKey(WM_KEYUP,vk,0,0);}
    Check(GetFocus()==g_hSearchEdit && !wcscmp(g_sbText,L"0123456789"),"numpad digits focus the search from the grid and insert once");
    Ctrl('A');SendMessageW(g_hSearchEdit,WM_CHAR,'1',0);SendMessageW(g_hSearchEdit,WM_CHAR,'2',0);
    Check(!wcscmp(g_sbText,L"12"),"digits replace a selection and append normally");Ctrl('Z');Check(!wcscmp(g_sbText,L"1"),"digit input supports undo");
    SearchBox_SetText(L"test1");SC_CopyUtf8(g_cfg.commands[0].name,SC_NAME_CAP,"test1");SearchBox_Update();
    Check(SendMessageW(g_hHistList,LB_GETCOUNT,0,0)==1 && SendMessageW(g_hHistList,LB_GETITEMDATA,0,0)==1,"mixed letter and digit query finds a command");SC_CopyUtf8(g_cfg.commands[0].name,SC_NAME_CAP,"Notepad++");
    SearchBox_SetText(L"Notepad");Check(SendMessageW(g_hHistList,LB_GETCOUNT,0,0)==1 && SendMessageW(g_hHistList,LB_GETITEMDATA,0,0)==1,"names containing plus remain searchable");
    Dashboard_HandlePhysicalKey(WM_KEYDOWN,VK_F6,0,0);Dashboard_HandlePhysicalKey(WM_KEYUP,VK_F6,0,0);Check(GetFocus()==g_hDockPane,"F6 moves keyboard focus to the shortcut grid");
    SendMessageW(g_hDockPane,WM_KEYDOWN,VK_RIGHT,0);Check(g_dockKeyCell==1,"arrow key moves the grid selection");SendMessageW(g_hDockPane,WM_KEYDOWN,VK_HOME,0);keys[VK_CONTROL]=keys[VK_LCONTROL]=1;SendMessageW(g_hDockPane,WM_KEYDOWN,VK_RETURN,0);
    Check(g_pendingCount==1 && !executed && Dashboard_IsVisible(),"Ctrl+Enter queues the focused command");keys[VK_CONTROL]=keys[VK_LCONTROL]=0;Dash_OpenPending();Check(executed==1 && !visibleAtExecute && !Dashboard_IsVisible(),"Ctrl release hides Dashboard before executing the queued command");
    Dashboard_Show();g_dockDialog++;Check(!Dashboard_HasFocus() && !Dashboard_HandlePhysicalKey(WM_KEYDOWN,'D',0,0),"file picker and menu guards leave keys with the modal UI");g_dockDialog--;
    RECT rect;GetWindowRect(g_hDash,&rect);SetWindowPos(g_hDash,NULL,rect.left,rect.top,DS(500),DS(460),SWP_NOZORDER|SWP_NOACTIVATE);Dash_Layout(g_hDash);RECT pane;GetClientRect(g_hDockPane,&pane);
    Check(g_dpStepY==DS(76) && g_dpItemH>=DS(68) && g_dockRowsVisible<g_dockTotalRows && pane.bottom>=g_dpItemH,"short window scrolls rows without shrinking icons into labels");
    SendMessageW(g_hDockPane,WM_VSCROLL,SB_LINEDOWN,0);Check(g_dockRowOffset==1,"short-window scrollbar changes visible grid rows");
    g_cfg.dashboardCompact=1;Dashboard_ApplyLayout();Check(g_dpStepY==DS(64),"compact preference applies a deliberate smaller cell layout");
    Dashboard_Shutdown();DestroyWindow(g_hWnd);TestEmptyClip();printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
