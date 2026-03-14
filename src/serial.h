#define MAX_PORTS 64

typedef struct {
    char portName[16];      // e.g. "COM3"
    char friendlyName[128]; // e.g. "USB-SERIAL CH340 (COM3)"  — may be empty
} PortInfo;

#if defined(_WIN32)
#include <Windows.h>

int EnumeratePorts(PortInfo *ports, int maxPorts);
void BuildDisplayString(const PortInfo *p, char *out, int outLen);
HANDLE OpenSerialPort(const char *portName, DWORD baudRate);
void SerialLoop(HANDLE hSerial);
#endif
