/* Read-only diagnosis of foreground ownership, visible Z order and target
   windows. Never changes focus, window styles, input or user configuration. */
#include "../src/main.h"
static DWORD Integrity(DWORD pid)
{
    DWORD value = 0, needed = 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid), token = NULL;
    if (process && OpenProcessToken(process,TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenIntegrityLevel,NULL,0,&needed);
        BYTE *data = needed ? malloc(needed) : NULL;
        if (data && GetTokenInformation(token,TokenIntegrityLevel,data,needed,&needed)) {
            PSID sid = ((TOKEN_MANDATORY_LABEL*)data)->Label.Sid;
            value = *GetSidSubAuthority(sid,*GetSidSubAuthorityCount(sid)-1);
        }
        free(data); CloseHandle(token);
    }
    if (process) CloseHandle(process);
    return value;
}
int main(void)
{
    HWND foreground = GetForegroundWindow();
    printf("foreground=%p\n",(void*)foreground);
    int visible = 0, z = 0;
    for (HWND window = GetTopWindow(NULL); window; window = GetWindow(window,GW_HWNDNEXT)) {
        z++;
        DWORD pid = 0, thread = GetWindowThreadProcessId(window,&pid);
        wchar_t path[32768] = L"", title[192] = L"", cls[128] = L"";
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        DWORD length = 32768;
        if (process) { QueryFullProcessImageNameW(process,0,path,&length); CloseHandle(process); }
        const wchar_t *name = wcsrchr(path,L'\\'); name = name ? name+1 : path;
        int target = !lstrcmpiW(name,L"Clipboard++.exe") || !lstrcmpiW(name,L"ClipboardAgent.exe") ||
            !lstrcmpiW(name,L"ShortCutter.exe");
        int shown = IsWindowVisible(window);
        if (shown) visible++;
        if ((!shown || visible > 30) && !target && window != foreground) continue;
        GetClassNameW(window,cls,128);
        if (target) GetWindowTextW(window,title,192);
        RECT rect = {0}; GetWindowRect(window,&rect);
        wprintf(L"z=%d visibleZ=%d hwnd=%p pid=%lu tid=%lu il=%lu exe=%ls visible=%d iconic=%d style=%llx ex=%llx owner=%p root=%p rect=%ld,%ld,%ld,%ld class=%ls title=%ls\n",
            z,visible,(void*)window,(unsigned long)pid,(unsigned long)thread,(unsigned long)Integrity(pid),name,
            shown,IsIconic(window),(unsigned long long)GetWindowLongPtrW(window,GWL_STYLE),
            (unsigned long long)GetWindowLongPtrW(window,GWL_EXSTYLE),(void*)GetWindow(window,GW_OWNER),
            (void*)GetAncestor(window,GA_ROOTOWNER),rect.left,rect.top,rect.right,rect.bottom,cls,title);
    }
    return 0;
}
