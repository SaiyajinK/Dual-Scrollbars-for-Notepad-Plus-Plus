#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// ABI minimale d’un plugin Notepad++
// -----------------------------------------------------------------------------
struct NppData
{
    HWND _nppHandle = nullptr;
    HWND _scintillaMainHandle = nullptr;
    HWND _scintillaSecondHandle = nullptr;
};

using PluginCommand = void(__cdecl*)();

struct ShortcutKey
{
    bool _isCtrl = false;
    bool _isAlt = false;
    bool _isShift = false;
    UCHAR _key = 0;
};

constexpr int kMenuItemSize = 64;

struct FuncItem
{
    wchar_t _itemName[kMenuItemSize] = {};
    PluginCommand _pFunc = nullptr;
    int _cmdID = 0;
    bool _init2Check = false;
    ShortcutKey* _pShKey = nullptr;
};

struct SCNotificationMinimal
{
    NMHDR nmhdr;
};

// -----------------------------------------------------------------------------
// Messages Notepad++
// -----------------------------------------------------------------------------
constexpr UINT NPPMSG = WM_USER + 1000;
constexpr UINT NPPM_SETMENUITEMCHECK = NPPMSG + 40;
constexpr UINT NPPM_ISDARKMODEENABLED = NPPMSG + 107;
constexpr UINT NPPM_GETBOOKMARKID = NPPMSG + 111;
constexpr UINT NPPN_SHUTDOWN = 1009;

// -----------------------------------------------------------------------------
// Messages et constantes Scintilla utilisés par ce plugin
// -----------------------------------------------------------------------------
constexpr UINT SCI_MARKERADD = 2043;
constexpr UINT SCI_MARKERDELETE = 2044;
constexpr UINT SCI_MARKERDELETEALL = 2045;
constexpr UINT SCI_MARKERGET = 2046;
constexpr UINT SCI_GETFIRSTVISIBLELINE = 2152;
constexpr UINT SCI_GETLINECOUNT = 2154;
constexpr UINT SCI_SETMARGINRIGHT = 2157;
constexpr UINT SCI_GETMARGINRIGHT = 2158;
constexpr UINT SCI_SETSEL = 2160;
constexpr UINT SCI_LINESCROLL = 2168;
constexpr UINT SCI_POSITIONFROMLINE = 2167;
constexpr UINT SCI_SCROLLCARET = 2169;
constexpr UINT SCI_GETLINEENDPOSITION = 2136;
constexpr UINT SCI_VISIBLEFROMDOCLINE = 2220;
constexpr UINT SCI_DOCLINEFROMVISIBLE = 2221;
constexpr UINT SCI_GETMARGINWIDTHN = 2243;
constexpr UINT SCI_GETMARGINMASKN = 2245;
constexpr UINT SCI_SETMARGINCURSORN = 2248;
constexpr UINT SCI_GETMARGINCURSORN = 2249;
constexpr UINT SCI_GETMARGINS = 2253;
constexpr UINT SCI_TEXTHEIGHT = 2279;
constexpr UINT SCI_LINESONSCREEN = 2370;
constexpr UINT SCI_GETZOOM = 2374;
constexpr UINT SCI_SETFIRSTVISIBLELINE = 2613;

constexpr UINT SCI_STYLEGETFORE = 2481;
constexpr UINT SCI_STYLEGETBACK = 2482;
constexpr UINT SCI_STYLEGETBOLD = 2483;
constexpr UINT SCI_STYLEGETITALIC = 2484;
constexpr UINT SCI_STYLEGETSIZE = 2485;
constexpr UINT SCI_STYLEGETFONT = 2486;
constexpr UINT SCI_STYLEGETUNDERLINE = 2488;
constexpr UINT SCI_STYLEGETCHARACTERSET = 2490;
constexpr UINT SCI_STYLEGETSIZEFRACTIONAL = 2062;
constexpr UINT SCI_STYLEGETWEIGHT = 2064;

constexpr int STYLE_LINENUMBER = 33;
constexpr int SC_CURSORARROW = 2;
constexpr int kLineNumberMargin = 0;
constexpr int kMaxRememberedMargins = 16;
constexpr std::uint32_t SC_MASK_FOLDERS = 0xFE000000U;

constexpr wchar_t kPluginName[] = L"Dual Scrollbars";
constexpr wchar_t kToggleCommand[] = L"Displays a double scrollbar";
constexpr wchar_t kLeftRailClass[] = L"DualScrollbars.LeftRail.v24";
constexpr wchar_t kRightNumbersClass[] = L"DualScrollbars.RightNumbers.v24";
constexpr UINT_PTR kScintillaSubclassId = 0x44534352; // DSCR
constexpr UINT_PTR kScrollbarSubclassId = 0x44534252; // DSBR
constexpr UINT kSyncIntervalMs = 40;
constexpr UINT kMirrorRefreshMessage = WM_APP + 0x194;
constexpr DWORD kMirrorDebounceMs = 25;
constexpr int kLeftGapDip = 0;
constexpr int kRightSeparatorDip = 1;

struct FontSignature
{
    std::wstring face;
    int sizeFractional = 0;
    int zoom = 0;
    int weight = FW_NORMAL;
    bool italic = false;
    bool underline = false;
    BYTE charset = DEFAULT_CHARSET;

    bool operator==(const FontSignature& other) const
    {
        return face == other.face &&
            sizeFractional == other.sizeFractional &&
            zoom == other.zoom &&
            weight == other.weight &&
            italic == other.italic &&
            underline == other.underline &&
            charset == other.charset;
    }
};

struct ViewState
{
    HWND scintilla = nullptr;
    HWND host = nullptr;

    HWND leftRail = nullptr;
    HWND leftScrollbar = nullptr;
    HWND rightNumbers = nullptr;

    RECT fullRect{}; // Rectangle que Notepad++ réserve à la vue Scintilla.
    bool haveFullRect = false;
    bool internalLayout = false;
    bool scintillaSubclassed = false;
    bool scrollbarSubclassed = false;

    int scrollbarWidth = 0;
    int gapWidth = 0;
    int rightSeparatorWidth = 1;
    COLORREF rightSeparatorColor = RGB(100, 100, 100);

    int baseRightMargin = 0;
    int appliedRightMargin = 0;
    int nativeLineNumberWidth = 0;
    int bookmarkAreaWidth = 0;
    int bookmarkMarginIndex = -1;
    int bookmarkMarginX = 0;
    int rightNumberWidth = 0; // Marges natives reproduites en miroir + séparateur, sans espace sombre supplémentaire.
    std::uint64_t nativeMarginLayoutSignature = 0;

    std::vector<std::uint8_t> mirrorPixels;
    int mirrorBitmapWidth = 0;
    int mirrorBitmapHeight = 0;
    bool mirrorCaptureInProgress = false;
    bool mirrorDirty = true;
    DWORD mirrorDirtySince = 0;
    int wheelDeltaRemainder = 0;
    bool forwardingWheelToScintilla = false;

    LONG_PTR originalScintillaStyle = 0;
    bool addedClipChildren = false;

    bool rightMarginMouseGesture = false;
    int rightGestureMargin = -1;

    int marginCount = 0;
    std::array<int, kMaxRememberedMargins> originalMarginCursors{};

    COLORREF lineNumberFore = RGB(0, 0, 0);
    COLORREF lineNumberBack = RGB(255, 255, 255);
    HFONT lineNumberFont = nullptr;
    FontSignature fontSignature{};
    bool haveFontSignature = false;
    int lineHeight = 16;

    int lastFirstVisible = INT_MIN;
    int lastLineCount = INT_MIN;
    int lastLineHeight = INT_MIN;
    int lastRightWidth = INT_MIN;
    std::uint64_t lastBookmarkSignature = ~std::uint64_t{0};

    int selectionAnchorLine = -1;
    bool selectingFromRight = false;

    int lastScrollMin = INT_MIN;
    int lastScrollMax = INT_MIN;
    UINT lastScrollPage = UINT_MAX;
    int lastScrollPos = INT_MIN;
};

static HINSTANCE g_instance = nullptr;
static NppData g_npp;
static FuncItem g_funcItems[1];
static std::array<ViewState, 2> g_views;
static UINT_PTR g_timerId = 0;
static bool g_enabled = true;
static bool g_started = false;
static bool g_cleaned = false;
static int g_lastDarkMode = -1;
static unsigned g_visualRefreshTick = 0;
static int g_bookmarkMarkerId = 24;
static COLORREF g_sharedSeparatorColor = RGB(100, 100, 100);
static bool g_haveSharedSeparatorColor = false;

static LRESULT CALLBACK ScintillaSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData);

static LRESULT CALLBACK ScrollbarSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData);

static LRESULT CALLBACK LeftRailWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK RightNumbersWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static void CALLBACK SyncTimerProc(HWND, UINT, UINT_PTR, DWORD);
static void TogglePlugin();

static LRESULT SciCall(HWND scintilla, UINT message, WPARAM wParam = 0, LPARAM lParam = 0)
{
    return ::SendMessageW(scintilla, message, wParam, lParam);
}

static int RectWidth(const RECT& rect)
{
    return std::max(0, static_cast<int>(rect.right - rect.left));
}

static int RectHeight(const RECT& rect)
{
    return std::max(0, static_cast<int>(rect.bottom - rect.top));
}

