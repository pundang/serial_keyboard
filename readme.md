# Serial Keyboard emulator for Windows

Originally made for CH340, this program is a keyboard emulator for Windows. It allows you to send keystrokes to your computer by connecting a serial device and sending data to it.

## Notes

* Works at 230400 baud
* The program translates the serial data into keystrokes and sends them to the operating system following the [Microsoft's VK key codes](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).

## Example Usage (Arduino)

```c
void setup() {
    Serial.begin(230400);
}

int hKey = 0x48; // H
int iKey = 0x49; // I

void loop() {
    Serial.write(hKey);
    Serial.write(iKey);

    // OUTPUT: "HI"

    while(true) {
        // Do nothing
    }
}
```

![Example result](repo/example-result.png)
