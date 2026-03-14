#if defined(_WIN32)
#include <Windows.h>
#include <devguid.h>
#include <regstr.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")

#define MAX_PORTS 64

typedef struct {
    char portName[16];      // e.g. "COM3"
    char friendlyName[128]; // e.g. "USB-SERIAL CH340 (COM3)"  — may be empty
} PortInfo;

// Enumerate serial ports via SetupDiGetClassDevs (returns active/present
// ports). Falls back to a plain registry scan for any ports the device manager
// misses.
static int EnumeratePorts(PortInfo *ports, int maxPorts) {
    int count = 0;

    // --- Pass 1: SetupAPI (gives us friendly names) ---
    HDEVINFO hDevInfo =
        SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);

    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
            if (count >= maxPorts)
                break;

            // Get the "PortName" registry value (e.g. "COM3")
            HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hKey == INVALID_HANDLE_VALUE)
                continue;

            char portName[16] = {0};
            DWORD size = sizeof(portName);
            DWORD type = 0;
            LONG res = RegQueryValueExA(hKey, "PortName", NULL, &type, (LPBYTE)portName, &size);
            RegCloseKey(hKey);

            if (res != ERROR_SUCCESS || strncmp(portName, "COM", 3) != 0)
                continue;

            // Get the friendly name from the device property
            char friendly[128] = {0};
            SetupDiGetDeviceRegistryPropertyA(
                hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly,
                sizeof(friendly), NULL
            );

            strncpy(ports[count].portName, portName, sizeof(ports[count].portName) - 1);
            strncpy(ports[count].friendlyName, friendly, sizeof(ports[count].friendlyName) - 1);
            count++;
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    // --- Pass 2: Registry fallback (catches any ports missed above) ---
    HKEY hSerial;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hSerial) == ERROR_SUCCESS) {
        char valueName[64], portName[16];
        DWORD idx = 0;
        while (1) {
            DWORD vnSize = sizeof(valueName);
            DWORD pnSize = sizeof(portName);
            DWORD type = 0;
            if (RegEnumValueA(hSerial, idx++, valueName, &vnSize, NULL, &type, (LPBYTE)portName, &pnSize) != ERROR_SUCCESS)
                break;

            // Check if already listed
            int found = 0;
            for (int j = 0; j < count; j++) {
                if (_stricmp(ports[j].portName, portName) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && count < maxPorts) {
                strncpy(ports[count].portName, portName, sizeof(ports[count].portName) - 1);
                ports[count].friendlyName[0] = '\0';
                count++;
            }
        }
        RegCloseKey(hSerial);
    }

    return count;
}

// Build the display string shown in the list box:
//   If friendlyName already contains the port name → use it as-is
//   Else if friendlyName is non-empty → "COM3 (USB-SERIAL CH340)"
//   Else → "COM3"
static void BuildDisplayString(const PortInfo *p, char *out, int outLen) {
    if (p->friendlyName[0] != '\0') {
        // Many drivers embed "(COMx)" in the friendly name already
        if (strstr(p->friendlyName, p->portName)) {
            strncpy(out, p->friendlyName, outLen - 1);
            out[outLen - 1] = '\0';
        } else {
            // Strip any trailing " (COMx)" the driver may add, then add ours
            char stripped[128];
            strncpy(stripped, p->friendlyName, sizeof(stripped) - 1);
            stripped[sizeof(stripped) - 1] = '\0';
            // Remove trailing parenthetical if present
            char *paren = strrchr(stripped, '(');
            if (paren && paren > stripped)
                *(paren - 1) = '\0';

            snprintf(out, outLen, "%s (%s)", p->portName, stripped);
        }
    } else {
        strncpy(out, p->portName, outLen - 1);
        out[outLen - 1] = '\0';
    }
}

static HANDLE OpenSerialPort(const char *portName, DWORD baudRate) {
    char fullName[64];
    snprintf(fullName, sizeof(fullName), "\\\\.\\%s", portName);

    HANDLE hSerial = CreateFileA(fullName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

static void SendKey(WORD vk) {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static WORD ByteToVK(BYTE b) {
    if (b >= 0x41 && b <= 0x5A)
        return (WORD)b;
    if (b >= 0x61 && b <= 0x7A)
        return (WORD)(b - 0x20);
    if (b >= 0x30 && b <= 0x39)
        return (WORD)b;
    return (WORD)b;
}

static void SerialLoop(HANDLE hSerial) {
    BYTE byte;
    DWORD read;
    printf("Listening for serial bytes...\n");
    while (1) {
        if (!ReadFile(hSerial, &byte, 1, &read, NULL) || read == 0)
            continue;
        printf("Received: 0x%02X -> VK 0x%02X\n", byte, ByteToVK(byte));
        SendKey(ByteToVK(byte));
    }
}

// Gui
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