static bool GetWindowRectInParent(HWND hwnd, HWND parent, RECT& result)
{
    RECT screen{};
    if (!hwnd || !parent || !::GetWindowRect(hwnd, &screen))
        return false;

    POINT points[2] = {
        { screen.left, screen.top },
        { screen.right, screen.bottom }
    };
    ::MapWindowPoints(nullptr, parent, points, 2);

    result.left = points[0].x;
    result.top = points[0].y;
    result.right = points[1].x;
    result.bottom = points[1].y;
    return true;
}

static UINT GetDpiForWindowCompat(HWND hwnd)
{
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
    {
        auto function = reinterpret_cast<GetDpiForWindowFn>(
            ::GetProcAddress(user32, "GetDpiForWindow"));
        if (function)
        {
            const UINT dpi = function(hwnd);
            if (dpi)
                return dpi;
        }
    }

    HDC dc = ::GetDC(hwnd);
    const UINT dpi = dc ? static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc)
        ::ReleaseDC(hwnd, dc);
    return dpi ? dpi : 96;
}

static int ScaleDip(HWND hwnd, int dip)
{
    return std::max(0, ::MulDiv(dip, static_cast<int>(GetDpiForWindowCompat(hwnd)), 96));
}

static int GetScrollbarWidthForWindow(HWND hwnd)
{
    using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
    {
        auto function = reinterpret_cast<GetSystemMetricsForDpiFn>(
            ::GetProcAddress(user32, "GetSystemMetricsForDpi"));
        if (function)
            return std::max(1, function(SM_CXVSCROLL, GetDpiForWindowCompat(hwnd)));
    }

    return std::max(1, ::GetSystemMetrics(SM_CXVSCROLL));
}

static int GetLeftNonClientBorderWidth(HWND hwnd)
{
    if (!hwnd || !::IsWindow(hwnd))
        return 1;

    RECT windowRect{};
    POINT clientOrigin{ 0, 0 };
    if (!::GetWindowRect(hwnd, &windowRect) ||
        !::ClientToScreen(hwnd, &clientOrigin))
    {
        return 1;
    }

    return std::max(1, static_cast<int>(clientOrigin.x - windowRect.left));
}

static COLORREF SampleLeftNonClientBorderColor(HWND hwnd, COLORREF fallback)
{
    if (!hwnd || !::IsWindow(hwnd))
        return fallback;

    RECT windowRect{};
    RECT clientRect{};
    POINT clientOrigin{ 0, 0 };
    if (!::GetWindowRect(hwnd, &windowRect) ||
        !::GetClientRect(hwnd, &clientRect) ||
        !::ClientToScreen(hwnd, &clientOrigin))
    {
        return fallback;
    }

    const int borderWidth = clientOrigin.x - windowRect.left;
    if (borderWidth <= 0)
        return fallback;

    HDC windowDc = ::GetWindowDC(hwnd);
    if (!windowDc)
        return fallback;

    const int x = std::max(0, borderWidth - 1);
    const int clientTopInWindow = clientOrigin.y - windowRect.top;
    const int y = std::max(0, clientTopInWindow + RectHeight(clientRect) / 2);
    const COLORREF sampled = ::GetPixel(windowDc, x, y);
    ::ReleaseDC(hwnd, windowDc);

    return sampled == CLR_INVALID ? fallback : sampled;
}

static int RightDecorationWidth(const ViewState& view)
{
    return std::max(1, view.rightSeparatorWidth) + std::max(0, view.gapWidth);
}

static int LeftReserveWidth(const ViewState& view)
{
    // Reproduit la bordure extérieure native visible à l’extrême droite :
    // bordure extérieure grise | barre de défilement gauche | éditeur (sans espace sombre supplémentaire).
    return std::max(1, view.rightSeparatorWidth) +
        std::max(1, view.scrollbarWidth) +
        std::max(0, view.gapWidth);
}

static std::uint32_t BookmarkMask()
{
    return (g_bookmarkMarkerId >= 0 && g_bookmarkMarkerId < 32)
        ? (std::uint32_t{1} << g_bookmarkMarkerId)
        : 0;
}

static bool LineHasBookmark(const ViewState& view, int documentLine)
{
    if (!view.scintilla || documentLine < 0)
        return false;

    const auto markers = static_cast<std::uint32_t>(
        SciCall(view.scintilla, SCI_MARKERGET, static_cast<WPARAM>(documentLine)));
    return (markers & BookmarkMask()) != 0;
}

struct BookmarkMarginGeometry
{
    int index = -1;
    int x = 0;
    int width = 0;
};

static BookmarkMarginGeometry FindBookmarkMarginGeometry(const ViewState& view)
{
    BookmarkMarginGeometry geometry{};
    const int fallback = std::max(ScaleDip(view.scintilla, 14), 10);
    const std::uint32_t bookmarkMask = BookmarkMask();
    const int count = std::clamp(
        static_cast<int>(SciCall(view.scintilla, SCI_GETMARGINS)),
        0,
        kMaxRememberedMargins);

    int x = 0;
    for (int margin = 0; margin < count; ++margin)
    {
        const int width = std::max(0, static_cast<int>(
            SciCall(view.scintilla, SCI_GETMARGINWIDTHN, margin)));
        const auto mask = static_cast<std::uint32_t>(
            SciCall(view.scintilla, SCI_GETMARGINMASKN, margin));

        if (margin != kLineNumberMargin &&
            (mask & bookmarkMask) != 0 &&
            width > 0)
        {
            geometry.index = margin;
            geometry.x = x;
            geometry.width = width;
            return geometry;
        }

        x += width;
    }

    geometry.width = fallback;
    return geometry;
}


struct NativeMarginSegment
{
    int index = -1;
    int x = 0;
    int width = 0;
    std::uint32_t mask = 0;
};

struct NativeMarginLayout
{
    int count = 0;
    int totalWidth = 0;
    std::array<NativeMarginSegment, kMaxRememberedMargins> segments{};
};

static NativeMarginLayout ReadNativeMarginLayout(const ViewState& view)
{
    NativeMarginLayout layout{};
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return layout;

    layout.count = std::clamp(
        static_cast<int>(SciCall(view.scintilla, SCI_GETMARGINS)),
        0,
        kMaxRememberedMargins);

    int x = 0;
    for (int margin = 0; margin < layout.count; ++margin)
    {
        const int width = std::max(
            0,
            static_cast<int>(SciCall(view.scintilla, SCI_GETMARGINWIDTHN, margin)));

        layout.segments[margin].index = margin;
        layout.segments[margin].x = x;
        layout.segments[margin].width = width;
        layout.segments[margin].mask = static_cast<std::uint32_t>(
            SciCall(view.scintilla, SCI_GETMARGINMASKN, margin));
        x += width;
    }

    layout.totalWidth = x;
    return layout;
}

static std::uint64_t ComputeNativeMarginLayoutSignature(const NativeMarginLayout& layout)
{
    std::uint64_t hash = 1469598103934665603ULL;
    hash ^= static_cast<std::uint64_t>(layout.count);
    hash *= 1099511628211ULL;

    for (int margin = 0; margin < layout.count; ++margin)
    {
        hash ^= static_cast<std::uint64_t>(layout.segments[margin].width + 1);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(layout.segments[margin].mask);
        hash *= 1099511628211ULL;
    }

    return hash;
}

static bool IsFoldMargin(const NativeMarginSegment& segment)
{
    return (segment.mask & SC_MASK_FOLDERS) != 0;
}

static void InvalidateMirroredMargins(ViewState& view)
{
    if (view.rightNumbers && ::IsWindow(view.rightNumbers))
        ::InvalidateRect(view.rightNumbers, nullptr, FALSE);
}

static void MarkMirrorDirty(ViewState& view)
{
    if (!view.mirrorDirty)
        view.mirrorDirtySince = ::GetTickCount();
    view.mirrorDirty = true;
}

static void DrawReadableMirroredLineNumbers(
    ViewState& view,
    HDC target,
    const NativeMarginLayout& layout,
    int totalWidth,
    int height)
{
    if (!target || layout.count <= kLineNumberMargin)
        return;

    const NativeMarginSegment& numbers = layout.segments[kLineNumberMargin];
    if (numbers.width <= 0)
        return;

    const int destinationX = totalWidth - (numbers.x + numbers.width);
    RECT numberArea{
        destinationX,
        0,
        destinationX + numbers.width,
        height
    };

    HBRUSH background = ::CreateSolidBrush(view.lineNumberBack);
    if (background)
    {
        ::FillRect(target, &numberArea, background);
        ::DeleteObject(background);
    }

    ::SetBkMode(target, TRANSPARENT);
    ::SetTextColor(target, view.lineNumberFore);
    HFONT oldFont = nullptr;
    if (view.lineNumberFont)
        oldFont = static_cast<HFONT>(::SelectObject(target, view.lineNumberFont));

    const int firstVisible = static_cast<int>(
        SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int lineCount = std::max(
        1,
        static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT)));
    const int lineHeight = std::max(1, view.lineHeight);
    const int rows = height / lineHeight + 2;
    const int padding = std::max(1, ScaleDip(view.scintilla, 2));

    for (int row = 0; row < rows; ++row)
    {
        const int displayLine = firstVisible + row;
        const int documentLine = static_cast<int>(
            SciCall(view.scintilla, SCI_DOCLINEFROMVISIBLE, displayLine));
        if (documentLine < 0 || documentLine >= lineCount)
            continue;

        const int firstDisplayForDocument = static_cast<int>(
            SciCall(view.scintilla, SCI_VISIBLEFROMDOCLINE, documentLine));
        if (firstDisplayForDocument != displayLine)
            continue;

        wchar_t number[32] = {};
        ::_snwprintf_s(
            number,
            _countof(number),
            _TRUNCATE,
            L"%d",
            documentLine + 1);

        RECT rowRect{
            destinationX + padding,
            row * lineHeight,
            destinationX + numbers.width,
            (row + 1) * lineHeight
        };
        ::DrawTextW(
            target,
            number,
            -1,
            &rowRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    }

    if (oldFont)
        ::SelectObject(target, oldFont);
}

