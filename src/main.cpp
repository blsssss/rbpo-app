/**
 * РБПО — Tray Application
 *
 * Win32 API application with system tray icon support,
 * single-instance enforcement, and background operation.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <shellapi.h>
#include "resource.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const wchar_t APP_MUTEX_NAME[] = L"Local\\RBPO_TrayApp_SingleInstance";
static const wchar_t WINDOW_CLASS[]   = L"RBPOTrayAppClass";
static const wchar_t WINDOW_TITLE[]   = L"РБПО — Tray Application";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HINSTANCE       g_hInstance       = nullptr;
static HWND            g_hWnd            = nullptr;
static NOTIFYICONDATAW g_nid             = {};
static UINT            WM_TASKBARCREATED = 0;
static HANDLE          g_hMutex          = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void AddTrayIcon(HWND hWnd);
static void RemoveTrayIcon();
static void ShowTrayContextMenu(HWND hWnd);
static void ShowMainWindow();
static void HideMainWindow();

// ===========================================================================
// Entry point
// ===========================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int)
{
    // --- Requirement 10: single-instance check via named mutex --------------
    g_hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_hMutex) CloseHandle(g_hMutex);
        return 0;   // exit BEFORE creating the tray icon
    }

    g_hInstance = hInstance;

    // --- Requirement 6: register for taskbar-recreation message -------------
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    // --- Register window class ----------------------------------------------
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hIcon          = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName   = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName  = WINDOW_CLASS;
    wc.hIconSm        = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        CloseHandle(g_hMutex);
        return 1;
    }

    // --- Create the main window (initially hidden) --------------------------
    g_hWnd = CreateWindowExW(
        0, WINDOW_CLASS, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 400,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd) {
        CloseHandle(g_hMutex);
        return 1;
    }

    // --- Requirement 1: add tray icon at startup ----------------------------
    AddTrayIcon(g_hWnd);

    // --- Requirement 7: support hidden launch (--silent flag) ---------------
    bool startSilent = pCmdLine && wcsstr(pCmdLine, L"--silent");
    if (!startSilent) {
        ShowMainWindow();
    }

    // --- Message loop -------------------------------------------------------
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // --- Cleanup ------------------------------------------------------------
    RemoveTrayIcon();
    ReleaseMutex(g_hMutex);
    CloseHandle(g_hMutex);

    return static_cast<int>(msg.wParam);
}

// ===========================================================================
// Window procedure
// ===========================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // --- Requirement 6: re-add tray icon when taskbar is recreated ----------
    if (message == WM_TASKBARCREATED) {
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (message) {

    // --- Tray icon callback -------------------------------------------------
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            // Requirement 2: left-click on tray icon → show main window
            ShowMainWindow();
            break;
        case WM_RBUTTONUP:
            // Requirement 3: right-click on tray icon → context menu
            ShowTrayContextMenu(hWnd);
            break;
        }
        return 0;

    // --- Menu commands ------------------------------------------------------
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN:
            // Requirement 4: "Открыть" → show main window
            ShowMainWindow();
            break;
        case ID_TRAY_EXIT:
            // Requirement 5: "Выход" in tray menu → exit
            DestroyWindow(hWnd);
            break;
        case ID_FILE_EXIT:
            // Requirement 9: Файл → Выход → exit
            DestroyWindow(hWnd);
            break;
        }
        return 0;

    // --- Requirement 8: closing the window hides it -------------------------
    case WM_CLOSE:
        HideMainWindow();
        return 0;

    // --- Create static label inside the window ------------------------------
    case WM_CREATE: {
        HFONT hFont = CreateFontW(
            20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        HWND hLabel = CreateWindowExW(0, L"STATIC",
            L"Приложение работает в области уведомлений.\r\n"
            L"Закройте это окно — приложение продолжит работу в фоне.",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            0, 0, 0, 0,
            hWnd, nullptr, g_hInstance, nullptr);

        if (hFont && hLabel)
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Store label handle for resizing
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hLabel);
        return 0;
    }

    // --- Keep label centered ------------------------------------------------
    case WM_SIZE: {
        HWND hLabel = (HWND)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hLabel) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(hLabel, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ===========================================================================
// Tray icon helpers
// ===========================================================================

// Requirement 1 & 6: add (or re-add) the notification-area icon
static void AddTrayIcon(HWND hWnd)
{
    g_nid.cbSize            = sizeof(g_nid);
    g_nid.hWnd              = hWnd;
    g_nid.uID               = 1;
    g_nid.uFlags            = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage  = WM_TRAYICON;
    g_nid.hIcon             = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"РБПО Tray Application");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// Requirement 3, 4, 5: tray context menu
static void ShowTrayContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Открыть");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Выход");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

// Requirement 2, 4, 7: show the main window
static void ShowMainWindow()
{
    ShowWindow(g_hWnd, SW_SHOW);
    ShowWindow(g_hWnd, SW_RESTORE);
    SetForegroundWindow(g_hWnd);
}

// Requirement 8: hide the main window (app continues in tray)
static void HideMainWindow()
{
    ShowWindow(g_hWnd, SW_HIDE);
}
