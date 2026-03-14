#include "serial.h"

#if defined(_WIN32)
#include <Windows.h>
#include <devguid.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")

// Enumerate serial ports via SetupDiGetClassDevs (returns active/present
// ports). Falls back to a plain registry scan for any ports the device manager
// misses.
int EnumeratePorts(PortInfo *ports, int maxPorts) {
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
void BuildDisplayString(const PortInfo *p, char *out, int outLen) {
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

HANDLE OpenSerialPort(const char *portName, DWORD baudRate) {
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

// PRIVATE
static void SendKey(WORD vk) {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// PRIVATE
static WORD ByteToVK(BYTE b) {
    if (b >= 0x41 && b <= 0x5A)
        return (WORD)b;
    if (b >= 0x61 && b <= 0x7A)
        return (WORD)(b - 0x20);
    if (b >= 0x30 && b <= 0x39)
        return (WORD)b;
    return (WORD)b;
}

void SerialLoop(HANDLE hSerial) {
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
#endif