static bool CaptureMirroredMargins(ViewState& view, bool forceInvalidate = false)
{
    if (view.mirrorCaptureInProgress ||
        !view.scintilla || !::IsWindow(view.scintilla) ||
        !view.rightNumbers || !::IsWindow(view.rightNumbers))
    {
        return false;
    }

    const NativeMarginLayout layout = ReadNativeMarginLayout(view);
    if (layout.totalWidth <= 0)
        return false;

    RECT client{};
    if (!::GetClientRect(view.scintilla, &client))
        return false;

    const int nativeWidth = std::min(layout.totalWidth, RectWidth(client));
    const int decorationWidth = RightDecorationWidth(view);
    const int width = std::min(nativeWidth + decorationWidth, RectWidth(client));
    const int height = RectHeight(client);
    if (nativeWidth <= 0 || width <= 0 || height <= 0)
        return false;

    view.mirrorCaptureInProgress = true;

    HDC source = ::GetDC(view.scintilla);
    if (!source)
    {
        view.mirrorCaptureInProgress = false;
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height; // Image stockée de haut en bas.
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bitmapBits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(
        source,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &bitmapBits,
        nullptr,
        0);
    HDC buffer = bitmap ? ::CreateCompatibleDC(source) : nullptr;
    HGDIOBJ oldBitmap = nullptr;

    if (buffer && bitmap)
        oldBitmap = ::SelectObject(buffer, bitmap);

    if (!bitmap || !buffer || !oldBitmap || !bitmapBits)
    {
        if (oldBitmap)
            ::SelectObject(buffer, oldBitmap);
        if (buffer)
            ::DeleteDC(buffer);
        if (bitmap)
            ::DeleteObject(bitmap);
        ::ReleaseDC(view.scintilla, source);
        view.mirrorCaptureInProgress = false;
        return false;
    }

    RECT bufferRect{ 0, 0, width, height };
    HBRUSH background = ::CreateSolidBrush(view.lineNumberBack);
    if (background)
    {
        ::FillRect(buffer, &bufferRect, background);
        ::DeleteObject(background);
    }

    // Reproduit en miroir toute la bande des marges natives en une seule opération. Cela conserve
    // exactement chaque pixel et chaque espacement : numéros de ligne, signets, branches
    // de pliage, marqueurs de l’historique des modifications et tout futur contenu natif des marges.
    ::SetStretchBltMode(buffer, COLORONCOLOR);
    ::StretchBlt(
        buffer,
        nativeWidth,
        0,
        -nativeWidth,
        height,
        source,
        0,
        0,
        nativeWidth,
        height,
        SRCCOPY);

    // Un miroir exact inverserait les chiffres. Seuls les glyphes des numéros de ligne sont
    // redessinés afin de conserver une disposition parfaitement symétrique tout en gardant les nombres lisibles.
    DrawReadableMirroredLineNumbers(view, buffer, layout, nativeWidth, height);

    // Reproduit le séparateur situé à côté de la barre de défilement native de droite :
    // marges reproduites en miroir | bordure grise | barre de défilement native (sans espace sombre supplémentaire).
    const int separatorWidth = std::min(
        std::max(1, view.rightSeparatorWidth),
        std::max(0, width - nativeWidth));
    if (separatorWidth > 0)
    {
        RECT separatorRect{
            nativeWidth,
            0,
            nativeWidth + separatorWidth,
            height
        };
        HBRUSH separatorBrush = ::CreateSolidBrush(view.rightSeparatorColor);
        if (separatorBrush)
        {
            ::FillRect(buffer, &separatorRect, separatorBrush);
            ::DeleteObject(separatorBrush);
        }
    }

    ::GdiFlush();

    const std::size_t byteCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        4U;

    bool changed =
        view.mirrorBitmapWidth != width ||
        view.mirrorBitmapHeight != height ||
        view.mirrorPixels.size() != byteCount;

    if (!changed && byteCount > 0)
        changed = std::memcmp(view.mirrorPixels.data(), bitmapBits, byteCount) != 0;

    if (changed)
    {
        const auto* begin = static_cast<const std::uint8_t*>(bitmapBits);
        view.mirrorPixels.assign(begin, begin + byteCount);
        view.mirrorBitmapWidth = width;
        view.mirrorBitmapHeight = height;
    }

    ::SelectObject(buffer, oldBitmap);
    ::DeleteDC(buffer);
    ::DeleteObject(bitmap);
    ::ReleaseDC(view.scintilla, source);
    view.mirrorCaptureInProgress = false;

    if (changed || forceInvalidate)
        InvalidateMirroredMargins(view);

    return changed;
}

static void RefreshMirrorIfReady(ViewState& view, bool force = false)
{
    if (!view.mirrorDirty && !force)
        return;

    const DWORD now = ::GetTickCount();
    if (!force && now - view.mirrorDirtySince < kMirrorDebounceMs)
        return;

    CaptureMirroredMargins(view, force);
    view.mirrorDirty = false;
}

static void PaintMirroredMarginsWindow(ViewState& view, HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd, &paint);
    if (!dc)
        return;

    RECT client{};
    ::GetClientRect(hwnd, &client);

    const int width = RectWidth(client);
    const int height = RectHeight(client);

    if (width > 0 &&
        height > 0 &&
        view.mirrorBitmapWidth == width &&
        view.mirrorBitmapHeight == height &&
        !view.mirrorPixels.empty())
    {
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        ::SetDIBitsToDevice(
            dc,
            0,
            0,
            static_cast<DWORD>(width),
            static_cast<DWORD>(height),
            0,
            0,
            0,
            static_cast<UINT>(height),
            view.mirrorPixels.data(),
            &bitmapInfo,
            DIB_RGB_COLORS);
    }
    else
    {
        HBRUSH background = ::CreateSolidBrush(view.lineNumberBack);
        if (background)
        {
            ::FillRect(dc, &client, background);
            ::DeleteObject(background);
        }
    }

    ::EndPaint(hwnd, &paint);
}

struct MappedMarginPoint
{
    int marginIndex = -1;
    int nativeX = -1;
};

static MappedMarginPoint MapRightChildXToNativeMargin(
    const ViewState& view,
    int childX)
{
    MappedMarginPoint mapped{};
    const NativeMarginLayout layout = ReadNativeMarginLayout(view);
    if (layout.totalWidth <= 0 || childX < 0 || childX >= layout.totalWidth)
        return mapped;

    // La bande de droite est le miroir horizontal exact de la bande native de gauche.
    mapped.nativeX = layout.totalWidth - 1 - childX;

    for (int margin = 0; margin < layout.count; ++margin)
    {
        const NativeMarginSegment& segment = layout.segments[margin];
        if (mapped.nativeX >= segment.x &&
            mapped.nativeX < segment.x + segment.width)
        {
            mapped.marginIndex = segment.index;
            break;
        }
    }

    return mapped;
}

static COLORREF ScintillaColourToColorRef(LRESULT value)
{
    return static_cast<COLORREF>(value & 0x00FFFFFF);
}

static std::wstring ReadStyleFontName(HWND scintilla)
{
    char fontName[128] = {};
    SciCall(
        scintilla,
        SCI_STYLEGETFONT,
        STYLE_LINENUMBER,
        reinterpret_cast<LPARAM>(fontName));

    if (!fontName[0])
        return L"Consolas";

    wchar_t wide[128] = {};
    int count = ::MultiByteToWideChar(CP_UTF8, 0, fontName, -1, wide, 128);
    if (!count)
        count = ::MultiByteToWideChar(CP_ACP, 0, fontName, -1, wide, 128);

    return count ? std::wstring(wide) : std::wstring(L"Consolas");
}

