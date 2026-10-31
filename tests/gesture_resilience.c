/* Real gesture parser/routing with private event queue; no hooks or injected input. */
#include "../src/main.h"
AppConfig g_cfg;int g_enabled=1,g_dpiScale=96;HINSTANCE g_hInst;HWND g_hWnd;HMENU g_hTrayMenu;wchar_t g_exeDir[SC_PATH_CAP];
static WPARAM postedType[16];static LPARAM posted[16];static int postCount,rightWin,capture,checks,failures,executed[16],execCount,inputs;
static int outside,outsideCalls;
int Dashboard_IsVisible(void){return 0;}void Dashboard_Close(void){}
int Dashboard_HandleOutsideClick(POINT point){(void)point;outsideCalls++;return outside;}
int UI_IsCapturingInput(void){return capture;}
void SC_ReportError(const wchar_t *op,DWORD error){(void)op;(void)error;}
void Action_ExecuteCustom(const GestureDef *g){executed[execCount++]=g->commandId;}
void Action_NoteUserInput(void){}
static BOOL TestPost(HWND w,UINT message,WPARAM wp,LPARAM lp){(void)w;if(message==WM_SC_MATCH && postCount<16){postedType[postCount]=wp;posted[postCount++]=lp;return TRUE;}return FALSE;}
static SHORT TestKey(int vk){return rightWin && vk==VK_RWIN?(SHORT)0x8000:0;}
static int TestMetrics(int kind){return kind==SM_XVIRTUALSCREEN?-1920:kind==SM_YVIRTUALSCREEN?-1080:kind==SM_CXVIRTUALSCREEN?3840:kind==SM_CYVIRTUALSCREEN?2160:0;}
static UINT TestInput(UINT count,LPINPUT data,int size){(void)data;(void)size;inputs+=(int)count;return count;}
static LRESULT TestNext(HHOOK hook,int code,WPARAM wp,LPARAM lp){(void)hook;(void)code;(void)wp;(void)lp;return 42;}
#define PostMessageW TestPost
#define GetAsyncKeyState TestKey
#define GetSystemMetrics TestMetrics
#define SendInput TestInput
#define CallNextHookEx TestNext
#include "../src/gesture.c"
static void Check(int condition,const char *text){checks++;printf("%s: %s\n",condition?"PASS":"FAIL",text);if(!condition)failures++;}
static void Event(UINT message,int x,int y){MSLLHOOKSTRUCT data={0};data.pt.x=x;data.pt.y=y;MouseProc(HC_ACTION,message,(LPARAM)&data);}
static void Draw(int x,int y,int step){Event(WM_MBUTTONDOWN,x,y);for(int i=1;i<=6;i++)Event(WM_MOUSEMOVE,x+step*i,y);Event(WM_MBUTTONUP,x+step*6,y);}
int main(void)
{
    g_cfg.mouseButton=2;g_cfg.minGesture=20;g_cfg.matchThreshold=70;g_cfg.gestureCount=2;
    for(int i=0;i<2;i++){g_cfg.gestures[i].enabled=1;g_cfg.gestures[i].chainCount=1;g_cfg.gestures[i].commandId=100+i;g_cfg.gestures[i].chains[0][0]=i?'2':'3';}
    Draw(100,100,30);Draw(100,100,-30);
    GestureEvent *first=(GestureEvent*)posted[0],*second=(GestureEvent*)posted[1];
    Check(postCount==2 && first!=second && !strcmp(first->chain,"3") && !strcmp(second->chain,"2"),"queued gestures own separate immutable direction snapshots");
    Gesture_HandleMatch(postedType[0],posted[0]);Gesture_HandleMatch(postedType[1],posted[1]);posted[0]=posted[1]=0;
    Check(execCount==2 && executed[0]==100 && executed[1]==101,"queued gestures dispatch their own commands in order");
    rightWin=1;Draw(100,100,30);first=(GestureEvent*)posted[2];Check(first->modifier==SC_MOD_WIN,"right Win contributes to gesture modifiers");rightWin=0;
    Draw(1800,-1000,-30);first=(GestureEvent*)posted[3];Check(first->zone==ZONE_TR,"negative virtual desktop origins map the physical top-right zone correctly");
    Event(WM_MBUTTONDOWN,100,100);Check(Gesture_IsActive(),"mouse-down starts a tracked gesture");g_enabled=0;Event(WM_MBUTTONUP,100,100);g_enabled=1;Check(!Gesture_IsActive() && !g_pointCount,"pausing before mouse-up cancels tracking and samples");
    Event(WM_MBUTTONDOWN,100,100);Gesture_Cancel();Check(!Gesture_IsActive() && !g_downMods,"explicit cancel clears modifier and tracking state");
    capture=1;Event(WM_MBUTTONDOWN,100,100);Check(!Gesture_IsActive(),"Settings capture leaves the gesture recording canvas alone");capture=0;
    outside=1;g_enabled=0;MSLLHOOKSTRUCT data={0};
    Check(MouseProc(HC_ACTION,WM_LBUTTONDOWN,(LPARAM)&data)==42 && outsideCalls,
        "outside click is routed to Dashboard and passed through even while gestures are paused");g_enabled=1;
    capture=1;int routed=outsideCalls;
    Check(MouseProc(HC_ACTION,WM_RBUTTONDOWN,(LPARAM)&data)==42 && outsideCalls==routed+1,
        "outside-click dismissal is independent of Settings input capture");capture=0;
    Check(MouseProc(HC_ACTION,WM_MBUTTONDOWN,(LPARAM)&data)==42 && !Gesture_IsActive(),
        "outside click on the configured gesture button does not start a new gesture");
    routed=outsideCalls;data.flags=LLMHF_INJECTED;MouseProc(HC_ACTION,WM_LBUTTONDOWN,(LPARAM)&data);
    Check(outsideCalls==routed,"injected clicks do not dismiss Dashboard");outside=0;
    Event(WM_MBUTTONDOWN,100,100);Gesture_OnDisplayChange();Check(!Gesture_IsActive(),"display changes cancel stale virtual-screen tracking");
    Gesture_PostEvent(NULL,0,0,0);int last=postCount-1;Gesture_HandleMatch(postedType[last],posted[last]);posted[last]=0;Check(inputs==2,"ordinary click replay is deferred and uses the captured mouse button");
    int executedBefore=execCount;Draw(1800,-1000,-300);last=postCount-1;
    first=(GestureEvent*)posted[last];Check(first->zone==ZONE_TR,"a gesture crossing into Main keeps its original top-right starting zone");
    Gesture_HandleMatch(postedType[last],posted[last]);posted[last]=0;
    Check(execCount==executedBefore,"a corner-started gesture cannot dispatch a Main-only binding");
    g_cfg.gestures[1].zone=ZONE_ALL;Draw(1800,-1000,-300);last=postCount-1;
    Gesture_HandleMatch(postedType[last],posted[last]);posted[last]=0;
    Check(execCount==executedBefore+1 && executed[execCount-1]==101,"an explicit All areas binding dispatches the same corner-started gesture");
    for(int i=0;i<postCount;i++)if(posted[i])free((void*)posted[i]);
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
