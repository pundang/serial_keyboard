#if defined(_WIN32)
#include <Windows.h>
#include <stdio.h>

static HANDLE OpenSerialPort(const char* portName, DWORD baudRate) {
    char fullName[64];
    snprintf(fullName, sizeof(fullName), "\\\\.\\%s", portName);

    HANDLE hSerial = CreateFileA(fullName, GENERIC_READ, 0, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout        = 50;
    timeouts.ReadTotalTimeoutConstant   = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

static void SendKey(WORD vk) {
    INPUT inputs[2] = {0};
    inputs[0].type   = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type       = INPUT_KEYBOARD;
    inputs[1].ki.wVk     = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// Translates any received byte to a VK:
// 0x41-0x5A -> A-Z keys directly (HID/ASCII overlap)
// 0x30-0x39 -> 0-9 keys
// anything else -> treated as a raw VK code
static WORD ByteToVK(BYTE b) {
    if (b >= 0x41 && b <= 0x5A) return (WORD)b; // A-Z
    if (b >= 0x61 && b <= 0x7A) return (WORD)(b - 0x20); // a-z -> A-Z VK
    if (b >= 0x30 && b <= 0x39) return (WORD)b; // 0-9
    return (WORD)b; // fallback: use byte as raw VK code
}

static void SerialLoop(HANDLE hSerial) {
    BYTE  byte;
    DWORD read;

    printf("Listening for serial bytes...\n");

    while (1) {
        if (!ReadFile(hSerial, &byte, 1, &read, NULL) || read == 0) continue;

        printf("Received: 0x%02X -> VK 0x%02X\n", byte, ByteToVK(byte));
        SendKey(ByteToVK(byte));
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow) {
    const char* PORT      = "COM3";
    const DWORD BAUD_RATE = 9600;

    HANDLE hSerial = OpenSerialPort(PORT, BAUD_RATE);
    if (hSerial == INVALID_HANDLE_VALUE) {
        MessageBoxA(NULL, "Failed to open serial port.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    SerialLoop(hSerial);
    CloseHandle(hSerial);
    return 0;
}
#endif

int main() { printf("PROGRAM IS NOT AVAILABLE ON LINUX, TEMPORARILY."); }