static FontSignature ReadFontSignature(const ViewState& view)
{
    FontSignature signature{};
    signature.face = ReadStyleFontName(view.scintilla);

    signature.sizeFractional = static_cast<int>(
        SciCall(view.scintilla, SCI_STYLEGETSIZEFRACTIONAL, STYLE_LINENUMBER));
    if (signature.sizeFractional <= 0)
    {
        signature.sizeFractional = static_cast<int>(
            SciCall(view.scintilla, SCI_STYLEGETSIZE, STYLE_LINENUMBER)) * 100;
    }

    signature.zoom = static_cast<int>(SciCall(view.scintilla, SCI_GETZOOM));

    signature.weight = static_cast<int>(
        SciCall(view.scintilla, SCI_STYLEGETWEIGHT, STYLE_LINENUMBER));
    if (signature.weight <= 0)
    {
        signature.weight = SciCall(view.scintilla, SCI_STYLEGETBOLD, STYLE_LINENUMBER)
            ? FW_BOLD
            : FW_NORMAL;
    }

    signature.italic = !!SciCall(view.scintilla, SCI_STYLEGETITALIC, STYLE_LINENUMBER);
    signature.underline = !!SciCall(view.scintilla, SCI_STYLEGETUNDERLINE, STYLE_LINENUMBER);
    signature.charset = static_cast<BYTE>(
        SciCall(view.scintilla, SCI_STYLEGETCHARACTERSET, STYLE_LINENUMBER));
    return signature;
}

static void UpdateLineNumberVisuals(ViewState& view, bool force)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return;

    const COLORREF fore = ScintillaColourToColorRef(
        SciCall(view.scintilla, SCI_STYLEGETFORE, STYLE_LINENUMBER));
    const COLORREF back = ScintillaColourToColorRef(
        SciCall(view.scintilla, SCI_STYLEGETBACK, STYLE_LINENUMBER));
    const int height = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_TEXTHEIGHT, 0)));
    const FontSignature signature = ReadFontSignature(view);

    const bool fontChanged = force || !view.haveFontSignature || !(signature == view.fontSignature);
    const bool appearanceChanged = force ||
        fore != view.lineNumberFore ||
        back != view.lineNumberBack ||
        height != view.lineHeight;

    view.lineNumberFore = fore;
    view.lineNumberBack = back;
    view.lineHeight = height;
    view.rightSeparatorColor = SampleLeftNonClientBorderColor(
        view.scintilla,
        view.rightSeparatorColor);

    if (fontChanged)
    {
        if (view.lineNumberFont)
        {
            ::DeleteObject(view.lineNumberFont);
            view.lineNumberFont = nullptr;
        }

        const int effectiveHundredths = std::max(100, signature.sizeFractional + signature.zoom * 100);
        const int pixelHeight = std::max(
            1,
            ::MulDiv(effectiveHundredths, static_cast<int>(GetDpiForWindowCompat(view.scintilla)), 7200));

        view.lineNumberFont = ::CreateFontW(
            -pixelHeight,
            0,
            0,
            0,
            signature.weight,
            signature.italic ? TRUE : FALSE,
            signature.underline ? TRUE : FALSE,
            FALSE,
            signature.charset,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            signature.face.c_str());

        view.fontSignature = signature;
        view.haveFontSignature = true;
    }

    if (fontChanged || appearanceChanged)
        CaptureMirroredMargins(view, true);
    if (appearanceChanged && view.leftRail)
        ::InvalidateRect(view.leftRail, nullptr, TRUE);
}

static COLORREF ReadSharedSeparatorColor()
{
    HWND reference = nullptr;
    if (g_npp._scintillaMainHandle && ::IsWindow(g_npp._scintillaMainHandle))
        reference = g_npp._scintillaMainHandle;
    else if (g_npp._scintillaSecondHandle && ::IsWindow(g_npp._scintillaSecondHandle))
        reference = g_npp._scintillaSecondHandle;

    return SampleLeftNonClientBorderColor(
        reference,
        g_haveSharedSeparatorColor
            ? g_sharedSeparatorColor
            : ::GetSysColor(COLOR_3DSHADOW));
}

static void RefreshSharedSeparatorColor(bool force)
{
    const COLORREF sampled = ReadSharedSeparatorColor();
    if (!force && g_haveSharedSeparatorColor && sampled == g_sharedSeparatorColor)
        return;

    g_sharedSeparatorColor = sampled;
    g_haveSharedSeparatorColor = true;

    for (auto& view : g_views)
    {
        if (!view.scintilla || !::IsWindow(view.scintilla))
            continue;

        const bool changed = view.rightSeparatorColor != g_sharedSeparatorColor;
        view.rightSeparatorColor = g_sharedSeparatorColor;

        if (changed || force)
        {
            if (view.leftRail && ::IsWindow(view.leftRail))
                ::InvalidateRect(view.leftRail, nullptr, TRUE);

            MarkMirrorDirty(view);
            RefreshMirrorIfReady(view, true);
        }
    }
}

static void ApplyScrollbarTheme(ViewState& view)
{
    if (!view.leftScrollbar || !::IsWindow(view.leftScrollbar))
        return;

    const BOOL darkMode = g_npp._nppHandle
        ? static_cast<BOOL>(::SendMessageW(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0))
        : FALSE;

    ::SetWindowTheme(view.leftScrollbar, darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ::RedrawWindow(
        view.leftScrollbar,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_FRAME | RDW_ERASE);
}

static void ApplyThemeIfNeeded()
{
    const int darkMode = g_npp._nppHandle
        ? (::SendMessageW(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) ? 1 : 0)
        : 0;

    if (darkMode == g_lastDarkMode)
        return;

    g_lastDarkMode = darkMode;
    RefreshSharedSeparatorColor(true);
    for (auto& view : g_views)
    {
        ApplyScrollbarTheme(view);
        UpdateLineNumberVisuals(view, true);
    }
}

static void PositionLeftRail(ViewState& view)
{
    if (!view.leftRail || !view.haveFullRect || !view.host)
        return;

    const bool visible = ::IsWindowVisible(view.scintilla) && RectHeight(view.fullRect) > 0;
    if (!visible)
    {
        ::ShowWindow(view.leftRail, SW_HIDE);
        return;
    }

    ::SetWindowPos(
        view.leftRail,
        HWND_TOP,
        view.fullRect.left,
        view.fullRect.top,
        LeftReserveWidth(view),
        RectHeight(view.fullRect),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void PositionRightNumbers(ViewState& view)
{
    if (!view.rightNumbers || !view.scintilla)
        return;

    if (view.rightNumberWidth <= 0 || !::IsWindowVisible(view.scintilla))
    {
        ::ShowWindow(view.rightNumbers, SW_HIDE);
        return;
    }

    RECT client{};
    if (!::GetClientRect(view.scintilla, &client))
        return;

    const int width = std::min(view.rightNumberWidth, RectWidth(client));
    ::SetWindowPos(
        view.rightNumbers,
        HWND_TOP,
        client.right - width,
        client.top,
        width,
        RectHeight(client),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void SetRightMarginInternal(ViewState& view, int value)
{
    view.appliedRightMargin = std::max(0, value);
    SciCall(view.scintilla, SCI_SETMARGINRIGHT, 0, view.appliedRightMargin);
}

static void UpdateRightNumberLayout(ViewState& view)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return;

    const NativeMarginLayout layout = ReadNativeMarginLayout(view);
    const std::uint64_t signature = ComputeNativeMarginLayoutSignature(layout);

    const int expectedWidth = layout.totalWidth + RightDecorationWidth(view);

    if (signature != view.nativeMarginLayoutSignature ||
        expectedWidth != view.rightNumberWidth)
    {
        view.nativeMarginLayoutSignature = signature;
        view.rightNumberWidth = expectedWidth;
        view.mirrorPixels.clear();
        view.mirrorBitmapWidth = 0;
        view.mirrorBitmapHeight = 0;
        SetRightMarginInternal(view, view.baseRightMargin + view.rightNumberWidth);
        PositionRightNumbers(view);
        CaptureMirroredMargins(view, true);
        return;
    }

    const int currentRightMargin = static_cast<int>(
        SciCall(view.scintilla, SCI_GETMARGINRIGHT));

    if (currentRightMargin != view.appliedRightMargin)
    {
        // Conserve une véritable modification de la marge droite effectuée par Notepad++ ou un plugin, puis ajoute
        // une seule fois les marges natives reproduites en miroir. Ce chemin n’est exécuté que lorsque la valeur externe
        // diffère de la dernière valeur appliquée par ce plugin.
        view.baseRightMargin = std::max(0, currentRightMargin);
        SetRightMarginInternal(view, view.baseRightMargin + view.rightNumberWidth);
        PositionRightNumbers(view);
        CaptureMirroredMargins(view, true);
    }
}

static void ApplyNormalMarginCursors(ViewState& view, bool initial)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return;

    const int count = std::clamp(
        static_cast<int>(SciCall(view.scintilla, SCI_GETMARGINS)),
        0,
        kMaxRememberedMargins);

    if (initial)
    {
        view.marginCount = count;
        for (int margin = 0; margin < count; ++margin)
        {
            view.originalMarginCursors[margin] = static_cast<int>(
                SciCall(view.scintilla, SCI_GETMARGINCURSORN, margin));
            SciCall(view.scintilla, SCI_SETMARGINCURSORN, margin, SC_CURSORARROW);
        }
        return;
    }

    view.marginCount = std::max(view.marginCount, count);
    for (int margin = 0; margin < count; ++margin)
    {
        const int current = static_cast<int>(
            SciCall(view.scintilla, SCI_GETMARGINCURSORN, margin));
        if (current != SC_CURSORARROW)
        {
            // Mémorise toute modification ultérieure des préférences de Notepad++ afin de pouvoir la restaurer.
            view.originalMarginCursors[margin] = current;
            SciCall(view.scintilla, SCI_SETMARGINCURSORN, margin, SC_CURSORARROW);
        }
    }
}

static void RestoreMarginCursors(ViewState& view)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return;

    const int count = std::clamp(view.marginCount, 0, kMaxRememberedMargins);
    for (int margin = 0; margin < count; ++margin)
    {
        SciCall(
            view.scintilla,
            SCI_SETMARGINCURSORN,
            margin,
            view.originalMarginCursors[margin]);
    }
}

static void ApplyScrollInfoIfChanged(ViewState& view, const SCROLLINFO& info)
{
    if (view.lastScrollMin == info.nMin &&
        view.lastScrollMax == info.nMax &&
        view.lastScrollPage == info.nPage &&
        view.lastScrollPos == info.nPos)
    {
        return;
    }

    SCROLLINFO target = info;
    target.cbSize = sizeof(target);
    target.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    ::SetScrollInfo(view.leftScrollbar, SB_CTL, &target, TRUE);

    view.lastScrollMin = info.nMin;
    view.lastScrollMax = info.nMax;
    view.lastScrollPage = info.nPage;
    view.lastScrollPos = info.nPos;
}

static void SyncScrollbarFromScintilla(ViewState& view)
{
    if (!view.scintilla || !view.leftScrollbar || !::IsWindow(view.leftScrollbar))
        return;

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;

    if (::GetScrollInfo(view.scintilla, SB_VERT, &info))
    {
        ApplyScrollInfoIfChanged(view, info);
        return;
    }

    const int firstVisible = static_cast<int>(SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int lineCount = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT)));
    const int linesOnScreen = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_LINESONSCREEN)));

    info.nMin = 0;
    info.nMax = lineCount - 1;
    info.nPage = static_cast<UINT>(linesOnScreen);
    info.nPos = firstVisible;
    ApplyScrollInfoIfChanged(view, info);
}

