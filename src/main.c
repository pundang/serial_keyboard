#include "serial.h"

#include <stdio.h>

#if defined(_WIN32)
#include <Windows.h>

#define IDC_LIST_PORTS 1001
#define IDC_BTN_REFRESH 1002
#define IDC_BTN_CONNECT 1003
#define IDC_BTN_DISCONNECT 1004
#define IDC_LABEL_STATUS 1005

typedef struct {
    PortInfo ports[MAX_PORTS];
    int portCount;
    HANDLE hSerial;
    HANDLE hThread;
} AppState;

static AppState g_state = {0};

// Thread proc — runs SerialLoop so the GUI stays responsive
static DWORD WINAPI SerialThreadProc(LPVOID param) {
    SerialLoop((HANDLE)param);
    return 0;
}

static void PopulateListBox(HWND hList) {
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    g_state.portCount = EnumeratePorts(g_state.ports, MAX_PORTS);

    if (g_state.portCount == 0) {
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM) "(No serial ports found)");
    } else {
        for (int i = 0; i < g_state.portCount; i++) {
            char display[160];
            BuildDisplayString(&g_state.ports[i], display, sizeof(display));
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)display);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hList, hBtnRefresh, hBtnConnect, hLabel;

    switch (msg) {
    case WM_CREATE: {
        // Title label
        CreateWindowA("STATIC", "Available Serial Ports:", WS_CHILD | WS_VISIBLE, 10, 10, 360, 20, hWnd, NULL, NULL, NULL);

        // List box
        hList = CreateWindowA(
            "LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 10, 35,
            360, 140, hWnd, (HMENU)(UINT_PTR)IDC_LIST_PORTS, NULL, NULL
        );

        // Refresh button
        hBtnRefresh = CreateWindowA(
            "BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 185, 90,
            28, hWnd, (HMENU)(UINT_PTR)IDC_BTN_REFRESH, NULL, NULL
        );

        // Connect button
        hBtnConnect = CreateWindowA(
            "BUTTON", "Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 185,
            90, 28, hWnd, (HMENU)(UINT_PTR)IDC_BTN_CONNECT, NULL, NULL
        );

        // Status label
        hLabel = CreateWindowA("STATIC", "AWAITING CONNECTION...", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 225, 360, 20, hWnd, (HMENU)(UINT_PTR)IDC_LABEL_STATUS, NULL, NULL);

        PopulateListBox(hList);
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);

        if (id == IDC_BTN_REFRESH) {
            PopulateListBox(hList);
            SetWindowTextA(hLabel, "Port list refreshed.");
        }

        if (id == IDC_BTN_CONNECT) {
            LRESULT sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR || g_state.portCount == 0) {
                SetWindowTextA(hLabel, "Please select a port first.");
                break;
            }

            // Disconnect existing connection
            if (g_state.hThread) {
                TerminateThread(g_state.hThread, 0);
                CloseHandle(g_state.hThread);
                g_state.hThread = NULL;
            }
            if (g_state.hSerial && g_state.hSerial != INVALID_HANDLE_VALUE) {
                CloseHandle(g_state.hSerial);
                g_state.hSerial = NULL;
            }

            const char *portName = g_state.ports[(int)sel].portName;
            g_state.hSerial = OpenSerialPort(portName, 230400);

            if (g_state.hSerial == INVALID_HANDLE_VALUE) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Failed to open %s.", portName);
                SetWindowTextA(hLabel, msg);
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "Connected to %s at 230400 baud.", portName);
                SetWindowTextA(hLabel, msg);

                // Replace Connect with Disconnect
                SetWindowText(hBtnConnect, TEXT("Disconnect"));
                SetWindowLongPtr(hBtnConnect, GWLP_ID, IDC_BTN_DISCONNECT);

                // Spawn serial reading thread
                g_state.hThread = CreateThread(NULL, 0, SerialThreadProc, (LPVOID)g_state.hSerial, 0, NULL);
            }
        }

        if (id == IDC_BTN_DISCONNECT) {
            // Disconnect existing connection
            if (g_state.hThread) {
                TerminateThread(g_state.hThread, 0);
                CloseHandle(g_state.hThread);
                g_state.hThread = NULL;
            }
            if (g_state.hSerial && g_state.hSerial != INVALID_HANDLE_VALUE) {
                CloseHandle(g_state.hSerial);
                g_state.hSerial = NULL;
            }

            SetWindowText(hBtnConnect, TEXT("Connect"));
            SetWindowLongPtr(hBtnConnect, GWLP_ID, IDC_BTN_CONNECT);

            char msg[128];
            snprintf(msg, sizeof(msg), "Disconnected...");
            SetWindowTextA(hLabel, msg);

            Sleep(500);

            snprintf(msg, sizeof(msg), "AWAITING CONNECTION...");
            SetWindowTextA(hLabel, msg);
        }
        break;
    }

    case WM_DESTROY:
        if (g_state.hThread) {
            TerminateThread(g_state.hThread, 0);
            CloseHandle(g_state.hThread);
        }
        if (g_state.hSerial && g_state.hSerial != INVALID_HANDLE_VALUE)
            CloseHandle(g_state.hSerial);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Register window class
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SerialInputClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA(
        "SerialInputClass", "SERIAL TO KEYBOARD",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 400, 295, NULL, NULL, hInstance, NULL
    );

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

#endif // _WIN32

// Stub for non-Windows builds
#if !defined(_WIN32)
#include <stdio.h>
int main(void) {
    printf("This program is Windows-only.\n");
    return 1;
}
#endif
