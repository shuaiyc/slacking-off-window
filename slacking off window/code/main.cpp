// ============================================================
//  AutoHideWin - Auto hide window when mouse leaves (Edge hide)
//  Dev-C++ (MinGW/GCC) : -luser32 -lgdi32 -lcomctl32 -lshell32 -mwindows
// ============================================================

#define UNICODE
#define _UNICODE
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>

// ---------------- Control IDs ----------------
#define IDC_LIST        1001
#define IDC_BTN_ADD     1002
#define IDC_BTN_REMOVE  1003
#define IDC_BTN_HIDEALL 1004
#define IDC_STATIC_INFO 1005

// ---------------- Tray ----------------
#define WM_TRAYICON     (WM_USER + 1)
#define ID_TRAY         2001

// ---------------- Timer ----------------
#define TIMER_POLL      3001
#define POLL_MS         120
#define LEAVE_TICKS     3
#define EDGE_ZONE       3

// ---------------- Globals ----------------
HINSTANCE g_hInst = NULL;
HWND      g_hMain = NULL;
HWND      g_hList = NULL;
HWND      g_hInfo = NULL;

struct WatchItem {
    HWND  hwnd;
    RECT  originalRect;
    bool  hidden;
    int   leaveCount;
    std::wstring title;
};
std::vector<WatchItem> g_items;

bool g_inTray = false;
bool  g_waitingForAdd = false;
DWORD g_addStartTick  = 0;

// ---------------- Prototypes ----------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AddCurrentWindow();
void RemoveSelected();
void RefreshList();
void PollAllWindows();
void HideToEdge(WatchItem& it);
void RestoreFromEdge(WatchItem& it);
void ShowTrayIcon(bool show);

// ============================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t* CLS = L"AutoHideWinClass";

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassEx(&wc);

    g_hMain = CreateWindowEx(
        0, CLS, L"AutoHideWin",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 360,
        NULL, NULL, hInstance, NULL);

    if (!g_hMain) return 1;

    ShowWindow(g_hMain, SW_SHOW);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// ============================================================
void CreateControls(HWND hwnd)
{
    g_hInfo = CreateWindowEx(0, L"STATIC",
        L"Tip: click target window, then press [Add Current Window]. "
        L"Move mouse away to hide. Move to right screen edge to restore.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        12, 10, 420, 40,
        hwnd, (HMENU)IDC_STATIC_INFO, g_hInst, NULL);

    g_hList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        12, 55, 420, 200,
        hwnd, (HMENU)IDC_LIST, g_hInst, NULL);

    ListView_SetExtendedListViewStyle(g_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = (LPWSTR)L"Window Title";
    lvc.cx = 380;
    ListView_InsertColumn(g_hList, 0, &lvc);

    CreateWindowEx(0, L"BUTTON", L"Add Current Window",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        12, 268, 140, 32,
        hwnd, (HMENU)IDC_BTN_ADD, g_hInst, NULL);

    CreateWindowEx(0, L"BUTTON", L"Remove Selected",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        160, 268, 120, 32,
        hwnd, (HMENU)IDC_BTN_REMOVE, g_hInst, NULL);

    CreateWindowEx(0, L"BUTTON", L"Hide All Now",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        288, 268, 140, 32,
        hwnd, (HMENU)IDC_BTN_HIDEALL, g_hInst, NULL);

    SetTimer(hwnd, TIMER_POLL, POLL_MS, NULL);
}

// ============================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateControls(hwnd);
        ShowTrayIcon(true);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_ADD:     AddCurrentWindow();  break;
        case IDC_BTN_REMOVE:  RemoveSelected();    break;
        case IDC_BTN_HIDEALL:
            for (size_t i = 0; i < g_items.size(); ++i)
                if (!g_items[i].hidden) HideToEdge(g_items[i]);
            break;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_POLL) PollAllWindows();
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenu(menu, MF_STRING, 1, L"Show Panel");
            AppendMenu(menu, MF_STRING, 2, L"Hide All");
            AppendMenu(menu, MF_SEPARATOR, 0, NULL);
            AppendMenu(menu, MF_STRING, 3, L"Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);

            if (cmd == 1) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                g_inTray = false;
            } else if (cmd == 2) {
                for (size_t i = 0; i < g_items.size(); ++i)
                    if (!g_items[i].hidden) HideToEdge(g_items[i]);
            } else if (cmd == 3) {
                DestroyWindow(hwnd);
            }
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            g_inTray = false;
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        g_inTray = true;
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_POLL);
        ShowTrayIcon(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================
static std::wstring GetWindowTitle(HWND hwnd)
{
    wchar_t buf[256] = {0};
    GetWindowTextW(hwnd, buf, 255);
    return std::wstring(buf);
}

// ============================================================
// 延时抓取：点击后 3 秒抓当前前台窗口
static UINT_PTR g_addTimer = 0;

void CALLBACK AddTimerProc(HWND, UINT, UINT_PTR, DWORD)
{
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hMain) return;

    for (size_t i = 0; i < g_items.size(); ++i) {
        if (g_items[i].hwnd == fg) return; // 已在列表
    }

    WatchItem it;
    it.hwnd = fg;
    it.hidden = false;
    it.leaveCount = 0;
    it.title = GetWindowTitle(fg);
    GetWindowRect(fg, &it.originalRect);
    g_items.push_back(it);
    RefreshList();
}

