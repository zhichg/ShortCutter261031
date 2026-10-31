/* UI-thread-only antialiased shapes. Text and icons remain native-resolution.
   Windows' built-in alpha compositing is loaded lazily; no bundled runtime. */
typedef BOOL (WINAPI *DashAlphaBlendFn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
static DashAlphaBlendFn g_dashAlphaBlend;
static HMODULE g_dashAlphaModule;
static HDC g_dashShapeDC;
static HBITMAP g_dashShapeBitmap, g_dashShapeOldBitmap;
static DWORD *g_dashShapePixels;
static int g_dashShapeSize;

static int Dash_ShapeSurface(int size)
{
    if (!g_dashAlphaModule) {
        g_dashAlphaModule = LoadLibraryW(L"msimg32.dll");
        if (g_dashAlphaModule)
            g_dashAlphaBlend = (DashAlphaBlendFn)(void*)GetProcAddress(g_dashAlphaModule, "AlphaBlend");
    }
    if (!g_dashAlphaBlend) return 0;
    if (size <= g_dashShapeSize) return 1;
    if (!g_dashShapeDC) g_dashShapeDC = CreateCompatibleDC(NULL);
    if (!g_dashShapeDC) return 0;
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(g_dashShapeDC, &info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    if (!bitmap) return 0;
    HBITMAP previous = SelectObject(g_dashShapeDC, bitmap);
    if (g_dashShapeBitmap) DeleteObject(previous);
    else g_dashShapeOldBitmap = previous;
    g_dashShapeBitmap = bitmap;
    g_dashShapePixels = pixels;
    g_dashShapeSize = size;
    return 1;
}

static DWORD Dash_Premultiplied(COLORREF color, int alpha)
{
    return ((DWORD)alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
           ((GetGValue(color) * alpha / 255) << 8) | (GetBValue(color) * alpha / 255);
}

static BYTE Dash_CornerAlpha(int x, int y, int radius, int stroke)
{
    int covered = 0;
    float outer = (float)radius * radius;
    float inner = (float)(radius - stroke) * (radius - stroke);
    for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
        float dx = radius - (x + (sx + .5f) / 4);
        float dy = radius - (y + (sy + .5f) / 4);
        float distance = dx * dx + dy * dy;
        if (distance <= outer && (!stroke || distance >= inner)) covered++;
    }
    return (BYTE)((covered * 255 + 8) / 16);
}

static void Dash_ColorRect(HDC hdc, RECT rect, COLORREF color)
{
    SetDCBrushColor(hdc, color);
    FillRect(hdc, &rect, GetStockObject(DC_BRUSH));
}

static int Dash_RoundAA(HDC hdc, const RECT *rc, int diameter, int stroke, COLORREF color)
{
    int width = rc->right - rc->left, height = rc->bottom - rc->top;
    int radius = diameter / 2;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    if (radius < 1 || !Dash_ShapeSurface(radius * 2)) return 0;
    if (stroke > radius) stroke = radius;
    if (!stroke) {
        Dash_ColorRect(hdc, (RECT){rc->left + radius, rc->top, rc->right - radius, rc->bottom}, color);
        Dash_ColorRect(hdc, (RECT){rc->left, rc->top + radius, rc->right, rc->bottom - radius}, color);
    } else {
        Dash_ColorRect(hdc, (RECT){rc->left + radius, rc->top, rc->right - radius, rc->top + stroke}, color);
        Dash_ColorRect(hdc, (RECT){rc->left + radius, rc->bottom - stroke, rc->right - radius, rc->bottom}, color);
        Dash_ColorRect(hdc, (RECT){rc->left, rc->top + radius, rc->left + stroke, rc->bottom - radius}, color);
        Dash_ColorRect(hdc, (RECT){rc->right - stroke, rc->top + radius, rc->right, rc->bottom - radius}, color);
    }
    GdiFlush();
    for (int y = 0; y < radius; y++) for (int x = 0; x < radius; x++) {
        DWORD pixel = Dash_Premultiplied(color, Dash_CornerAlpha(x, y, radius, stroke));
        g_dashShapePixels[y * g_dashShapeSize + x] = pixel;
        g_dashShapePixels[y * g_dashShapeSize + radius * 2 - 1 - x] = pixel;
        g_dashShapePixels[(radius * 2 - 1 - y) * g_dashShapeSize + x] = pixel;
        g_dashShapePixels[(radius * 2 - 1 - y) * g_dashShapeSize + radius * 2 - 1 - x] = pixel;
    }
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    for (int n = 0; n < 4; n++) {
        int right = n & 1, bottom = n >> 1;
        g_dashAlphaBlend(hdc, right ? rc->right - radius : rc->left,
            bottom ? rc->bottom - radius : rc->top, radius, radius,
            g_dashShapeDC, right * radius, bottom * radius, radius, radius, blend);
    }
    return 1;
}

static void Dash_LineAA(HDC hdc, int x1, int y1, int x2, int y2, int width, COLORREF color)
{
    int pad = width / 2 + 1;
    int left = (x1 < x2 ? x1 : x2) - pad, top = (y1 < y2 ? y1 : y2) - pad;
    int w = abs(x2 - x1) + pad * 2 + 1, h = abs(y2 - y1) + pad * 2 + 1;
    if (!Dash_ShapeSurface(w > h ? w : h)) {
        HPEN pen = CreatePen(PS_SOLID, width, color), old = SelectObject(hdc, pen);
        MoveToEx(hdc, x1, y1, NULL); LineTo(hdc, x2, y2);
        SelectObject(hdc, old); DeleteObject(pen); return;
    }
    float dx = (float)x2 - x1, dy = (float)y2 - y1, length = dx * dx + dy * dy;
    float half = width / 2.0f;
    GdiFlush();
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int covered = 0;
        for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
            float px = left + x + (sx + .5f) / 4 - x1;
            float py = top + y + (sy + .5f) / 4 - y1;
            float t = length ? (px * dx + py * dy) / length : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float ex = px - t * dx, ey = py - t * dy;
            if (ex * ex + ey * ey <= half * half) covered++;
        }
        g_dashShapePixels[y * g_dashShapeSize + x] = Dash_Premultiplied(color, (covered * 255 + 8) / 16);
    }
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    g_dashAlphaBlend(hdc, left, top, w, h, g_dashShapeDC, 0, 0, w, h, blend);
}

