#include <assert.h>
#include "../src/actions.c"
#include "../src/key_record.h"

AppConfig g_cfg;
HWND g_hWnd;
int g_enabled;
/* The parser is tested without starting application actions or changing any
   settings. Unexpected calls into these app services fail immediately. */
void SC_ReportError(const wchar_t *operation,DWORD error) { (void)operation;(void)error;abort(); }
void SC_LogDiagnostic(const wchar_t *message) { (void)message;abort(); }
void SC_SetEnabled(int enabled) { (void)enabled;abort(); }
void Dashboard_Show(void) { abort(); }
int Dashboard_IsVisible(void) { abort(); }
void Dashboard_RecordUsage(int id) { (void)id;abort(); }
void UI_Show(HWND parent) { (void)parent;abort(); }

static void Valid(KeyRecording *r)
{
    wchar_t error[256];
    KeyRecord_Finish(r);
    assert(r->length==strlen(r->sequence));
    assert(r->length<SC_KEYS_CAP);
    assert(Action_ValidateKeySeq(r->sequence,error,256));
}

int main(void)
{
    KeyRecording r;MacroStep steps[MACRO_STEP_MAX];int count=0;
    wchar_t error[256],preview[1024];
    KeyRecord_Reset(&r);
    assert(KeyRecord_Event(&r,VK_LCONTROL,0,0,100));
    assert(KeyRecord_Event(&r,'C',0,0,120));
    assert(KeyRecord_Event(&r,'C',0,1,160));
    assert(KeyRecord_Event(&r,VK_LCONTROL,0,1,180));
    Valid(&r);
    assert(Macro_Parse(r.sequence,steps,&count,error,256));
    assert(count==7 && steps[0].vk==VK_LCONTROL && !steps[0].up);
    assert(steps[1].delay==20 && steps[2].vk=='C');
    assert(steps[3].delay==40 && steps[4].up && steps[4].vk=='C');
    assert(steps[6].vk==VK_LCONTROL && steps[6].up);
    Action_FormatKeySeq(r.sequence,preview,1024);
    assert(wcsstr(preview,L"↓") && wcsstr(preview,L"↑") && wcsstr(preview,L"20 ms"));
    assert(!wcsstr(preview,L"{vk"));

    /* Legacy configuration is still accepted and formatted. */
    assert(Action_ValidateKeySeq("{Ctrl down}{C}{Ctrl up}{Sleep 100}{Alt down}{F4}{Alt up}",error,256));
    assert(Action_ValidateKeySeq("{Win down}{Left}{Win up}",error,256));
    assert(!Action_ValidateKeySeq("{vk00}",error,256));
    assert(!Action_ValidateKeySeq("{vkGG}",error,256));
    assert(!Action_ValidateKeySeq("{vk100}",error,256));
    assert(!Action_ValidateKeySeq("{vk41Q}",error,256));
    assert(!Action_ValidateKeySeq("{vkA3E down}",error,256));

    /* All key codes, extended keys, right modifiers, OEM and numpad keys. */
    for (UINT vk=1;vk<256;vk++) for (int extended=0;extended<2;extended++) {
        KeyRecord_Reset(&r);
        assert(KeyRecord_Event(&r,vk,extended,0,1));
        assert(KeyRecord_Event(&r,vk,extended,1,2));
        Valid(&r);assert(Macro_Parse(r.sequence,steps,&count,error,256));
        assert(count==3 && steps[0].vk==vk && steps[2].vk==vk);
        assert(steps[0].extended==extended && steps[2].extended==extended && steps[2].up);
    }

    /* Ignore orphan releases and auto-repeat without corrupting held state. */
    KeyRecord_Reset(&r);
    assert(KeyRecord_Event(&r,'A',0,1,10) && !r.hasEvent);
    assert(KeyRecord_Event(&r,'A',0,0,20));size_t before=r.length;
    assert(KeyRecord_Event(&r,'A',0,0,30) && r.length==before);
    assert(KeyRecord_Event(&r,'A',0,1,40));Valid(&r);
    assert(r.totalDelay==20);

    /* Finishing while modifiers are held must generate balanced releases. */
    KeyRecord_Reset(&r);
    assert(KeyRecord_Event(&r,VK_LCONTROL,0,0,0));
    assert(KeyRecord_Event(&r,VK_RCONTROL,1,0,0));
    assert(KeyRecord_Event(&r,VK_RWIN,1,0,0));Valid(&r);
    assert(Macro_Parse(r.sequence,steps,&count,error,256) && count==6);

    /* Long pauses split into valid five-second playback steps. */
    KeyRecord_Reset(&r);
    assert(KeyRecord_Event(&r,'A',0,0,0));
    assert(KeyRecord_Event(&r,'A',0,1,60000));
    assert(!KeyRecord_Event(&r,'B',0,0,60001));Valid(&r);
    assert(r.totalDelay==60000);
    assert(Macro_Parse(r.sequence,steps,&count,error,256) && count==14);
    for (int i=1;i<13;i++) assert(steps[i].delay==5000);

    /* Low-level hook timestamps can wrap after ~49 days of uptime. */
    KeyRecord_Reset(&r);
    assert(KeyRecord_Event(&r,'A',0,0,0xfffffff0u));
    assert(KeyRecord_Event(&r,'A',0,1,0x10));Valid(&r);assert(r.totalDelay==32);

    /* Full recordings always have enough space to release every held key. */
    KeyRecord_Reset(&r);
    for (UINT vk=1;vk<256;vk++) if (!KeyRecord_Event(&r,vk,vk%2,0,0)) break;
    assert(r.releaseSpace>0);Valid(&r);
    for (unsigned seed=1;seed<=100;seed++) {
        KeyRecord_Reset(&r);unsigned random=seed;DWORD time=0;
        for (int i=0;i<10000;i++) {
            random=random*1664525u+1013904223u;
            UINT vk=1+(random>>8)%255;time+=random%10;
            if (!KeyRecord_Event(&r,vk,vk%2,!!r.held[vk],time)) break;
        }
        Valid(&r);
    }
    puts("PASS: key recording, playback compilation, legacy sequences, all key codes, timing and capacity boundaries");
    return 0;
}