static int MaxScrollPosition(const SCROLLINFO& info)
{
    const int pageAdjustment = info.nPage > 0
        ? static_cast<int>(std::min<UINT>(info.nPage - 1, static_cast<UINT>(INT_MAX)))
        : 0;
    return std::max(info.nMin, info.nMax - pageAdjustment);
}

static void ScrollScintillaFromLeftBar(ViewState& view, WPARAM wParam)
{
    if (!view.scintilla || !view.leftScrollbar)
        return;

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    if (!::GetScrollInfo(view.leftScrollbar, SB_CTL, &info))
        return;

    const int request = LOWORD(wParam);
    const int current = static_cast<int>(SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int page = std::max(1, static_cast<int>(info.nPage));
    const int maximum = MaxScrollPosition(info);
    int target = current;

    switch (request)
    {
        case SB_LINEUP: target = current - 1; break;
        case SB_LINEDOWN: target = current + 1; break;
        case SB_PAGEUP: target = current - page; break;
        case SB_PAGEDOWN: target = current + page; break;
        case SB_TOP: target = info.nMin; break;
        case SB_BOTTOM: target = maximum; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            target = info.nTrackPos;
            break;
        case SB_ENDSCROLL:
        default:
            return;
    }

    target = std::clamp(target, info.nMin, maximum);
    SciCall(view.scintilla, SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(target));
    SyncScrollbarFromScintilla(view);

    if (view.rightNumbers)
        ::InvalidateRect(view.rightNumbers, nullptr, FALSE);
}

static int DisplayLineFromRightY(const ViewState& view, int y)
{
    const int firstVisible = static_cast<int>(
        SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    return firstVisible + std::max(0, y) / std::max(1, view.lineHeight);
}

static int DocumentLineFromRightY(const ViewState& view, int y)
{
    const int displayLine = DisplayLineFromRightY(view, y);
    const int documentLine = static_cast<int>(
        SciCall(view.scintilla, SCI_DOCLINEFROMVISIBLE, displayLine));
    const int lineCount = static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT));
    return (documentLine >= 0 && documentLine < lineCount) ? documentLine : -1;
}

static void SelectWholeLineRange(ViewState& view, int anchorLine, int currentLine)
{
    if (anchorLine < 0 || currentLine < 0)
        return;

    const int lineCount = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT)));
    anchorLine = std::clamp(anchorLine, 0, lineCount - 1);
    currentLine = std::clamp(currentLine, 0, lineCount - 1);

    const int firstLine = std::min(anchorLine, currentLine);
    const int lastLine = std::max(anchorLine, currentLine);

    const LRESULT start = SciCall(view.scintilla, SCI_POSITIONFROMLINE, firstLine);
    LRESULT end = 0;
    if (lastLine + 1 < lineCount)
        end = SciCall(view.scintilla, SCI_POSITIONFROMLINE, lastLine + 1);
    else
        end = SciCall(view.scintilla, SCI_GETLINEENDPOSITION, lastLine);

    ::SetFocus(view.scintilla);
    SciCall(view.scintilla, SCI_SETSEL, static_cast<WPARAM>(start), end);
    SciCall(view.scintilla, SCI_SCROLLCARET);
}

static void PaintRightNumbers(ViewState& view, HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd, &paint);
    if (!dc)
        return;

    RECT client{};
    ::GetClientRect(hwnd, &client);

    HBRUSH background = ::CreateSolidBrush(view.lineNumberBack);
    if (background)
    {
        ::FillRect(dc, &client, background);
        ::DeleteObject(background);
    }

    const int markerWidth = std::clamp(view.bookmarkAreaWidth, 0, RectWidth(client));

    // Copie pixel par pixel la véritable marge des signets de Notepad++ / Scintilla.
    // Cela conserve exactement l’apparence, les couleurs et le thème des marqueurs natifs, au lieu
    // de dessiner une approximation séparée qui semblerait ajoutée artificiellement.
    if (markerWidth > 0 && view.bookmarkMarginIndex >= 0)
    {
        if (HDC source = ::GetDC(view.scintilla))
        {
            ::BitBlt(
                dc,
                client.left,
                client.top,
                markerWidth,
                RectHeight(client),
                source,
                view.bookmarkMarginX,
                client.top,
                SRCCOPY);
            ::ReleaseDC(view.scintilla, source);
        }
    }

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, view.lineNumberFore);
    HFONT oldFont = nullptr;
    if (view.lineNumberFont)
        oldFont = static_cast<HFONT>(::SelectObject(dc, view.lineNumberFont));

    const int firstVisible = static_cast<int>(
        SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int lineCount = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT)));
    const int lineHeight = std::max(1, view.lineHeight);
    const int rows = RectHeight(client) / lineHeight + 2;

    for (int row = 0; row < rows; ++row)
    {
        const int displayLine = firstVisible + row;
        const int documentLine = static_cast<int>(
            SciCall(view.scintilla, SCI_DOCLINEFROMVISIBLE, displayLine));
        if (documentLine < 0 || documentLine >= lineCount)
            continue;

        const int firstDisplayForDocument = static_cast<int>(
            SciCall(view.scintilla, SCI_VISIBLEFROMDOCLINE, documentLine));
        if (firstDisplayForDocument != displayLine)
            continue; // Suite d’une ligne repliée : les marges natives sont vides ici.

        wchar_t number[32] = {};
        ::_snwprintf_s(number, _countof(number), _TRUNCATE, L"%d", documentLine + 1);

        RECT numberRect{
            client.left + markerWidth,
            row * lineHeight,
            client.right,
            (row + 1) * lineHeight
        };
        ::DrawTextW(
            dc,
            number,
            -1,
            &numberRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    }

    if (oldFont)
        ::SelectObject(dc, oldFont);
    ::EndPaint(hwnd, &paint);
}

