/* An isolated UI preview. Uses default in-memory settings and never loads or
   saves the user's configuration, registers hotkeys, or starts app actions. */
#include <assert.h>
#include "../src/main.h"

static int g_testHooks, g_testUnhooks;
static HWND g_testForeground;
static BYTE g_testHeld[256];
static HHOOK Preview_SetHook(int id,HOOKPROC callback,HINSTANCE instance,DWORD thread)
{ return g_testHooks?(HHOOK)(INT_PTR)1:SetWindowsHookExW(id,callback,instance,thread); }
static BOOL Preview_Unhook(HHOOK hook)
{ if(g_testHooks){g_testUnhooks++;return TRUE;}return UnhookWindowsHookEx(hook); }
static HWND Preview_Foreground(void)
{ return g_testHooks?g_testForeground:GetForegroundWindow(); }
static SHORT Preview_KeyState(int vk)
{ return g_testHooks?(g_testHeld[vk]?(SHORT)0x8000:0):GetAsyncKeyState(vk); }
static LRESULT Preview_NextHook(HHOOK hook,int code,WPARAM message,LPARAM param)
{ return g_testHooks?0x51:CallNextHookEx(hook,code,message,param); }
#define SetWindowsHookExW Preview_SetHook
#define UnhookWindowsHookEx Preview_Unhook
#define GetForegroundWindow Preview_Foreground
#define GetAsyncKeyState Preview_KeyState
#define CallNextHookEx Preview_NextHook
#include "../src/ui.c"

static LRESULT TestKey(UINT vk,int extended,int up,DWORD time,int injected)
{
    KBDLLHOOKSTRUCT key={0};key.vkCode=vk;key.time=time;
    key.flags=(extended?LLKHF_EXTENDED:0)|(injected?LLKHF_INJECTED:0)|(up?LLKHF_UP:0);
    return Cmd_RecordHook(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,(LPARAM)&key);
}