void AddCurrentWindow()
{
    if (g_waitingForAdd) return;   // 正在等待中，忽略重复点击

    MessageBox(g_hMain,
        L"Click OK, then switch to the target window within 3 seconds.",
        L"Add Window", MB_OK | MB_ICONINFORMATION);

    g_waitingForAdd = true;
    g_addStartTick  = GetTickCount();
}
// ============================================================
void RemoveSelected()
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBox(g_hMain, L"Please select an item first.",
                   L"Info", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_items[sel].hidden && IsWindow(g_items[sel].hwnd))
        RestoreFromEdge(g_items[sel]);

    g_items.erase(g_items.begin() + sel);
    RefreshList();
}

// ============================================================
void RefreshList()
{
    ListView_DeleteAllItems(g_hList);
    for (size_t i = 0; i < g_items.size(); ++i) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;

        std::wstring title = g_items[i].title;
        if (title.empty()) title = L"(No Title)";
        if (g_items[i].hidden) title += L"  [HIDDEN]";

        lvi.pszText = (LPWSTR)title.c_str();
        ListView_InsertItem(g_hList, &lvi);
    }
}

// ============================================================
static bool PtInRectEx(const RECT& rc, POINT pt)
{
    return pt.x >= rc.left && pt.x < rc.right &&
           pt.y >= rc.top  && pt.y < rc.bottom;
}

// ============================================================
void HideToEdge(WatchItem& it)
{
    if (!IsWindow(it.hwnd)) return;

    GetWindowRect(it.hwnd, &it.originalRect);

    int sw = GetSystemMetrics(SM_CXSCREEN);

    SetWindowPos(it.hwnd, NULL,
        sw - 2, it.originalRect.top,
        0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    it.hidden = true;
    RefreshList();
}

// ============================================================
void RestoreFromEdge(WatchItem& it)
{
    if (!IsWindow(it.hwnd)) return;

    SetWindowPos(it.hwnd, NULL,
        it.originalRect.left, it.originalRect.top,
        0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    it.hidden = false;
    RefreshList();
}

// ============================================================
void PollAllWindows()
{
	    // 处理“添加窗口”的倒计时
    if (g_waitingForAdd)
    {
        if (GetTickCount() - g_addStartTick >= 3000)
        {
            g_waitingForAdd = false;   // 先关掉，保证只抓一次

            HWND fg = GetForegroundWindow();
            if (fg && fg != g_hMain)
            {
                // 过滤桌面窗口
                wchar_t cls[64] = {0};
                GetClassNameW(fg, cls, 63);
                if (wcscmp(cls, L"Progman") != 0 &&
                    wcscmp(cls, L"WorkerW") != 0)
                {
                    // 去重：已在列表就不加
                    bool exists = false;
                    for (size_t i = 0; i < g_items.size(); ++i)
                        if (g_items[i].hwnd == fg) { exists = true; break; }

                    if (!exists)
                    {
                        WatchItem it;
                        it.hwnd = fg;
                        it.hidden = false;
                        it.leaveCount = 0;
                        it.title = GetWindowTitle(fg);
                        GetWindowRect(fg, &it.originalRect);
                        g_items.push_back(it);
                        RefreshList();
                    }
                }
            }
        }
    }
    POINT pt;
    GetCursorPos(&pt);

    int sw = GetSystemMetrics(SM_CXSCREEN);

    for (size_t i = 0; i < g_items.size(); ++i)
    {
        WatchItem& it = g_items[i];
        if (!IsWindow(it.hwnd)) continue;

        if (it.hidden) {
            if (pt.x >= sw - EDGE_ZONE) {
                RestoreFromEdge(it);
            }
            continue;
        }

        RECT rc;
        GetWindowRect(it.hwnd, &rc);

        if (PtInRectEx(rc, pt)) {
            it.leaveCount = 0;
        } else {
            it.leaveCount++;
            if (it.leaveCount >= LEAVE_TICKS) {
                HideToEdge(it);
                it.leaveCount = 0;
            }
        }
    }
}

// ============================================================
void ShowTrayIcon(bool show)
{
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hMain;
    nid.uID    = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon  = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(nid.szTip, L"AutoHideWin");

    if (show)
        Shell_NotifyIconW(NIM_ADD, &nid);
    else
        Shell_NotifyIconW(NIM_DELETE, &nid);
}
