// locker.cpp — Windows 10/11 x64
// MSVC:  cl /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE locker.cpp user32.lib gdi32.lib winmm.lib advapi32.lib /Fe:locker.exe
// MinGW: g++ -std=c++17 -O2 -municode -mwindows locker.cpp -o locker.exe -luser32 -lgdi32 -lwinmm -ladvapi32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <cstdio>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

// ============================================================
// CONFIG — правь только здесь
// ============================================================
static const wchar_t* ADMIN_PASSWORD = L"1234";     // вход в админ-настройки
static const wchar_t* EXIT_PASSWORD  = L"5678";     // выход из локера
static const wchar_t* MUSIC_FILE     = L"gazan.mp3"; // файл рядом с exe
static const bool DISABLE_TASKMGR    = true;
static const bool BLOCK_SYSTEM_KEYS  = true;

// ============================================================
static HHOOK  g_kb_hook   = nullptr;
static HBRUSH g_bg_brush  = nullptr;

#define ID_EDIT_ADMIN 1001
#define ID_EDIT_EXIT  1002
#define ID_BTN_ADMIN  1003
#define ID_BTN_EXIT   1004
#define ID_TIMER_TOP  2001
#define ID_STATUS     1005

static void set_taskmgr_policy(bool disable) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD val = disable ? 1 : 0;
        RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&val), sizeof(val));
        RegCloseKey(hk);
    }
}

static LRESULT CALLBACK low_level_kb_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && BLOCK_SYSTEM_KEYS) {
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift= (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) return 1;
        if (down) {
            if (alt && (kb->vkCode == VK_TAB || kb->vkCode == VK_F4)) return 1;
            if (ctrl && kb->vkCode == VK_ESCAPE) return 1;
            if (ctrl && shift && kb->vkCode == VK_ESCAPE) return 1;
            if (alt && kb->vkCode == VK_ESCAPE) return 1;
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

static void start_music() {
    wchar_t cmd[512];
    swprintf_s(cmd, L"open \"%ls\" type mpegvideo alias bgm", MUSIC_FILE);
    if (mciSendStringW(cmd, nullptr, 0, nullptr) == 0) {
        mciSendStringW(L"play bgm repeat", nullptr, 0, nullptr);
    } else {
        PlaySoundW(MUSIC_FILE, nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP);
    }
}
static void stop_music() {
    mciSendStringW(L"stop bgm", nullptr, 0, nullptr);
    mciSendStringW(L"close bgm", nullptr, 0, nullptr);
    PlaySoundW(nullptr, nullptr, 0);
}

static std::wstring get_edit_text(HWND parent, int id) {
    HWND h = GetDlgItem(parent, id);
    int len = GetWindowTextLengthW(h);
    std::wstring buf(len, L'\0');
    GetWindowTextW(h, buf.data(), len + 1);
    return buf;
}
static void set_status(HWND hwnd, const wchar_t* text) {
    SetDlgItemTextW(hwnd, ID_STATUS, text);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bg_brush);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_bg_brush);
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_ADMIN) {
            std::wstring pw = get_edit_text(hwnd, ID_EDIT_ADMIN);
            if (pw == ADMIN_PASSWORD) {
                set_status(hwnd, L"Админ-настройки разблокированы.");
                MessageBoxW(hwnd, L"Доступ к админ-настройкам открыт.", L"OK", MB_OK);
            } else {
                set_status(hwnd, L"Неверный админ-пароль.");
            }
        } else if (id == ID_BTN_EXIT) {
            std::wstring pw = get_edit_text(hwnd, ID_EDIT_EXIT);
            if (pw == EXIT_PASSWORD) {
                DestroyWindow(hwnd);
            } else {
                set_status(hwnd, L"Неверный пароль выхода.");
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == ID_TIMER_TOP) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void init_ui(HWND hwnd) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int cx = sw / 2;

    CreateWindowExW(0, L"STATIC", L"СИСТЕМА ЗАБЛОКИРОВАНА",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, sh / 6, sw, 60, hwnd, nullptr, hi, nullptr);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
        cx - 150, sh / 2 - 60, 300, 30, hwnd, (HMENU)ID_EDIT_ADMIN, hi, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Вход в админ-настройки",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx - 150, sh / 2 - 20, 300, 30, hwnd, (HMENU)ID_BTN_ADMIN, hi, nullptr);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
        cx - 150, sh / 2 + 40, 300, 30, hwnd, (HMENU)ID_EDIT_EXIT, hi, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Выход из локера",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx - 150, sh / 2 + 80, 300, 30, hwnd, (HMENU)ID_BTN_EXIT, hi, nullptr);

    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx - 200, sh / 2 + 140, 400, 30, hwnd, (HMENU)ID_STATUS, hi, nullptr);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    if (DISABLE_TASKMGR) set_taskmgr_policy(true);
    if (BLOCK_SYSTEM_KEYS) {
        g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, low_level_kb_proc,
            GetModuleHandleW(nullptr), 0);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LockerWnd";
    RegisterClassExW(&wc);

    g_bg_brush = CreateSolidBrush(RGB(10, 10, 12));

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LockerWnd", L"LOCKER",
        WS_POPUP | WS_VISIBLE,
        0, 0, sw, sh, nullptr, nullptr, hi, nullptr);

    init_ui(hwnd);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    start_music();
    SetTimer(hwnd, ID_TIMER_TOP, 100, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (IsWindow(hwnd) && GetForegroundWindow() != hwnd) {
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
        }
    }

    KillTimer(hwnd, ID_TIMER_TOP);
    if (g_kb_hook) UnhookWindowsHookEx(g_kb_hook);
    if (DISABLE_TASKMGR) set_taskmgr_policy(false);
    stop_music();
    DeleteObject(g_bg_brush);
    return 0;
}
