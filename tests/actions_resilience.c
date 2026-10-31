/* Real actions.c; all OS mutation boundaries are mocked. No UI or real input. */
#include "../src/main.h"
static int failures,checks,sends,failSendAt,usageCount,lastUsage,errorCount,shellCount;
static int timerFailure,confirmation=IDYES,foregroundChanges,enumerated,activated,shellSuccess=1;
static int pendingActivation,activationDenied,activationTimeout,activationLost,minimized,restoreRequested,flashes;
static int focusOnTab;
static BYTE keyState[256];
static ULONGLONG clockMs=1000;
static LPTHREAD_START_ROUTINE worker;
static void *workerArg;
static LPARAM resultMessage;
static wchar_t shellFile[8192],shellParams[8192],shellDirectory[8192],shellVerb[32];
static INPUT inputs[4096];
static void Check(int pass,const char *name)
{checks++;printf("%s: %s\n",pass?"PASS":"FAIL",name);if(!pass)failures++;}
static UINT ProbeInput(UINT count,LPINPUT value,int size)
{(void)size;for(UINT i=0;i<count;i++){sends++;if(sends==failSendAt){SetLastError(ERROR_ACCESS_DENIED);return i;}if(sends<4096)inputs[sends-1]=value[i];if(focusOnTab && value[i].ki.wVk==VK_TAB && !(value[i].ki.dwFlags&KEYEVENTF_KEYUP))foregroundChanges=1;}return count;}
static SHORT ProbeAsync(int vk){return keyState[vk]?(SHORT)0x8000:0;}
static UINT_PTR ProbeTimer(HWND w,UINT_PTR id,UINT interval,TIMERPROC cb)
{(void)w;(void)interval;(void)cb;return timerFailure?0:id;}
static BOOL ProbeKill(HWND w,UINT_PTR id){(void)w;(void)id;return TRUE;}
static ULONGLONG ProbeClock(void){return clockMs;}
static HWND ProbeForeground(void){return(HWND)(INT_PTR)(foregroundChanges?2:activated?activated:1);}
static HANDLE ProbeThread(LPSECURITY_ATTRIBUTES a,SIZE_T s,LPTHREAD_START_ROUTINE start,LPVOID p,DWORD f,LPDWORD id)
{(void)a;(void)s;(void)f;(void)id;worker=start;workerArg=p;return(HANDLE)(INT_PTR)11;}
static BOOL ProbeClose(HANDLE h){(void)h;return TRUE;}
static BOOL ProbePost(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{(void)w;(void)wp;if(msg==WM_SC_ACTIONRESULT)resultMessage=lp;return TRUE;}
static int ProbeBox(HWND w,LPCWSTR t,LPCWSTR title,UINT f)
{(void)w;(void)t;(void)title;(void)f;return confirmation;}
static HRESULT ProbeCom(void *r,DWORD f){(void)r;(void)f;return S_OK;}
static void ProbeUncom(void){}
static BOOL ProbeShell(SHELLEXECUTEINFOW *info)
{
 shellCount++;lstrcpynW(shellFile,info->lpFile,8192);lstrcpynW(shellParams,info->lpParameters?info->lpParameters:L"",8192);
 lstrcpynW(shellDirectory,info->lpDirectory?info->lpDirectory:L"",8192);lstrcpynW(shellVerb,info->lpVerb?info->lpVerb:L"",32);
 if(!shellSuccess)SetLastError(ERROR_FILE_NOT_FOUND);
 return shellSuccess;
}
static DWORD ProbeAttributes(LPCWSTR p)
{if(!wcscmp(p,L"D:\\Work"))return FILE_ATTRIBUTE_DIRECTORY;if(wcsstr(p,L".exe"))return FILE_ATTRIBUTE_NORMAL;return INVALID_FILE_ATTRIBUTES;}
static DWORD ProbeSearch(LPCWSTR d,LPCWSTR p,LPCWSTR ext,DWORD n,LPWSTR out,LPWSTR *part)
{(void)d;(void)ext;(void)part;int len=swprintf(out,n,L"D:\\Apps\\%ls",p);return len>0?(DWORD)len:0;}
static DWORD ProbeFull(LPCWSTR p,DWORD n,LPWSTR out,LPWSTR *part)
{(void)part;lstrcpynW(out,p,(int)n);return(DWORD)wcslen(out);}
static BOOL ProbeEnum(WNDENUMPROC callback,LPARAM p)
{enumerated++;if(callback((HWND)(INT_PTR)100,p))callback((HWND)(INT_PTR)200,p);return TRUE;}
static BOOL ProbeVisible(HWND w){(void)w;return TRUE;}
static HWND ProbeWindow(HWND w,UINT op){(void)w;(void)op;return NULL;}
static DWORD ProbePid(HWND w,LPDWORD pid){if(pid)*pid=(DWORD)(INT_PTR)w;return 1;}
static HANDLE ProbeOpen(DWORD a,BOOL i,DWORD pid){(void)a;(void)i;return(HANDLE)(INT_PTR)pid;}
static BOOL ProbeImage(HANDLE h,DWORD f,LPWSTR out,PDWORD n)
{(void)f;lstrcpynW(out,(INT_PTR)h==100?L"D:\\Other\\app.exe":L"D:\\Apps\\app.exe",(int)*n);*n=(DWORD)wcslen(out);return TRUE;}
static BOOL ProbeIconic(HWND w){(void)w;return minimized;}
/* Cross-thread activation succeeds before the receiving queue processes it. */
static BOOL ProbeSetForeground(HWND w)
{if(activationDenied)return FALSE;pendingActivation=(int)(INT_PTR)w;return TRUE;}
BOOL ProbeRestore(HWND w,int show)
{(void)w;if(show!=SW_RESTORE)failures++;restoreRequested=1;minimized=0;return TRUE;}
LRESULT ProbeTimeout(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT flags,UINT timeout,PDWORD_PTR result)
{
 (void)w;(void)wp;(void)lp;(void)flags;
 if(message==WM_SC_FOREGROUND || message==WM_SC_FOREGROUNDGRANT){
  if(result)*result=(DWORD_PTR)Action_HandleForeground(message,wp,lp);
  return 1;
 }
 if(message!=WM_NULL || !timeout || timeout>1000)failures++;
 if(activationTimeout){SetLastError(ERROR_TIMEOUT);return 0;}
 if(restoreRequested)minimized=0;
 if(!activationLost)activated=pendingActivation;
 pendingActivation=0;if(result)*result=0;return 1;
}
static BOOL ProbeFlash(PFLASHWINFO info){(void)info;flashes++;return TRUE;}
static BOOL ProbeRegister(HWND w,int id,UINT mods,UINT key)
{(void)w;(void)id;(void)mods;(void)key;SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);return FALSE;}
static BOOL ProbeUnregister(HWND w,int id){(void)w;(void)id;failures++;return TRUE;}
#define RegisterHotKey ProbeRegister
#define UnregisterHotKey ProbeUnregister
#define SendInput ProbeInput
#define GetAsyncKeyState ProbeAsync
#define SetTimer ProbeTimer
#define KillTimer ProbeKill
#define GetTickCount64 ProbeClock
#define GetForegroundWindow ProbeForeground
#define CreateThread ProbeThread
#define CloseHandle ProbeClose
#define PostMessageW ProbePost
#define MessageBoxW ProbeBox
#define CoInitializeEx ProbeCom
#define CoUninitialize ProbeUncom
#define ShellExecuteExW ProbeShell
#define GetFileAttributesW ProbeAttributes
#define SearchPathW ProbeSearch
#define GetFullPathNameW ProbeFull
#define EnumWindows ProbeEnum
#define IsWindowVisible ProbeVisible
#define GetWindow ProbeWindow
#define GetWindowThreadProcessId ProbePid
#define OpenProcess ProbeOpen
#define QueryFullProcessImageNameW ProbeImage
#define IsIconic ProbeIconic
#define SetForegroundWindow ProbeSetForeground
#define ShowWindowAsync ProbeRestore
#define SendMessageTimeoutW ProbeTimeout
#define FlashWindowEx ProbeFlash
#include "../src/actions.c"
AppConfig g_cfg;
int g_enabled=1;
HWND g_hWnd=(HWND)(INT_PTR)7;
void UI_Show(HWND w){(void)w;}
void Dashboard_Show(void){}
int Dashboard_IsVisible(void){return TRUE;}
void Dashboard_RecordUsage(int id){usageCount++;lastUsage=id;}
void SC_ReportError(const wchar_t *op,DWORD e){(void)op;(void)e;errorCount++;}
void SC_LogDiagnostic(const wchar_t *message){(void)message;}
void SC_SetEnabled(int enabled){g_enabled=!!enabled;Action_Cancel();}
static void Reset(void)
{
 Action_Cancel();memset(&g_cfg,0,sizeof(g_cfg));memset(keyState,0,sizeof(keyState));memset(inputs,0,sizeof(inputs));
 sends=failSendAt=usageCount=lastUsage=errorCount=shellCount=timerFailure=foregroundChanges=enumerated=activated=0;
 pendingActivation=activationDenied=activationTimeout=activationLost=minimized=restoreRequested=flashes=0;
 focusOnTab=0;g_enabled=1;confirmation=IDYES;shellSuccess=1;clockMs=1000;worker=NULL;workerArg=NULL;resultMessage=0;g_actionStopped=0;
}
static Command *CommandAt(int i,int id,CmdType type,const char *value)
{
 if(i>=g_cfg.commandCount)g_cfg.commandCount=i+1;
 Command *c=&g_cfg.commands[i];c->id=id;c->type=type;
 SC_CopyUtf8(c->name,sizeof(c->name),"Test");SC_CopyUtf8(type==CMD_KEYS?c->keySeq:c->runPath,type==CMD_KEYS?sizeof(c->keySeq):sizeof(c->runPath),value);return c;
}
static void Tick(void){Action_Tick();clockMs+=15;}
static void CompleteWorker(void)
{LPTHREAD_START_ROUTINE start=worker;void *p=workerArg;worker=NULL;workerArg=NULL;if(start)start(p);if(resultMessage){LPARAM r=resultMessage;resultMessage=0;Action_HandleResult(r);}}
static int Validate(const char *s){wchar_t e[256];return Action_ValidateKeySeq(s,e,256);}
static void TestFocusSwitchingMacros(void)
{
 const char *seq[]={
  "{vkA2 down}{Sleep 125}{vkA4 down}{Sleep 31}{vk09 down}{Sleep 172}{vkA4 up}{vk09 up}{Sleep 31}{vkA2 up}",
  "{vkA2 down}{Sleep 78}{vkA4 down}{Sleep 62}{vk09 down}{Sleep 141}{vkA4 up}{Sleep 16}{vk09 up}{Sleep 78}{vkA2 up}",
  "{Ctrl down}{Alt down}{Tab}{Alt up}{Ctrl up}"
 };
 for(int sample=0;sample<3;sample++){
  Reset();focusOnTab=1;CommandAt(0,99+sample,CMD_KEYS,seq[sample]);Action_ExecuteCommand(99+sample);
  for(int tick=0;tick<64 && (g_jobCount || g_actionBusy);tick++){if(g_macroDue)clockMs=g_macroDue;Tick();}
  Check(foregroundChanges && !errorCount && usageCount==1 && sends==6 && !g_actionBusy,
        "recorded test/test1 and legacy Ctrl+Alt+Tab finish their releases after focus changes");
  int held=0;for(int vk=0;vk<256;vk++)held+=g_macroOwned[vk];
  Check(!held && (inputs[5].ki.dwFlags&KEYEVENTF_KEYUP),"focus-switching shortcut leaves no macro keys held");
 }
 Reset();focusOnTab=1;CommandAt(0,102,CMD_KEYS,"{Alt down}{Tab}{X}{Alt up}");Action_ExecuteCommand(102);Tick();Tick();
 int sentX=0;for(int i=0;i<sends;i++)if(inputs[i].ki.wVk=='X')sentX=1;
 Check(errorCount==1 && !sentX && !usageCount && !g_actionBusy,
       "focus change caused during a single timer tick cannot send a later key to another window");
}
int main(void)
{
 Reset();
 Check(Validate("{Ctrl down}{C}{Ctrl up} \r\n\t"),"balanced macro accepts trailing whitespace");
 Check(!Validate("{NoSuchKey}")&&!Validate("{C")&&!Validate("C")&&!Validate("{Ctrl down}"),"invalid tokens and unmatched presses are rejected");
 Check(!Validate("{C up}")&&!Validate("{Sleep 5001}")&&!Validate("{Sleep -1}")&&!Validate("{Sleep 3x}"),"invalid releases and delays are rejected");
 char unterminated[SC_KEYS_CAP];memset(unterminated,' ',sizeof(unterminated));Check(!Validate(unterminated),"unterminated sequence parsing is bounded");
 char boundary[64]="{C} ";memcpy(boundary+5,"{D}",4);CommandAt(0,1,CMD_KEYS,boundary);Action_ExecuteCommand(1);Tick();Tick();
 Check(sends==2&&inputs[0].ki.wVk=='C'&&inputs[1].ki.wVk=='C',"trailing whitespace never executes bytes after NUL");
 Check(!inputs[0].ki.wScan&&!inputs[0].ki.time&&!inputs[0].ki.dwExtraInfo,"INPUT fields are initialized");
 Check(usageCount==1&&lastUsage==1,"macro usage follows successful completion");
 Reset();CommandAt(0,2,CMD_KEYS,"{C}{Sleep 5000}{D}");Action_ExecuteCommand(2);Tick();Tick();
 Check(sends==2&&g_actionBusy==1&&g_macroDue>clockMs&&!usageCount,"delay never sleeps the main thread");
 clockMs=g_macroDue-1;Tick();Check(sends==2,"timer respects macro delay");clockMs=g_macroDue;Tick();Check(sends==4&&usageCount==1,"timer resumes keys in order");
 Reset();CommandAt(0,3,CMD_KEYS,"{Ctrl down}{Sleep 20}{C}{Ctrl up}");Action_ExecuteCommand(3);Tick();Tick();Action_Cancel();
 Check(sends==2&&inputs[1].ki.wVk==VK_CONTROL&&(inputs[1].ki.dwFlags&KEYEVENTF_KEYUP)&&!usageCount,"cancel releases owned keys");
 Reset();keyState[VK_CONTROL]=1;CommandAt(0,4,CMD_KEYS,"{Ctrl down}{Sleep 20}{C}{Ctrl up}");Action_ExecuteCommand(4);Tick();Tick();Action_Cancel();
 Check(!sends,"cancel preserves physically held Ctrl");
 Reset();failSendAt=2;CommandAt(0,5,CMD_KEYS,"{Ctrl down}{C}{Ctrl up}");Action_ExecuteCommand(5);Tick();Tick();
 Check(errorCount==1&&!usageCount&&sends==3&&inputs[2].ki.wVk==VK_CONTROL&&(inputs[2].ki.dwFlags&KEYEVENTF_KEYUP),"input failure releases owned keys without usage");
 Reset();CommandAt(0,6,CMD_KEYS,"{Ctrl down}{Sleep 20}{C}{Ctrl up}");Action_ExecuteCommand(6);Tick();Tick();foregroundChanges=1;clockMs=g_macroDue;Tick();
 Check(errorCount==1&&sends==2&&!usageCount,"focus change during delay cancels macro");
 Reset();CommandAt(0,10,CMD_RUN,"D:\\Documents\\new.txt");Action_ExecuteCommand(10);Tick();
 Check(worker&&!shellCount&&!usageCount,"launch uses a deferred worker");
 CompleteWorker();Check(shellCount==1&&!enumerated&&usageCount==1,"documents always open through Shell");
 Reset();Command *c=CommandAt(0,11,CMD_RUN,"app.exe");c->activateExisting=1;Action_ExecuteCommand(11);Tick();CompleteWorker();
 Check(activated==200&&!shellCount&&usageCount==1,"exe activation compares full process paths");
 Check(!pendingActivation,"existing-window activation completes without another input event");
 Reset();activationTimeout=1;c=CommandAt(0,11,CMD_RUN,"app.exe");c->activateExisting=1;Action_ExecuteCommand(11);Tick();CompleteWorker();
 Check(!activated&&shellCount==1&&usageCount==1,"unresponsive activation falls back to Shell instead of reporting foreground success");
 Reset();activationLost=1;c=CommandAt(0,11,CMD_RUN,"app.exe");c->activateExisting=1;Action_ExecuteCommand(11);Tick();CompleteWorker();
 Check(!activated&&shellCount==1&&usageCount==1,"foreground lost during activation uses normal launch fallback");
 Reset();activationDenied=1;c=CommandAt(0,11,CMD_RUN,"app.exe");c->activateExisting=1;Action_ExecuteCommand(11);Tick();CompleteWorker();
 Check(!activated&&shellCount==1&&flashes==1&&usageCount==1,"denied activation retains tray feedback and normal launch fallback");
 Reset();minimized=1;c=CommandAt(0,11,CMD_RUN,"app.exe");c->activateExisting=1;Action_ExecuteCommand(11);Tick();CompleteWorker();
 Check(restoreRequested&&!minimized&&activated==200&&!shellCount&&usageCount==1,"minimized existing window is restored before activation completes");
 Reset();CommandAt(0,12,CMD_RUN,"app.exe")->activateExisting=0;Action_ExecuteCommand(12);Tick();CompleteWorker();
 Check(shellCount==1&&!enumerated,"new-instance option skips activation");
 Reset();CommandAt(0,13,CMD_RUN,"https://example.com/tool.exe")->activateExisting=1;Action_ExecuteCommand(13);Tick();CompleteWorker();
 Check(shellCount==1&&!enumerated&&!wcscmp(shellFile,L"https://example.com/tool.exe"),"URL exe suffix never triggers process activation");
 Reset();c=CommandAt(0,14,CMD_RUN,"app.exe");c->activateExisting=1;c->runAsAdmin=1;strcpy(c->arguments,"--profile \"测试\"");strcpy(c->workingDir,"D:\\Work");
 Action_ExecuteCommand(14);Tick();CompleteWorker();
 Check(shellCount==1&&!enumerated&&!wcscmp(shellVerb,L"runas")&&!wcscmp(shellDirectory,L"D:\\Work")&&!wcscmp(shellParams,L"--profile \"测试\""),"arguments, directory and admin verb are preserved");
 Reset();char longScript[SC_PATH_CAP];memset(longScript,'a',sizeof(longScript));memcpy(longScript,"D:\\",3);memcpy(longScript+1000,".ps1",5);
 CommandAt(0,15,CMD_SCRIPT,longScript);Action_ExecuteCommand(15);Tick();CompleteWorker();
 Check(shellCount==1&&wcslen(shellParams)>1000&&wcsstr(shellParams,L"-WindowStyle Hidden")&&wcsstr(shellParams,L".ps1\""),"PowerShell parameters safely support long paths");
 Reset();CommandAt(0,16,CMD_SCRIPT,"D:\\Test_Show.ps1");Action_ExecuteCommand(16);Tick();CompleteWorker();
 Check(shellCount==1&&!wcsstr(shellParams,L"-WindowStyle"),"Show suffix preserves script visibility");
 Reset();shellSuccess=0;CommandAt(0,17,CMD_RUN,"missing.exe");Action_ExecuteCommand(17);Tick();CompleteWorker();
 Check(errorCount==1&&!usageCount,"failed launch reports without usage");
 Reset();CommandAt(0,18,CMD_RUN,"app.exe");Action_ExecuteCommand(18);Tick();Action_Cancel();CompleteWorker();
 Check(!usageCount&&!errorCount,"stale worker completion is discarded");
 Reset();CommandAt(0,19,CMD_RUN,"app.exe")->confirmBeforeRun=1;confirmation=IDNO;Action_ExecuteCommand(19);Tick();
 Check(!worker&&!shellCount&&!usageCount,"declined confirmation never executes");
 Reset();CommandAt(0,20,CMD_RUN,"one.exe");CommandAt(1,21,CMD_RUN,"two.exe");Action_ExecuteCommand(20);Action_ExecuteCommand(21);Tick();CompleteWorker();Tick();CompleteWorker();
 Check(shellCount==2&&usageCount==2&&lastUsage==21,"launch queue preserves order");
 Reset();timerFailure=1;CommandAt(0,22,CMD_RUN,"app.exe");Action_ExecuteCommand(22);Check(errorCount==1&&!g_jobCount&&!usageCount,"timer failure rolls back queue entry");
 Reset();CommandAt(0,23,CMD_RUN,"D:\\Work");Action_ExecuteCommand(23);Tick();CompleteWorker();Check(shellCount==1&&!enumerated,"directories always open through Shell");
 TestFocusSwitchingMacros();
 Action_Shutdown();printf("Checks: %d; Failures: %d\n",checks,failures);return failures?1:0;
}
