#include "main.h"
#include <stdlib.h>

/* ── 状态机 ───────────────────────────────── */
static HHOOK  g_hMouseHook  = NULL;
static int    g_state       = 0; /* 0=IDLE 1=TRACKING */
static int    g_isGesture   = 0; /* 是否已判定为手势（非普通点击） */
static POINT  g_points[1000];
static int    g_pointCount;
static int    g_downMods    = 0; /* 鼠标按下时的修饰键 */
/* Each posted event owns its input snapshot; later gestures cannot replace it. */
typedef struct {
    char chain[64];
    int chainLen, modifier, zone, mouseButton;
} GestureEvent;

static void Gesture_PostEvent(const char *chain, int length, int modifier, int zone)
{
    GestureEvent *event = calloc(1, sizeof(*event));
    if (!event) return;
    if (chain && length > 0 && length < 64) {
        memcpy(event->chain, chain, length);
        event->chainLen = length;
    }
    event->modifier = modifier;
    event->zone = zone;
    event->mouseButton = g_cfg.mouseButton;
    if (!PostMessageW(g_hWnd, WM_SC_MATCH, event->chainLen != 0, (LPARAM)event)) free(event);
}

void Gesture_HandleMatch(WPARAM type, LPARAM payload)
{
    (void)type;
    GestureEvent *owned = (GestureEvent*)payload;
    if (!owned) return;
    GestureEvent event = *owned;
    free(owned);
    if (!g_enabled) return;
    if (event.chainLen == 0) {
        INPUT input[2] = {0};
        input[0].type = input[1].type = INPUT_MOUSE;
        input[0].mi.dwFlags = event.mouseButton == 2 ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_RIGHTDOWN;
        input[1].mi.dwFlags = event.mouseButton == 2 ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_RIGHTUP;
        SendInput(2, input, sizeof(INPUT));
    } else {
        int match = Gesture_Match(event.chain, event.chainLen, event.modifier, event.zone);
        if (match >= 0) Action_ExecuteCustom(&g_cfg.gestures[match]);
    }
}

void Gesture_DiscardPendingEvents(void)
{
    MSG message;
    while (PeekMessageW(&message, g_hWnd, WM_SC_MATCH, WM_SC_MATCH, PM_REMOVE)) free((void*)message.lParam);
}

static int Gesture_Modifiers(void)
{
    int mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= SC_MOD_CTRL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= SC_MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= SC_MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= SC_MOD_WIN;
    return mods;
}

int Gesture_IsActive(void) { return g_state != 0; }

/* ── 方向量化 ──────────────────────────────── */
static int Gesture_QuantizeDir(int dx, int dy, int threshold)
{
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < threshold && ady < threshold) return DIR_NONE;
    if (adx > ady * 2 && adx > threshold)
        return dx > 0 ? DIR_RIGHT : DIR_LEFT;
    else if (ady > adx * 2 && ady > threshold)
        return dy > 0 ? DIR_DOWN : DIR_UP;
    else if (adx > threshold && ady > threshold)
        return (dx > 0)
            ? ((dy > 0) ? DIR_DOWN_RIGHT : DIR_UP_RIGHT)
            : ((dy > 0) ? DIR_DOWN_LEFT  : DIR_UP_LEFT);
    return DIR_NONE;
}

/* ── 降采样 ────────────────────────────────── */
int Gesture_Downsample(const POINT *src, int srcCount,
                       POINT *dst, int dstMax, int minDist)
{
    if (!src || !dst || srcCount < 2 || dstMax < 1 || minDist <= 0) return 0;
    int dc = 0;
    dst[dc++] = src[0];
    int minDistSq = minDist * minDist;
    for (int i = 1; i < srcCount && dc < dstMax; i++) {
        int dx = src[i].x - dst[dc - 1].x;
        int dy = src[i].y - dst[dc - 1].y;
        if (dx * dx + dy * dy >= minDistSq)
            dst[dc++] = src[i];
    }
    if (dc < dstMax) {
        int lx = src[srcCount - 1].x - dst[dc - 1].x;
        int ly = src[srcCount - 1].y - dst[dc - 1].y;
        if (lx * lx + ly * ly >= 4)
            dst[dc++] = src[srcCount - 1];
    }
    return dc;
}

