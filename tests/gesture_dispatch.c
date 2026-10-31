/* Real gesture matching, native window messages, timer and launch worker.
   No hooks, input injection, external apps or user configuration writes. */
#include "../src/main.h"
#include <stdlib.h>

AppConfig g_cfg;
int g_enabled=1,g_dpiScale=96;
HINSTANCE g_hInst;HWND g_hWnd;HMENU g_hTrayMenu;wchar_t g_exeDir[SC_PATH_CAP];
static LONG launches;
static int failures,usage,completed;
static ULONGLONG releasedAt,launchedAt,worstLatency;
static UINT trigger;
enum { DISPATCH_CYCLES=12 };
static BOOL ProbeShell(SHELLEXECUTEINFOW *info)
{
    if(wcscmp(info->lpFile,L"fixture.exe"))failures++;
    launchedAt=GetTickCount64();InterlockedIncrement(&launches);return TRUE;
}
void UI_Show(HWND parent){(void)parent;}
int UI_IsCapturingInput(void){return 0;}
void Dashboard_Show(void){}
int Dashboard_IsVisible(void){return 0;}
int Dashboard_HandleOutsideClick(POINT point){(void)point;return 0;}
void Dashboard_RecordUsage(int id){if(id==100)usage++;}
void SC_SetEnabled(int enabled){g_enabled=!!enabled;Action_Cancel();Gesture_Cancel();}
void SC_ReportError(const wchar_t *operation,DWORD error)
{(void)operation;printf("FAIL: reported error=%lu\n",(unsigned long)error);failures++;}
void SC_LogDiagnostic(const wchar_t *message){(void)message;}
static SHORT ProbeKeys(int vk){(void)vk;return 0;}
#define GetAsyncKeyState ProbeKeys
#include "../src/gesture.c"
#undef GetAsyncKeyState
#define ShellExecuteExW ProbeShell
#include "../src/actions.c"
#undef ShellExecuteExW

static const POINT cPath[]={{300,100},{270,90},{240,100},{210,130},{200,160},
    {200,190},{210,220},{240,250},{270,260},{300,260},{330,250}};
static void Mouse(UINT message,POINT point)
{
    MSLLHOOKSTRUCT event={0};event.pt=point;
    if(MouseProc(HC_ACTION,message,(LPARAM)&event)!=1 && message!=WM_MOUSEMOVE)failures++;
}
static LRESULT CALLBACK Sink(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_APP+900){
        LONG serialBefore = InterlockedCompareExchange(&g_actionInputSerial,0,0);
        Mouse(trigger==WM_MBUTTONDOWN?WM_MBUTTONDOWN:WM_RBUTTONDOWN,cPath[0]);
        for(size_t i=1;i<sizeof(cPath)/sizeof(cPath[0]);i++)Mouse(WM_MOUSEMOVE,cPath[i]);
        releasedAt=GetTickCount64();
        Mouse(trigger==WM_MBUTTONDOWN?WM_MBUTTONUP:WM_RBUTTONUP,cPath[10]);
        if(InterlockedCompareExchange(&g_actionInputSerial,0,0)!=serialBefore+1) {
            printf("FAIL: gesture movement/release changed the deliberate-input serial\n");failures++;
        }
        return 0;
    }
    if(message==WM_SC_MATCH){
        GestureEvent *event=(GestureEvent*)lp;
        if(Gesture_Match(event->chain,event->chainLen,event->modifier,event->zone)!=0){
            printf("FAIL: MATCH chain=%s sample=%s modifier=%d zone=%d\n",event->chain,
                g_cfg.gestures[0].chains[0],event->modifier,event->zone);failures++;
        }
        Gesture_HandleMatch(wp,lp);return 0;
    }
    if(message==WM_SC_ACTIONRESULT){
        Action_HandleResult(lp);completed++;
        ULONGLONG latency=launchedAt>=releasedAt?launchedAt-releasedAt:1000;
        if(latency>worstLatency)worstLatency=latency;
        if(launches!=completed || usage!=completed || latency>=1000)failures++;
        if(completed<DISPATCH_CYCLES && !failures)PostMessageW(window,WM_APP+900,0,0);
        else PostQuitMessage(failures?1:0);
        return 0;
    }
    if(message==WM_TIMER){
        if(wp==SC_ACTION_TIMER)Action_Tick();
        else if(wp==900){printf("TIMEOUT jobs=%d busy=%d launches=%ld points=%d state=%d\n",g_jobCount,g_actionBusy,launches,g_pointCount,g_state);failures++;PostQuitMessage(1);}
        return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
int main(int argc,char **argv)
{
    trigger=argc>1 && !strcmp(argv[1],"right")?WM_RBUTTONDOWN:WM_MBUTTONDOWN;
    g_hInst=GetModuleHandleW(NULL);g_cfg.mouseButton=trigger==WM_MBUTTONDOWN?2:1;
    g_cfg.minGesture=20;g_cfg.matchThreshold=70;
    g_cfg.trailEnabled=1;g_cfg.trailInnerWidth=3;g_cfg.trailInnerColor=RGB(20,100,200);
    g_cfg.commandCount=g_cfg.gestureCount=1;
    Command *command=&g_cfg.commands[0];command->id=100;command->type=CMD_RUN;
    SC_CopyUtf8(command->runPath,sizeof(command->runPath),"fixture.exe");
    GestureDef *gesture=&g_cfg.gestures[0];gesture->enabled=1;gesture->commandId=100;gesture->chainCount=1;gesture->zone=ZONE_ALL;
    POINT sampled[200];int count=Gesture_Downsample(cPath,11,sampled,200,8);
    Gesture_EncodeChain(sampled,count,gesture->chains[0],63);
    WNDCLASSW cls={0};cls.lpfnWndProc=Sink;cls.hInstance=g_hInst;cls.lpszClassName=L"SC_GestureDispatchFixture";
    RegisterClassW(&cls);g_hWnd=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,0,0,NULL,NULL,g_hInst,NULL);
    SetTimer(g_hWnd,900,2000,NULL);PostMessageW(g_hWnd,WM_APP+900,0,0);
    MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    KillTimer(g_hWnd,900);Action_Shutdown();Gesture_Stop();Gesture_DiscardPendingEvents();DestroyWindow(g_hWnd);
    if(completed!=DISPATCH_CYCLES || launches!=DISPATCH_CYCLES || usage!=DISPATCH_CYCLES)failures++;
    printf("%s: %s-button C gesture dispatched %d times without another click (Shell mocked); max latency=%llu ms; usage=%d; failures=%d\n",
        failures?"FAIL":"PASS",trigger==WM_MBUTTONDOWN?"middle":"right",
        completed,(unsigned long long)worstLatency,usage,failures);
    return failures?1:0;
}