static std::uint64_t ComputeVisibleBookmarkSignature(const ViewState& view)
{
    RECT client{};
    if (!view.rightNumbers || !::GetClientRect(view.rightNumbers, &client))
        return 0;

    const int firstVisible = static_cast<int>(
        SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int lineCount = std::max(1, static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT)));
    const int rows = RectHeight(client) / std::max(1, view.lineHeight) + 2;

    std::uint64_t hash = 1469598103934665603ULL;
    for (int row = 0; row < rows; ++row)
    {
        const int displayLine = firstVisible + row;
        const int documentLine = static_cast<int>(
            SciCall(view.scintilla, SCI_DOCLINEFROMVISIBLE, displayLine));
        if (documentLine < 0 || documentLine >= lineCount)
            continue;

        if (LineHasBookmark(view, documentLine))
        {
            hash ^= static_cast<std::uint64_t>(documentLine + 1);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static void UpdateVisibleBookmarksIfNeeded(ViewState& view, bool force)
{
    if (!view.rightNumbers || !::IsWindow(view.rightNumbers))
        return;

    const std::uint64_t signature = ComputeVisibleBookmarkSignature(view);
    if (force || signature != view.lastBookmarkSignature)
    {
        view.lastBookmarkSignature = signature;
        ::InvalidateRect(view.rightNumbers, nullptr, FALSE);
    }
}

static void ToggleBookmarkAtLine(ViewState& view, int line)
{
    if (line < 0 || g_bookmarkMarkerId < 0 || g_bookmarkMarkerId >= 32)
        return;

    if (LineHasBookmark(view, line))
        SciCall(view.scintilla, SCI_MARKERDELETE, static_cast<WPARAM>(line), g_bookmarkMarkerId);
    else
        SciCall(view.scintilla, SCI_MARKERADD, static_cast<WPARAM>(line), g_bookmarkMarkerId);

    view.lastBookmarkSignature = ~std::uint64_t{0};
    ::InvalidateRect(view.rightNumbers, nullptr, FALSE);
    ::SetFocus(view.scintilla);
}

static void UpdateRightNumbersIfNeeded(ViewState& view, bool force)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return;

    const int firstVisible = static_cast<int>(
        SciCall(view.scintilla, SCI_GETFIRSTVISIBLELINE));
    const int lineCount = static_cast<int>(SciCall(view.scintilla, SCI_GETLINECOUNT));
    const int lineHeight = view.lineHeight;
    const int rightWidth = view.rightNumberWidth;

    if (force ||
        firstVisible != view.lastFirstVisible ||
        lineCount != view.lastLineCount ||
        lineHeight != view.lastLineHeight ||
        rightWidth != view.lastRightWidth)
    {
        view.lastFirstVisible = firstVisible;
        view.lastLineCount = lineCount;
        view.lastLineHeight = lineHeight;
        view.lastRightWidth = rightWidth;
        MarkMirrorDirty(view);
        if (force)
            RefreshMirrorIfReady(view, true);
    }
}

static void PositionChildControls(ViewState& view)
{
    PositionLeftRail(view);
    PositionRightNumbers(view);
}

static void UpdateDpiSizes(ViewState& view)
{
    const int newScrollbarWidth = GetScrollbarWidthForWindow(view.scintilla);
    const int newGapWidth = ScaleDip(view.scintilla, kLeftGapDip);
    const int newSeparatorWidth = std::max(
        ScaleDip(view.scintilla, kRightSeparatorDip),
        GetLeftNonClientBorderWidth(view.scintilla));
    const COLORREF newSeparatorColor = g_haveSharedSeparatorColor
        ? g_sharedSeparatorColor
        : ReadSharedSeparatorColor();

    if (newScrollbarWidth == view.scrollbarWidth &&
        newGapWidth == view.gapWidth &&
        newSeparatorWidth == view.rightSeparatorWidth &&
        newSeparatorColor == view.rightSeparatorColor)
    {
        return;
    }

    view.scrollbarWidth = newScrollbarWidth;
    view.gapWidth = newGapWidth;
    view.rightSeparatorWidth = newSeparatorWidth;
    view.rightSeparatorColor = newSeparatorColor;

    if (view.haveFullRect && ::IsWindow(view.scintilla))
    {
        const int reserve = LeftReserveWidth(view);
        view.internalLayout = true;
        ::SetWindowPos(
            view.scintilla,
            nullptr,
            view.fullRect.left + reserve,
            view.fullRect.top,
            std::max(0, RectWidth(view.fullRect) - reserve),
            RectHeight(view.fullRect),
            SWP_NOZORDER | SWP_NOACTIVATE);
        view.internalLayout = false;
    }

    UpdateRightNumberLayout(view);
    MarkMirrorDirty(view);
    PositionChildControls(view);
}

static void RememberAndAdjustExternalWindowPos(ViewState& view, WINDOWPOS& position)
{
    if (view.internalLayout || !view.haveFullRect)
        return;

    RECT currentActual{};
    GetWindowRectInParent(view.scintilla, view.host, currentActual);

    RECT requested = view.fullRect;
    const int reserve = LeftReserveWidth(view);

    if (!(position.flags & SWP_NOMOVE))
    {
        // Certaines passes de mise en page répètent des coordonnées déjà ajustées. Elles sont considérées
        // comme une « absence de déplacement logique » afin que la largeur réservée n’augmente jamais.
        if (!(position.x == currentActual.left &&
              currentActual.left == view.fullRect.left + reserve))
        {
            const int width = RectWidth(requested);
            requested.left = position.x;
            requested.right = requested.left + width;
        }

        const int height = RectHeight(requested);
        requested.top = position.y;
        requested.bottom = requested.top + height;
    }

    if (!(position.flags & SWP_NOSIZE))
    {
        int requestedWidth = position.cx;
        const int currentActualWidth = RectWidth(currentActual);
        const int storedAdjustedWidth = std::max(0, RectWidth(view.fullRect) - reserve);
        if (requestedWidth == currentActualWidth && currentActualWidth == storedAdjustedWidth)
            requestedWidth = RectWidth(view.fullRect);

        requested.right = requested.left + std::max(0, requestedWidth);
        requested.bottom = requested.top + std::max(0, position.cy);
    }

    view.fullRect = requested;

    if (!(position.flags & SWP_NOMOVE))
    {
        position.x = requested.left + reserve;
        position.y = requested.top;
    }
    if (!(position.flags & SWP_NOSIZE))
    {
        position.cx = std::max(0, RectWidth(requested) - reserve);
        position.cy = RectHeight(requested);
    }
}

static bool RegisterPluginWindowClasses()
{
    WNDCLASSEXW rail{};
    rail.cbSize = sizeof(rail);
    rail.hInstance = g_instance;
    rail.lpfnWndProc = LeftRailWndProc;
    rail.lpszClassName = kLeftRailClass;
    rail.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    rail.style = CS_DBLCLKS;

    if (!::RegisterClassExW(&rail) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    WNDCLASSEXW numbers{};
    numbers.cbSize = sizeof(numbers);
    numbers.hInstance = g_instance;
    numbers.lpfnWndProc = RightNumbersWndProc;
    numbers.lpszClassName = kRightNumbersClass;
    numbers.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    numbers.style = CS_DBLCLKS;

    if (!::RegisterClassExW(&numbers) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    return true;
}

static bool AttachView(ViewState& view, HWND scintilla)
{
    if (!scintilla || !::IsWindow(scintilla))
        return false;

    view = {};
    view.scintilla = scintilla;
    view.host = ::GetParent(scintilla);
    if (!view.host || !::IsWindow(view.host))
    {
        view = {};
        return false;
    }

    if (!GetWindowRectInParent(scintilla, view.host, view.fullRect))
    {
        view = {};
        return false;
    }
    view.haveFullRect = true;

    view.scrollbarWidth = GetScrollbarWidthForWindow(scintilla);
    view.gapWidth = ScaleDip(scintilla, kLeftGapDip);
    view.rightSeparatorWidth = std::max(
        ScaleDip(scintilla, kRightSeparatorDip),
        GetLeftNonClientBorderWidth(scintilla));
    if (!g_haveSharedSeparatorColor)
    {
        g_sharedSeparatorColor = ReadSharedSeparatorColor();
        g_haveSharedSeparatorColor = true;
    }
    view.rightSeparatorColor = g_sharedSeparatorColor;
    view.baseRightMargin = std::max(0, static_cast<int>(SciCall(scintilla, SCI_GETMARGINRIGHT)));
    const NativeMarginLayout nativeMargins = ReadNativeMarginLayout(view);
    view.rightNumberWidth = nativeMargins.totalWidth + RightDecorationWidth(view);
    view.nativeMarginLayoutSignature =
        ComputeNativeMarginLayoutSignature(nativeMargins);

    view.leftRail = ::CreateWindowExW(
        0,
        kLeftRailClass,
        nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        view.fullRect.left,
        view.fullRect.top,
        LeftReserveWidth(view),
        RectHeight(view.fullRect),
        view.host,
        nullptr,
        g_instance,
        &view);
    if (!view.leftRail)
    {
        view = {};
        return false;
    }

    view.leftScrollbar = ::CreateWindowExW(
        0,
        L"SCROLLBAR",
        nullptr,
        WS_CHILD | WS_VISIBLE | SBS_VERT,
        0,
        0,
        view.scrollbarWidth,
        RectHeight(view.fullRect),
        view.leftRail,
        nullptr,
        g_instance,
        nullptr);
    if (!view.leftScrollbar)
    {
        ::DestroyWindow(view.leftRail);
        view = {};
        return false;
    }

    view.rightNumbers = ::CreateWindowExW(
        0,
        kRightNumbersClass,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        view.rightNumberWidth,
        0,
        scintilla,
        nullptr,
        g_instance,
        &view);
    if (!view.rightNumbers)
    {
        ::DestroyWindow(view.leftRail);
        view = {};
        return false;
    }

    view.scrollbarSubclassed = !!::SetWindowSubclass(
        view.leftScrollbar,
        ScrollbarSubclassProc,
        kScrollbarSubclassId,
        reinterpret_cast<DWORD_PTR>(&view));

    view.scintillaSubclassed = !!::SetWindowSubclass(
        scintilla,
        ScintillaSubclassProc,
        kScintillaSubclassId,
        reinterpret_cast<DWORD_PTR>(&view));

    if (!view.scrollbarSubclassed || !view.scintillaSubclassed)
    {
        if (view.scintillaSubclassed)
            ::RemoveWindowSubclass(scintilla, ScintillaSubclassProc, kScintillaSubclassId);
        if (view.scrollbarSubclassed)
            ::RemoveWindowSubclass(view.leftScrollbar, ScrollbarSubclassProc, kScrollbarSubclassId);
        if (view.rightNumbers && ::IsWindow(view.rightNumbers))
            ::DestroyWindow(view.rightNumbers);
        ::DestroyWindow(view.leftRail);
        view = {};
        return false;
    }

    // Empêche Scintilla de dessiner par-dessus la fenêtre enfant reproduite en miroir. Sans
    // ce style, les rafraîchissements rapides de la fenêtre parente, notamment lorsque Ctrl est maintenu, peuvent apparaître
    // à travers la fenêtre enfant et provoquer un scintillement visible.
    view.originalScintillaStyle = ::GetWindowLongPtrW(scintilla, GWL_STYLE);
    if ((view.originalScintillaStyle & WS_CLIPCHILDREN) == 0)
    {
        ::SetWindowLongPtrW(
            scintilla,
            GWL_STYLE,
            view.originalScintillaStyle | WS_CLIPCHILDREN);
        view.addedClipChildren = true;
    }

    ApplyNormalMarginCursors(view, true);
    UpdateLineNumberVisuals(view, true);
    SetRightMarginInternal(view, view.baseRightMargin + view.rightNumberWidth);

    const int reserve = LeftReserveWidth(view);
    view.internalLayout = true;
    ::SetWindowPos(
        scintilla,
        nullptr,
        view.fullRect.left + reserve,
        view.fullRect.top,
        std::max(0, RectWidth(view.fullRect) - reserve),
        RectHeight(view.fullRect),
        SWP_NOZORDER | SWP_NOACTIVATE);
    view.internalLayout = false;

    PositionChildControls(view);
    ApplyScrollbarTheme(view);
    SyncScrollbarFromScintilla(view);
    CaptureMirroredMargins(view, true);
    return true;
}

static void DetachView(ViewState& view)
{
    if (!view.scintilla)
        return;

    const RECT restoreRect = view.fullRect;
    const bool canRestore = view.haveFullRect && view.host && ::IsWindow(view.host);

    if (::IsWindow(view.scintilla))
    {
        RestoreMarginCursors(view);
        SciCall(view.scintilla, SCI_SETMARGINRIGHT, 0, std::max(0, view.baseRightMargin));

        if (view.scintillaSubclassed)
            ::RemoveWindowSubclass(view.scintilla, ScintillaSubclassProc, kScintillaSubclassId);

        if (view.addedClipChildren)
        {
            ::SetWindowLongPtrW(
                view.scintilla,
                GWL_STYLE,
                view.originalScintillaStyle);
        }
    }

    if (view.leftScrollbar && ::IsWindow(view.leftScrollbar) && view.scrollbarSubclassed)
    {
        ::RemoveWindowSubclass(
            view.leftScrollbar,
            ScrollbarSubclassProc,
            kScrollbarSubclassId);
    }

    if (view.rightNumbers && ::IsWindow(view.rightNumbers))
        ::DestroyWindow(view.rightNumbers);
    if (view.leftRail && ::IsWindow(view.leftRail))
        ::DestroyWindow(view.leftRail); // Détruit également la barre de défilement enfant.

    if (view.lineNumberFont)
    {
        ::DeleteObject(view.lineNumberFont);
        view.lineNumberFont = nullptr;
    }

    if (canRestore && ::IsWindow(view.scintilla))
    {
        ::SetWindowPos(
            view.scintilla,
            nullptr,
            restoreRect.left,
            restoreRect.top,
            RectWidth(restoreRect),
            RectHeight(restoreRect),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    view = {};
}

static void UpdateMenuCheck()
{
    if (!g_npp._nppHandle || g_funcItems[0]._cmdID == 0)
        return;

    ::SendMessageW(
        g_npp._nppHandle,
        NPPM_SETMENUITEMCHECK,
        static_cast<WPARAM>(g_funcItems[0]._cmdID),
        static_cast<LPARAM>(g_enabled ? TRUE : FALSE));
}

static void EnablePlugin()
{
    if (g_started)
        return;

    g_cleaned = false;
    g_sharedSeparatorColor = ReadSharedSeparatorColor();
    g_haveSharedSeparatorColor = true;
    AttachView(g_views[0], g_npp._scintillaMainHandle);
    AttachView(g_views[1], g_npp._scintillaSecondHandle);

    g_timerId = ::SetTimer(nullptr, 0, kSyncIntervalMs, SyncTimerProc);
    g_started = true;
    g_enabled = true;
    UpdateMenuCheck();
}

static void DisablePlugin()
{
    if (!g_started)
    {
        g_enabled = false;
        UpdateMenuCheck();
        return;
    }

    if (g_timerId)
    {
        ::KillTimer(nullptr, g_timerId);
        g_timerId = 0;
    }

    for (auto& view : g_views)
        DetachView(view);

    g_started = false;
    g_enabled = false;
    g_lastDarkMode = -1;
    g_haveSharedSeparatorColor = false;
    UpdateMenuCheck();
}

static void CleanupPlugin()
{
    if (g_cleaned)
        return;

    DisablePlugin();
    g_cleaned = true;
}

static void TogglePlugin()
{
    if (g_started)
        DisablePlugin();
    else
        EnablePlugin();
}

static void CALLBACK SyncTimerProc(HWND, UINT, UINT_PTR, DWORD)
{
    if (!g_started)
        return;

    ApplyThemeIfNeeded();
    ++g_visualRefreshTick;

    for (auto& view : g_views)
    {
        if (!view.scintilla || !::IsWindow(view.scintilla))
            continue;

        SyncScrollbarFromScintilla(view);
        UpdateRightNumberLayout(view);
        UpdateRightNumbersIfNeeded(view, false);

        // Les styles et la configuration des marges changent bien moins souvent que la position
        // de défilement. Ils sont donc vérifiés à faible fréquence, puis capturés après un court délai d’attente.
        if ((g_visualRefreshTick % 10U) == 0U)
        {
            UpdateLineNumberVisuals(view, false);
            ApplyNormalMarginCursors(view, false);
        }

        RefreshMirrorIfReady(view, false);
    }
}

static LRESULT CALLBACK ScrollbarSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR)
{
    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
    {
        if (HCURSOR cursor = ::LoadCursorW(nullptr, IDC_ARROW))
        {
            ::SetCursor(cursor);
            return TRUE;
        }
    }

    return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK LeftRailWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* view = reinterpret_cast<ViewState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        view = reinterpret_cast<ViewState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }

    if (view && message == WM_VSCROLL && reinterpret_cast<HWND>(lParam) == view->leftScrollbar)
    {
        ScrollScintillaFromLeftBar(*view, wParam);
        return 0;
    }

    if (view && message == WM_SIZE && view->leftScrollbar)
    {
        RECT client{};
        ::GetClientRect(hwnd, &client);

        const int clientWidth = RectWidth(client);
        const int clientHeight = RectHeight(client);
        int borderThickness = std::max(1, view->rightSeparatorWidth);
        borderThickness = std::min(borderThickness, clientWidth);
        borderThickness = std::min(borderThickness, std::max(0, clientHeight / 2));

        const int availableForScrollbar = std::max(
            0,
            clientWidth - borderThickness);
        const int availableHeight = std::max(
            0,
            clientHeight - (borderThickness * 2));

        ::SetWindowPos(
            view->leftScrollbar,
            nullptr,
            borderThickness,
            borderThickness,
            std::min(view->scrollbarWidth, availableForScrollbar),
            availableHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return 0;
    }

    if (view && (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL))
        return ::SendMessageW(view->scintilla, message, wParam, lParam);

    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
    {
        if (HCURSOR cursor = ::LoadCursorW(nullptr, IDC_ARROW))
        {
            ::SetCursor(cursor);
            return TRUE;
        }
    }

    if (message == WM_ERASEBKGND)
        return TRUE;

    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        if (dc)
        {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            const COLORREF backgroundColor = view
                ? view->lineNumberBack
                : ::GetSysColor(COLOR_WINDOW);
            HBRUSH backgroundBrush = ::CreateSolidBrush(backgroundColor);
            if (backgroundBrush)
            {
                ::FillRect(dc, &client, backgroundBrush);
                ::DeleteObject(backgroundBrush);
            }

            // Ferme la bande ajoutée à gauche avec la même bordure grise sur
            // le bord extérieur, le bord supérieur et le bord inférieur.
            if (view)
            {
                const int clientWidth = RectWidth(client);
                const int clientHeight = RectHeight(client);
                int borderThickness = std::max(1, view->rightSeparatorWidth);
                borderThickness = std::min(borderThickness, clientWidth);
                borderThickness = std::min(
                    borderThickness,
                    std::max(0, clientHeight / 2));

                if (borderThickness > 0)
                {
                    const RECT leftBorder{
                        client.left,
                        client.top,
                        client.left + borderThickness,
                        client.bottom
                    };
                    const RECT topBorder{
                        client.left,
                        client.top,
                        client.right,
                        client.top + borderThickness
                    };
                    const RECT bottomBorder{
                        client.left,
                        client.bottom - borderThickness,
                        client.right,
                        client.bottom
                    };

                    HBRUSH borderBrush = ::CreateSolidBrush(view->rightSeparatorColor);
                    if (borderBrush)
                    {
                        ::FillRect(dc, &leftBorder, borderBrush);
                        ::FillRect(dc, &topBorder, borderBrush);
                        ::FillRect(dc, &bottomBorder, borderBrush);
                        ::DeleteObject(borderBrush);
                    }
                }
            }
            ::EndPaint(hwnd, &paint);
        }
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

static LRESULT HandleRightMarginWheel(
    ViewState& view,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!view.scintilla || !::IsWindow(view.scintilla))
        return 0;

    ::SetFocus(view.scintilla);

    if (message == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL))
    {
        // Conserve le défilement horizontal natif et le zoom avec Ctrl + molette. La
        // protection empêche la sous-classe Scintilla de rediriger ce même message
        // vers le gestionnaire des marges reproduites en miroir.
        view.forwardingWheelToScintilla = true;
        const LRESULT result = ::SendMessageW(view.scintilla, message, wParam, lParam);
        view.forwardingWheelToScintilla = false;
        MarkMirrorDirty(view);
        return result;
    }

    UINT wheelLines = 3;
    ::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    view.wheelDeltaRemainder += delta;

    int notches = 0;
    while (view.wheelDeltaRemainder >= WHEEL_DELTA)
    {
        ++notches;
        view.wheelDeltaRemainder -= WHEEL_DELTA;
    }
    while (view.wheelDeltaRemainder <= -WHEEL_DELTA)
    {
        --notches;
        view.wheelDeltaRemainder += WHEEL_DELTA;
    }

    if (notches == 0)
        return 0;

    int linesPerNotch = 0;
    if (wheelLines == WHEEL_PAGESCROLL)
    {
        linesPerNotch = std::max(1, static_cast<int>(
            SciCall(view.scintilla, SCI_LINESONSCREEN)));
    }
    else
    {
        linesPerNotch = std::max(1, static_cast<int>(wheelLines));
    }

    // Une valeur positive de la molette fait défiler vers le haut, d’où le déplacement de ligne négatif.
    SciCall(
        view.scintilla,
        SCI_LINESCROLL,
        0,
        static_cast<LPARAM>(-notches * linesPerNotch));

    SyncScrollbarFromScintilla(view);
    MarkMirrorDirty(view);
    return 0;
}

static void ForwardMappedMouseMessage(
    ViewState& view,
    UINT message,
    WPARAM wParam,
    int childX,
    int childY)
{
    RECT client{};
    if (!::GetClientRect(view.rightNumbers, &client) || RectWidth(client) <= 0)
        return;

    childX = std::clamp(childX, 0, RectWidth(client) - 1);
    const MappedMarginPoint mapped = MapRightChildXToNativeMargin(view, childX);
    if (mapped.nativeX < 0)
        return;

    ::SendMessageW(
        view.scintilla,
        message,
        wParam,
        MAKELPARAM(
            static_cast<WORD>(mapped.nativeX),
            static_cast<WORD>(childY)));
}

static LRESULT CALLBACK RightNumbersWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* view = reinterpret_cast<ViewState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        view = reinterpret_cast<ViewState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }

    if (!view)
        return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message)
    {
        case kMirrorRefreshMessage:
            MarkMirrorDirty(*view);
            return 0;

        case WM_PAINT:
            PaintMirroredMarginsWindow(*view, hwnd);
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                if (HCURSOR cursor = ::LoadCursorW(nullptr, IDC_ARROW))
                {
                    ::SetCursor(cursor);
                    return TRUE;
                }
            }
            break;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return HandleRightMarginWheel(*view, message, wParam, lParam);

        case WM_LBUTTONDOWN:
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const MappedMarginPoint mapped = MapRightChildXToNativeMargin(*view, x);
            if (mapped.marginIndex < 0)
                return 0;

            ::SetFocus(view->scintilla);

            if (mapped.marginIndex == kLineNumberMargin)
            {
                const int line = DocumentLineFromRightY(*view, y);
                if (line >= 0)
                {
                    view->selectionAnchorLine = line;
                    view->selectingFromRight = true;
                    ::SetCapture(hwnd);
                    SelectWholeLineRange(*view, line, line);
                }
            }
            else
            {
                view->rightMarginMouseGesture = true;
                view->rightGestureMargin = mapped.marginIndex;
                ForwardMappedMouseMessage(*view, message, wParam, x, y);
                ::SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (view->selectingFromRight && (wParam & MK_LBUTTON))
            {
                const int line = DocumentLineFromRightY(*view, y);
                if (line >= 0)
                    SelectWholeLineRange(*view, view->selectionAnchorLine, line);
                return 0;
            }

            if (view->rightMarginMouseGesture)
            {
                ForwardMappedMouseMessage(*view, message, wParam, x, y);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (view->selectingFromRight)
            {
                const int line = DocumentLineFromRightY(*view, y);
                if (line >= 0)
                    SelectWholeLineRange(*view, view->selectionAnchorLine, line);
                view->selectingFromRight = false;
                view->selectionAnchorLine = -1;
            }
            else if (view->rightMarginMouseGesture)
            {
                ForwardMappedMouseMessage(*view, message, wParam, x, y);
            }

            view->rightMarginMouseGesture = false;
            view->rightGestureMargin = -1;
            if (::GetCapture() == hwnd)
                ::ReleaseCapture();

            MarkMirrorDirty(*view);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            ForwardMappedMouseMessage(
                *view,
                message,
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
            ForwardMappedMouseMessage(
                *view,
                message,
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;

        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            view->selectingFromRight = false;
            view->selectionAnchorLine = -1;
            view->rightMarginMouseGesture = false;
            view->rightGestureMargin = -1;
            break;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

static bool WheelPointIsOverRightMargins(const ViewState& view, LPARAM lParam)
{
    if (!view.rightNumbers || !::IsWindowVisible(view.rightNumbers))
        return false;

    RECT screenRect{};
    if (!::GetWindowRect(view.rightNumbers, &screenRect))
        return false;

    const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    return ::PtInRect(&screenRect, point) != FALSE;
}

static LRESULT CALLBACK ScintillaSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR refData)
{
    auto* view = reinterpret_cast<ViewState*>(refData);

    // Selon le réglage de la molette dans Windows, le message peut être envoyé
    // soit à la fenêtre enfant survolée, soit directement à la vue Scintilla active.
    // Les deux chemins sont gérés afin que le défilement sur les marges reproduites en miroir fonctionne toujours.
    if (view &&
        (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) &&
        !view->forwardingWheelToScintilla &&
        WheelPointIsOverRightMargins(*view, lParam))
    {
        return HandleRightMarginWheel(*view, message, wParam, lParam);
    }

    if (view && message == WM_WINDOWPOSCHANGING && !view->internalLayout)
    {
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (position)
            RememberAndAdjustExternalWindowPos(*view, *position);
    }

    const LRESULT result = ::DefSubclassProc(hwnd, message, wParam, lParam);

    if (!view)
        return result;

    switch (message)
    {
        case kMirrorRefreshMessage:
            MarkMirrorDirty(*view);
            return 0;


        case WM_WINDOWPOSCHANGED:
        case WM_SIZE:
            PositionChildControls(*view);
            SyncScrollbarFromScintilla(*view);
            UpdateRightNumberLayout(*view);
            MarkMirrorDirty(*view);
            RefreshMirrorIfReady(*view, true);
            break;

        case WM_SHOWWINDOW:
            PositionChildControls(*view);
            MarkMirrorDirty(*view);
            RefreshMirrorIfReady(*view, true);
            break;

        case WM_DPICHANGED:
            UpdateDpiSizes(*view);
            UpdateLineNumberVisuals(*view, true);
            UpdateRightNumberLayout(*view);
            MarkMirrorDirty(*view);
            RefreshMirrorIfReady(*view, true);
            break;

        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE:
            RefreshSharedSeparatorColor(true);
            UpdateLineNumberVisuals(*view, true);
            ApplyScrollbarTheme(*view);
            MarkMirrorDirty(*view);
            RefreshMirrorIfReady(*view, true);
            break;

        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            SyncScrollbarFromScintilla(*view);
            UpdateRightNumbersIfNeeded(*view, false);
            MarkMirrorDirty(*view);
            break;

        case SCI_MARKERADD:
        case SCI_MARKERDELETE:
        case SCI_MARKERDELETEALL:
            MarkMirrorDirty(*view);
            break;
    }

    return result;
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        ::DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
    g_npp = data;

    const int bookmarkId = g_npp._nppHandle
        ? static_cast<int>(::SendMessageW(g_npp._nppHandle, NPPM_GETBOOKMARKID, 0, 0))
        : -1;
    if (bookmarkId >= 0 && bookmarkId < 32)
        g_bookmarkMarkerId = bookmarkId;

    ::wcscpy_s(g_funcItems[0]._itemName, kMenuItemSize, kToggleCommand);
    g_funcItems[0]._pFunc = TogglePlugin;
    g_funcItems[0]._init2Check = true;
    g_funcItems[0]._pShKey = nullptr;

    if (RegisterPluginWindowClasses())
        EnablePlugin();
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
    if (count)
        *count = 1;
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotificationMinimal* notification)
{
    if (!notification)
        return;

    if (notification->nmhdr.code == NPPN_SHUTDOWN)
    {
        CleanupPlugin();
        return;
    }

    // Toute notification Scintilla peut affecter une marge native : modification du texte,
    // marqueurs d’état d’enregistrement, signets, pliage, zoom ou mise à jour des lignes visibles.
    for (auto& view : g_views)
    {
        if (notification->nmhdr.hwndFrom == view.scintilla)
        {
            MarkMirrorDirty(view);
            break;
        }
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}