/* ── 方向链编码 ────────────────────────────── */
int Gesture_EncodeChain(const POINT *pts, int count,
                        char *chain, int chainMax)
{
    if (!chain || chainMax < 1) return 0;
    chain[0] = '\0';
    if (!pts || count < 2) return 0;
    int cl = 0, lastDir = DIR_NONE;
    for (int i = 1; i < count && cl < chainMax; i++) {
        int dx = pts[i].x - pts[i - 1].x;
        int dy = pts[i].y - pts[i - 1].y;
        int dir = Gesture_QuantizeDir(dx, dy, 3);
        if (dir != DIR_NONE && dir != lastDir) {
            chain[cl++] = (char)('0' + dir);
            lastDir = dir;
        }
    }
    chain[cl] = '\0';
    return cl;
}

/* ── LCS 匹配 ──────────────────────────────── */
static int ChainSimilarity(const char *a, int aLen, const char *b, int bLen)
{
    if (aLen <= 0 || bLen <= 0 || aLen > 63 || bLen > 63) return 0;
    short dp[65][65];
    for (int i = 0; i <= aLen; i++) dp[i][0] = 0;
    for (int j = 0; j <= bLen; j++) dp[0][j] = 0;
    for (int i = 1; i <= aLen; i++) {
        for (int j = 1; j <= bLen; j++) {
            if (a[i-1] == b[j-1])
                dp[i][j] = dp[i-1][j-1] + 1;
            else
                dp[i][j] = dp[i-1][j] > dp[i][j-1] ? dp[i-1][j] : dp[i][j-1];
        }
    }
    int lcs = dp[aLen][bLen];
    int maxLen = aLen > bLen ? aLen : bLen;
    return (lcs * 100) / maxLen;
}

/* ── 屏幕区域判定 ──────────────────────────── */
int Gesture_GetZone(int x, int y, int screenW, int screenH)
{
    int zw = screenW / 3;
    int zh = screenH / 3;
    if (x < zw && y < zh)      return ZONE_TL;
    if (x > screenW - zw && y < zh)     return ZONE_TR;
    if (x < zw && y > screenH - zh)     return ZONE_BL;
    if (x > screenW - zw && y > screenH - zh) return ZONE_BR;
    return ZONE_MAIN;
}

/* ── 公有：匹配手势 → 返回索引或 -1 ──────────── */
int Gesture_Match(const char *chain, int chainLen, int modifier, int zone)
{
    if (zone < ZONE_MAIN || zone >= ZONE_ALL) return -1;
    int bestIdx = -1, bestScore = 0;
    int bestZone = -1; /* Equal scores prefer the starting zone over All areas. */

    for (int i = 0; i < g_cfg.gestureCount; i++) {
        GestureDef *g = &g_cfg.gestures[i];
        if (!g->enabled) continue;
        if (g->modifier != modifier) continue;

        /* Only an explicit All areas binding may match a different zone. */
        if (g->zone != zone && g->zone != ZONE_ALL) continue;

        for (int s = 0; s < g->chainCount; s++) {
            int tLen = (int)strlen(g->chains[s]);
            int score = ChainSimilarity(chain, chainLen, g->chains[s], tLen);
            if (score >= g_cfg.matchThreshold && score > bestScore) {
                bestScore = score;
                bestIdx = i;
                bestZone = g->zone;
            } else if (score >= g_cfg.matchThreshold && score == bestScore) {
                /* 同分时：精确区域匹配优先于全部区域。 */
                if (bestZone == ZONE_ALL && g->zone == zone) {
                    bestIdx = i;
                    bestZone = g->zone;
                }
            }
        }
    }
    return bestIdx;
}