static void TestDialog(void)
{
    wchar_t error[256],text[1024];char previous[SC_KEYS_CAP];
    g_testHooks=1;g_testForeground=g_hCmdDlg;
    assert(GetWindowLongPtrW(g_hCmdDlgKeys,GWL_STYLE)&ES_READONLY);
    assert(IsWindowVisible(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD)));
    SC_CopyUtf8(previous,sizeof(previous),g_cmdKeySeq);
    Cmd_RecordStart();assert(g_cmdRecordHook);
    assert(!IsWindowEnabled(GetDlgItem(g_hCmdDlg,101)));
    assert(!IsWindowEnabled(g_hCmdDlgType));
    assert(!IsWindowEnabled(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_CLEAR)));
    int before=g_cfg.commandCount;
    SendMessageW(g_hCmdDlg,WM_COMMAND,101,0);assert(g_cfg.commandCount==before);
    assert(TestKey('A',0,0,0,1)==0x51 && !g_cmdRecording.hasEvent);
    Cmd_RecordStop();assert(!strcmp(previous,g_cmdKeySeq));
    assert(IsWindowEnabled(GetDlgItem(g_hCmdDlg,101)));

    Cmd_RecordStart();
    /* These keys must be consumed rather than navigate, save or close. */
    const UINT keys[]={VK_TAB,VK_RETURN,VK_ESCAPE,VK_LWIN,VK_RCONTROL,VK_OEM_1,VK_NUMPAD0};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
        assert(TestKey(keys[i],i==3 || i==4,0,(DWORD)i*40,0)==1);
        assert(TestKey(keys[i],i==3 || i==4,1,(DWORD)i*40+20,0)==1);
    }
    SendMessageW(g_hCmdDlg,WM_SC_KEY_PREVIEW,0,0);
    GetWindowTextW(g_hCmdDlgKeys,text,1024);assert(wcsstr(text,L"↓") && wcsstr(text,L"↑"));
    Cmd_RecordStop();assert(!g_cmdRecordHook);
    assert(Action_ValidateKeySeq(g_cmdKeySeq,error,256));
    SC_CopyUtf8(previous,sizeof(previous),g_cmdKeySeq);
    SendMessageW(g_hCmdDlg,WM_COMMAND,101,0);
    assert(!g_hCmdDlg && g_cfg.commandCount==before+1);
    assert(!strcmp(g_cfg.commands[before].keySeq,previous));

    /* Reopening preserves serialized data; cancelling does not save a clear. */
    CmdDlg_Show(NULL,before);g_testForeground=g_hCmdDlg;
    assert(!strcmp(g_cmdKeySeq,previous));
    SendMessageW(g_hCmdDlg,WM_COMMAND,CID_CMD_DLG_CLEAR,0);assert(!g_cmdKeySeq[0]);
    SendMessageW(g_hCmdDlg,WM_COMMAND,102,0);
    assert(!strcmp(g_cfg.commands[before].keySeq,previous));

    CmdDlg_Show(NULL,before);g_testForeground=g_hCmdDlg;
    g_testHeld[VK_LCONTROL]=1;Cmd_RecordStart();
    assert(TestKey(VK_LCONTROL,0,1,1,0)==0x51 && !g_cmdRecording.hasEvent);
    g_testHeld[VK_LCONTROL]=0;
    assert(TestKey(VK_LCONTROL,0,0,10,0)==1);
    /* Losing activation balances a modifier that is still held. */
    SendMessageW(g_hCmdDlg,WM_ACTIVATE,WA_INACTIVE,0);
    assert(!g_cmdRecordHook && Action_ValidateKeySeq(g_cmdKeySeq,error,256));
    Cmd_RecordStart();assert(TestKey('A',0,0,10,0)==1);
    g_testForeground=NULL;
    assert(TestKey('A',0,1,20,0)==0x51 && !g_cmdRecordHook);
    assert(Action_ValidateKeySeq(g_cmdKeySeq,error,256));
    g_testForeground=g_hCmdDlg;
    Cmd_RecordStart();assert(TestKey('A',0,0,0,0)==1);
    assert(TestKey('A',0,1,60001,0)==1 && g_cmdRecordFull);
    SendMessageW(g_hCmdDlg,WM_SC_KEY_PREVIEW,0,0);
    assert(!g_cmdRecordHook && Action_ValidateKeySeq(g_cmdKeySeq,error,256));
    Cmd_RecordStart();int unhooks=g_testUnhooks;
    DestroyWindow(g_hCmdDlg);assert(!g_cmdRecordHook && g_testUnhooks==unhooks+1);

    /* Locked built-in commands cannot be overwritten or cleared. */
    int locked=g_cfg.commandCount;
    Command *builtin=&g_cfg.commands[locked];memset(builtin,0,sizeof(*builtin));
    builtin->id=Config_AllocateCommandId(UI_ActualConfig());builtin->type=CMD_KEYS;builtin->locked=1;
    SC_CopyUtf8(builtin->name,sizeof(builtin->name),"Ctrl+T");
    SC_CopyUtf8(builtin->keySeq,sizeof(builtin->keySeq),"{Ctrl down}{T}{Ctrl up}");
    g_cfg.commandCount++;CmdDlg_Show(NULL,locked);g_testForeground=g_hCmdDlg;
    SC_CopyUtf8(previous,sizeof(previous),g_cmdKeySeq);
    assert(!IsWindowEnabled(GetDlgItem(g_hCmdDlg,CID_CMD_DLG_RECORD)));
    SendMessageW(g_hCmdDlg,WM_COMMAND,CID_CMD_DLG_RECORD,0);assert(!g_cmdRecordHook);
    SendMessageW(g_hCmdDlg,WM_COMMAND,CID_CMD_DLG_CLEAR,0);assert(!strcmp(previous,g_cmdKeySeq));
    DestroyWindow(g_hCmdDlg);
    puts("PASS: read-only preview, recording controls, shortcut capture, save/reopen, cancellation, focus loss, limits and locked commands");
}

int main(int argc,char **argv)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();
    HDC screen=GetDC(NULL);
    if(screen){g_dpiScale=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);}
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES|ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    Config_SetDefaults();UI_ActualConfig()->uiLanguage=UI_LANG_CHINESE;
    CmdDlg_Show(NULL,-1);
    SetWindowTextW(g_hCmdDlgName,L"复制并粘贴");
    SendMessageW(g_hCmdDlgType,CB_SETCURSEL,CMD_KEYS,0);CmdDlg_UpdateFields();
    KeyRecording sample;KeyRecord_Reset(&sample);
    KeyRecord_Event(&sample,VK_LCONTROL,0,0,0);
    KeyRecord_Event(&sample,'C',0,0,40);
    KeyRecord_Event(&sample,'C',0,1,80);
    KeyRecord_Event(&sample,VK_LCONTROL,0,1,100);
    KeyRecord_Event(&sample,VK_LCONTROL,0,0,300);
    KeyRecord_Event(&sample,'V',0,0,340);
    KeyRecord_Event(&sample,'V',0,1,380);
    KeyRecord_Event(&sample,VK_LCONTROL,0,1,400);
    KeyRecord_Finish(&sample);
    SC_CopyUtf8(g_cmdKeySeq,sizeof(g_cmdKeySeq),sample.sequence);Cmd_RecordPreview();
    if(argc>1 && !strcmp(argv[1],"--check")){TestDialog();return 0;}
    MSG message;
    while(g_hCmdDlg && GetMessageW(&message,NULL,0,0)>0) {
        if(!UI_ProcessDialogMessage(&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return 0;
}