/* A window region has binary coverage. Leave four small squares out of the
   host region and composite its actual painted corner pixels with coverage
   alpha. These owned, input-transparent surfaces never activate or take focus. */
typedef struct {
    HWND corner[4];
    HDC dc;
    HBITMAP bitmap, oldBitmap;
    DWORD *pixels;
    BYTE *coverage;
    int radius, ready;
} DashSmoothFrame;

static void Dash_DestroySmoothFrame(HWND window)
{
    DashSmoothFrame *frame = (DashSmoothFrame*)RemovePropW(window, L"SC_SmoothFrame");
    if (!frame) return;
    for (int n = 0; n < 4; n++) if (frame->corner[n]) DestroyWindow(frame->corner[n]);
    if (frame->dc) {
        SelectObject(frame->dc, frame->oldBitmap);
        DeleteDC(frame->dc);
    }
    if (frame->bitmap) DeleteObject(frame->bitmap);
    free(frame->coverage); free(frame);
}

static void Dash_SyncSmoothFrame(HWND window, int visible)
{
    DashSmoothFrame *frame = (DashSmoothFrame*)GetPropW(window, L"SC_SmoothFrame");
    if (!frame) return;
    RECT rect; GetWindowRect(window, &rect);
    for (int n = 0; n < 4; n++) {
        SetWindowPos(frame->corner[n], HWND_TOPMOST,
            (n & 1) ? rect.right - frame->radius : rect.left,
            (n & 2) ? rect.bottom - frame->radius : rect.top,
            frame->radius, frame->radius, SWP_NOACTIVATE |
            (visible && frame->ready ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

static int Dash_SetSmoothFrame(HWND window, int width, int height, int radius)
{
    DashSmoothFrame *frame = (DashSmoothFrame*)GetPropW(window, L"SC_SmoothFrame");
    if (frame && frame->radius != radius) { Dash_DestroySmoothFrame(window); frame = NULL; }
    if (!frame) {
        frame = calloc(1, sizeof(*frame));
        if (!frame || !SetPropW(window, L"SC_SmoothFrame", frame)) { free(frame); return 0; }
        frame->radius = radius;
        frame->coverage = malloc(radius * radius);
        frame->dc = CreateCompatibleDC(NULL);
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = radius; info.bmiHeader.biHeight = -radius;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        frame->bitmap = CreateDIBSection(frame->dc, &info, DIB_RGB_COLORS, (void**)&frame->pixels, NULL, 0);
        if (!frame->coverage || !frame->dc || !frame->bitmap) { Dash_DestroySmoothFrame(window); return 0; }
        frame->oldBitmap = SelectObject(frame->dc, frame->bitmap);
        for (int y = 0; y < radius; y++) for (int x = 0; x < radius; x++)
            frame->coverage[y * radius + x] = Dash_CornerAlpha(x, y, radius, 0);
        for (int n = 0; n < 4; n++) {
            frame->corner[n] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT |
                WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP | WS_DISABLED,
                0, 0, radius, radius, window, NULL, g_hInst, NULL);
            if (!frame->corner[n]) { Dash_DestroySmoothFrame(window); return 0; }
        }
    }
    HRGN body = CreateRectRgn(radius, 0, width - radius, height);
    HRGN middle = CreateRectRgn(0, radius, width, height - radius);
    int result = body && middle && CombineRgn(body, body, middle, RGN_OR) != ERROR &&
                 SetWindowRgn(window, body, TRUE);
    if (!result && body) DeleteObject(body);
    if (middle) DeleteObject(middle);
    if (!result) { Dash_DestroySmoothFrame(window); return 0; }
    Dash_SyncSmoothFrame(window, IsWindowVisible(window));
    return 1;
}

static void Dash_PaintSmoothFrame(HWND window, HDC painted, BYTE opacity)
{
    DashSmoothFrame *frame = (DashSmoothFrame*)GetPropW(window, L"SC_SmoothFrame");
    if (!frame) return;
    RECT rect; GetWindowRect(window, &rect);
    int r = frame->radius;
    SIZE size = {r, r}; POINT source = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    for (int n = 0; n < 4; n++) {
        int x = (n & 1) ? rect.right - rect.left - r : 0;
        int y = (n & 2) ? rect.bottom - rect.top - r : 0;
        BitBlt(frame->dc, 0, 0, r, r, painted, x, y, SRCCOPY);
        GdiFlush();
        for (int py = 0; py < r; py++) for (int px = 0; px < r; px++) {
            int cx = (n & 1) ? r - 1 - px : px, cy = (n & 2) ? r - 1 - py : py;
            int alpha = frame->coverage[cy * r + cx];
            DWORD rgb = frame->pixels[py * r + px];
            frame->pixels[py * r + px] = ((DWORD)alpha << 24) |
                ((((rgb >> 16) & 255) * alpha / 255) << 16) |
                ((((rgb >> 8) & 255) * alpha / 255) << 8) | ((rgb & 255) * alpha / 255);
        }
        POINT position = {rect.left + x, rect.top + y};
        UpdateLayeredWindow(frame->corner[n], NULL, &position, &size,
                            frame->dc, &source, 0, &blend, ULW_ALPHA);
    }
    frame->ready = 1;
    Dash_SyncSmoothFrame(window, IsWindowVisible(window));
}

static void Dash_DeleteShapeSurface(void)
{
    if (g_dashShapeDC) {
        SelectObject(g_dashShapeDC, g_dashShapeOldBitmap);
        DeleteObject(g_dashShapeBitmap); DeleteDC(g_dashShapeDC);
    }
    g_dashShapeDC = NULL; g_dashShapeBitmap = NULL; g_dashShapePixels = NULL;
    g_dashShapeOldBitmap = NULL; g_dashShapeSize = 0;
    if (g_dashAlphaModule) FreeLibrary(g_dashAlphaModule);
    g_dashAlphaModule = NULL; g_dashAlphaBlend = NULL;
}