static void Trail_Show(void);
static void Trail_Update(int x, int y);
static void Trail_Hide(void);

/* ── 鼠标钩子回调 ───────────────────────────── */
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode < 0)
        return CallNextHookEx(NULL, nCode, wp, lp);

    MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lp;
    /* 跳过注入事件（SendInput 合成的点击），防止递归。
       不检查 LLMHF_LOWER_IL_INJECTED——系统应用（任务管理器/CMD 等）
       可能错误地给物理鼠标事件打上此标志，导致手势失灵。 */
    if (ms->flags & LLMHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* Pointer drift after a gesture is not a new command. Only deliberate
       button presses and scrolling cancel pending application activation. */
    if (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN ||
        wp == WM_XBUTTONDOWN || wp == WM_MOUSEWHEEL || wp == WM_MOUSEHWHEEL) Action_NoteUserInput();

    if((wp==WM_LBUTTONDOWN || wp==WM_RBUTTONDOWN || wp==WM_MBUTTONDOWN || wp==WM_XBUTTONDOWN) &&
       Dashboard_HandleOutsideClick(ms->pt))
        return CallNextHookEx(NULL,nCode,wp,lp);

    if (!g_enabled || UI_IsCapturingInput()) {
        if (g_state) Gesture_Cancel();
        return CallNextHookEx(NULL, nCode, wp, lp);
    }

    UINT downMsg, upMsg;
    if (g_cfg.mouseButton == 2) {
        downMsg = WM_MBUTTONDOWN; upMsg = WM_MBUTTONUP;
    } else {
        downMsg = WM_RBUTTONDOWN; upMsg = WM_RBUTTONUP;
    }

    if (wp == downMsg) {
        g_state = 1;
        g_isGesture = 0;
        g_pointCount = 0;
        g_points[g_pointCount++] = ms->pt;
        /* 追踪整个手势期间的修饰键（任意时间按住都算） */
        g_downMods = Gesture_Modifiers();
        Trail_Show();
        return 1; /* 吃掉按下事件，防止右键菜单等干扰 */
    }

    if (g_state == 1) {
        if (wp == WM_MOUSEMOVE) {
            /* 累积修饰键（任意时间按下都算） */
            g_downMods |= Gesture_Modifiers();
            /* 记录路径点（间距采样） */
            if (g_pointCount < 1000) {
                int dx = ms->pt.x - g_points[g_pointCount - 1].x;
                int dy = ms->pt.y - g_points[g_pointCount - 1].y;
                if (dx * dx + dy * dy >= 25) { /* 5px */
                    g_points[g_pointCount++] = ms->pt;
                    /* 移动超过阈值，标记为手势而非普通点击 */
                    { int td = (ms->pt.x - g_points[0].x) * (ms->pt.x - g_points[0].x)
                             + (ms->pt.y - g_points[0].y) * (ms->pt.y - g_points[0].y);
                      if (td >= 64) g_isGesture = 1; } /* >8px from start */
                }
            }
            Trail_Update(ms->pt.x, ms->pt.y);
        }

        if (wp == upMsg) {
            g_state = 0;
            Trail_Hide();

            if (!g_isGesture) {
                /* 普通点击（未拖动），异步合成正常点击 */
                g_pointCount = 0;
                Gesture_PostEvent(NULL, 0, 0, 0);
                return 1;
            }

            /* 手势模式 */
            if (g_pointCount >= 4) {
                /* 检查最小尺寸 + 计算自适应降采样距离 */
                int mix = g_points[0].x, max = g_points[0].x;
                int miy = g_points[0].y, may = g_points[0].y;
                for (int i = 1; i < g_pointCount; i++) {
                    if (g_points[i].x < mix) mix = g_points[i].x;
                    if (g_points[i].x > max) max = g_points[i].x;
                    if (g_points[i].y < miy) miy = g_points[i].y;
                    if (g_points[i].y > may) may = g_points[i].y;
                }
                int boxW = max - mix, boxH = may - miy;
                if (boxW < g_cfg.minGesture && boxH < g_cfg.minGesture) {
                    g_pointCount = 0;
                    return 1;
                }

                /* 自适应降采样距离：与录制一致（包围盒/20，限制[5,50]） */
                int adpDist = (boxW > boxH ? boxW : boxH) / 20;
                if (adpDist < 5)  adpDist = 5;
                if (adpDist > 50) adpDist = 50;

                POINT ds[200];
                int dsCnt = Gesture_Downsample(g_points, g_pointCount, ds, 200, adpDist);
                if (dsCnt >= 2) {
                    char chain[64];
                    int chainLen = Gesture_EncodeChain(ds, dsCnt, chain, 63);
                    if (chainLen >= 1) {
                        /* 累积整个手势期间的修饰键（按下+移动+松开都算） */
                        int mod = g_downMods | Gesture_Modifiers();
                        /* 计算手势起点区域 */
                        int zone = Gesture_GetZone(g_points[0].x - GetSystemMetrics(SM_XVIRTUALSCREEN),
                            g_points[0].y - GetSystemMetrics(SM_YVIRTUALSCREEN),
                            GetSystemMetrics(SM_CXVIRTUALSCREEN),
                            GetSystemMetrics(SM_CYVIRTUALSCREEN));
                        Gesture_PostEvent(chain, chainLen, mod, zone);
                        g_pointCount = 0;
                        return 1; /* 吞掉原始释放事件 */
                    }
                }
            }

            g_pointCount = 0;
            return 1;
        }
    }

    return CallNextHookEx(NULL, nCode, wp, lp);
}

