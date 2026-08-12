#include "main.h"

/* ── 状态机 ───────────────────────────────── */
static HHOOK  g_hMouseHook  = NULL;
static int    g_state       = 0; /* 0=IDLE 1=TRACKING */
static int    g_isGesture   = 0; /* 是否已判定为手势（非普通点击） */
static POINT  g_points[1000];
static int    g_pointCount;
static int    g_downMods    = 0; /* 鼠标按下时的修饰键 */
char          g_pendChain[64];   /* 异步匹配用：钩子写→主线程读 */

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
    if (srcCount < 2 || minDist <= 0) return 0;
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
    if (count < 2) return 0;
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
    if (aLen == 0 || bLen == 0) return 0;
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
    int bestIdx = -1, bestScore = 0;
    int bestZone = -1; /* 追踪最佳匹配的 zone：精确匹配 > Main fallback */

    for (int i = 0; i < g_cfg.gestureCount; i++) {
        GestureDef *g = &g_cfg.gestures[i];
        if (!g->enabled) continue;
        if (g->modifier != modifier) continue;

        /* Zone 过滤：只匹配 zone 相同 或 zone=Main(0) */
        if (g->zone != zone && g->zone != ZONE_MAIN) continue;

        for (int s = 0; s < g->chainCount; s++) {
            int tLen = (int)strlen(g->chains[s]);
            int score = ChainSimilarity(chain, chainLen, g->chains[s], tLen);
            if (score >= g_cfg.matchThreshold && score > bestScore) {
                bestScore = score;
                bestIdx = i;
                bestZone = g->zone;
            } else if (score >= g_cfg.matchThreshold && score == bestScore) {
                /* 同分时：精确 zone 匹配优先于 Main fallback */
                if (bestZone == ZONE_MAIN && g->zone == zone) {
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

    if (!g_enabled)
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* Input 弹窗可见时：点击框外直接关闭 */
    if ((wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN) &&
        Popup_CloseIfOutside(ms->pt.x, ms->pt.y))
        return CallNextHookEx(NULL, nCode, wp, lp);

    /* 仪表盘可见时：点击框外直接关闭 */
    if ((wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        if (Dashboard_IsVisible()) {
            RECT r;
            HWND hDash = NULL;
            /* Dashboard 标题可自定义，按窗口类定位以确保首个外部点击也能关闭。 */
            hDash = FindWindowW(L"SC_Dashboard", NULL);
            if (hDash && GetWindowRect(hDash, &r)) {
                if (ms->pt.x < r.left || ms->pt.x > r.right ||
                    ms->pt.y < r.top  || ms->pt.y > r.bottom) {
                    Dashboard_Close();
                }
            }
        }
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
        g_downMods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) g_downMods |= SC_MOD_CTRL;
        if (GetKeyState(VK_SHIFT)   & 0x8000) g_downMods |= SC_MOD_SHIFT;
        if (GetKeyState(VK_MENU)    & 0x8000) g_downMods |= SC_MOD_ALT;
        if (GetKeyState(VK_LWIN)    & 0x8000) g_downMods |= SC_MOD_WIN;
        Trail_Show();
        return 1; /* 吃掉按下事件，防止右键菜单等干扰 */
    }

    if (g_state == 1) {
        if (wp == WM_MOUSEMOVE) {
            /* 累积修饰键（任意时间按下都算） */
            int cm = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) cm |= SC_MOD_CTRL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) cm |= SC_MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) cm |= SC_MOD_ALT;
            if (GetKeyState(VK_LWIN)    & 0x8000) cm |= SC_MOD_WIN;
            g_downMods |= cm;
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
                PostMessageW(g_hWnd, WM_SC_MATCH, 0, (LPARAM)(g_cfg.mouseButton << 24));
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
                        int mod = g_downMods;
                        if (GetKeyState(VK_CONTROL) & 0x8000) mod |= SC_MOD_CTRL;
                        if (GetKeyState(VK_SHIFT)   & 0x8000) mod |= SC_MOD_SHIFT;
                        if (GetKeyState(VK_MENU)    & 0x8000) mod |= SC_MOD_ALT;
                        if (GetKeyState(VK_LWIN)    & 0x8000) mod |= SC_MOD_WIN;
                        for (int ci = 0; ci < chainLen; ci++) g_pendChain[ci] = chain[ci];
                        g_pendChain[chainLen] = 0;
                        /* 计算手势起点区域 */
                        int zone = Gesture_GetZone(g_points[0].x, g_points[0].y,
                            GetSystemMetrics(SM_CXVIRTUALSCREEN),
                            GetSystemMetrics(SM_CYVIRTUALSCREEN));
                        LPARAM l = (LPARAM)(((mod & 0xFF) << 16) | ((zone & 0xFF) << 8) | (UINT)(g_cfg.mouseButton << 24));
                        PostMessageW(g_hWnd, WM_SC_MATCH, (WPARAM)chainLen, l);
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
    /* 刷新 TOPMOST Z-order：对抗任务管理器等系统窗口的同级 TOPMOST 竞争 */
    SetWindowPos(g_hTrailWnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
}

void Gesture_Stop(void)
{
    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    if (g_hTrailWnd) {
        DestroyWindow(g_hTrailWnd);
        g_hTrailWnd = NULL;
    }
}