/* ── 实时轨迹窗口 ────────────────────────────── */
static HWND   g_hTrailWnd = NULL;
static int    g_trailOffX = 0, g_trailOffY = 0;
static POINT  g_trailPts[1000];
static int    g_trailCount = 0;

/* 纯黑(0)=色键 → 映射为 RGB(1,1,1)，肉眼无差别 */
static COLORREF FixBlack(COLORREF c) { return (c == 0) ? RGB(1,1,1) : c; }

static LRESULT CALLBACK TrailWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        int ox = g_trailOffX, oy = g_trailOffY;
        /* 分区线（浅绿色，仅四个角落内边界） */
        if (g_cfg.zoneLinesEnabled && g_trailCount > 0) {
            RECT rc; GetClientRect(hWnd, &rc);
            int w = rc.right, h = rc.bottom;
            int x1 = w / 3, x2 = w * 2 / 3;
            int y1 = h / 3, y2 = h * 2 / 3;
            HPEN zp = CreatePen(PS_SOLID, g_cfg.zoneLineWidth, FixBlack(g_cfg.zoneLineColor));
            HPEN old2 = SelectObject(hdc, zp);
            /* 四角 L 形边界，只画内线，不画屏幕边缘 */
            /* TL */ MoveToEx(hdc, x1, 0, NULL);  LineTo(hdc, x1, y1); LineTo(hdc, 0, y1);
            /* TR */ MoveToEx(hdc, x2, 0, NULL);  LineTo(hdc, x2, y1); LineTo(hdc, w, y1);
            /* BL */ MoveToEx(hdc, 0, y2, NULL);  LineTo(hdc, x1, y2); LineTo(hdc, x1, h);
            /* BR */ MoveToEx(hdc, w, y2, NULL);  LineTo(hdc, x2, y2); LineTo(hdc, x2, h);
            SelectObject(hdc, old2);
            DeleteObject(zp);
        }
        if (g_trailCount > 1) {
            /* 保存原始画笔 */
            HPEN oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            SelectObject(hdc, oldPen);
            /* 外发光（宽度为 0 时跳过，只画内芯） */
            if (g_cfg.trailGlowWidth > 0) {
                int gw = g_cfg.trailInnerWidth + g_cfg.trailGlowWidth;
                HPEN glow = CreatePen(PS_SOLID, gw, FixBlack(g_cfg.trailGlowColor));
                HPEN prev = SelectObject(hdc, glow);
                MoveToEx(hdc, g_trailPts[0].x - ox, g_trailPts[0].y - oy, NULL);
                for (int i = 1; i < g_trailCount; i++)
                    LineTo(hdc, g_trailPts[i].x - ox, g_trailPts[i].y - oy);
                SelectObject(hdc, prev);
                DeleteObject(glow);
            }
            /* 内芯 */
            { HPEN inner = CreatePen(PS_SOLID, g_cfg.trailInnerWidth, FixBlack(g_cfg.trailInnerColor));
              HPEN prev = SelectObject(hdc, inner);
              MoveToEx(hdc, g_trailPts[0].x - ox, g_trailPts[0].y - oy, NULL);
              for (int i = 1; i < g_trailCount; i++)
                  LineTo(hdc, g_trailPts[i].x - ox, g_trailPts[i].y - oy);
              SelectObject(hdc, prev);
              DeleteObject(inner); }
        }
        EndPaint(hWnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void Trail_Show(void)
{
    if (!g_cfg.trailEnabled) return;
    RECT virtualScreen = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    virtualScreen.right = virtualScreen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualScreen.bottom = virtualScreen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_hTrailWnd) {
        RECT previous;
        if (!GetWindowRect(g_hTrailWnd, &previous) || !EqualRect(&virtualScreen, &previous)) {
            DestroyWindow(g_hTrailWnd);
            g_hTrailWnd = NULL;
        }
    }
    if (!g_hTrailWnd) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = TrailWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"SC_Trail";
        RegisterClassExW(&wc);
        g_trailOffX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        g_trailOffY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        g_hTrailWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"SC_Trail", L"", WS_POPUP,
            g_trailOffX, g_trailOffY, w, h, NULL, NULL, wc.hInstance, NULL);
        SetLayeredWindowAttributes(g_hTrailWnd, RGB(0,0,0), 0, LWA_COLORKEY);
    }
    g_trailCount = 1;
    g_trailPts[0] = g_points[0];
    ShowWindow(g_hTrailWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hTrailWnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void Trail_Update(int x, int y)
{
    if (!g_hTrailWnd || !g_cfg.trailEnabled) return;
    if (g_trailCount < 1000) {
        int dx = x - g_trailPts[g_trailCount - 1].x;
        int dy = y - g_trailPts[g_trailCount - 1].y;
        if (dx*dx + dy*dy >= 9) {
            g_trailPts[g_trailCount].x = x;
            g_trailPts[g_trailCount].y = y;
            g_trailCount++;
        }
    }
    /* 异步绘制：避免钩子回调中同步阻塞，防止 Windows 因超时静默移除钩子 */
    InvalidateRect(g_hTrailWnd, NULL, FALSE);
    /* Z-order is established when the gesture starts, not on every sample. */
}

static void Trail_Hide(void)
{
    if (g_hTrailWnd) ShowWindow(g_hTrailWnd, SW_HIDE);
    g_trailCount = 0;
}

/* ── 公有接口 ──────────────────────────────── */
void Gesture_Start(void)
{
    if (g_hMouseHook) return;
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc,
                                     GetModuleHandleW(NULL), 0);
    if (!g_hMouseHook) SC_ReportError(SC_Text(L"Mouse input listener", L"鼠标输入监听"), GetLastError());
}

int Gesture_GetHookStatus(void) { return g_hMouseHook != NULL; }

void Gesture_Cancel(void)
{
    g_state = g_isGesture = g_pointCount = g_downMods = 0;
    Trail_Hide();
}

void Gesture_OnDisplayChange(void)
{
    Gesture_Cancel();
    if (g_hTrailWnd) {
        DestroyWindow(g_hTrailWnd);
        g_hTrailWnd = NULL;
    }
}

void Gesture_Stop(void)
{
    Gesture_Cancel();
    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    if (g_hTrailWnd) {
        DestroyWindow(g_hTrailWnd);
        g_hTrailWnd = NULL;
    }
}
