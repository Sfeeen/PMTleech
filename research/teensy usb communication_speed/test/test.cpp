#include <windows.h>
#include <iostream>
#include <chrono>

#define COM_PORT "\\\\.\\COM18"  // Replace with your Teensy COM port
#define BUFFER_SIZE 8096  // Use a larger buffer for faster transfers

int main() {
    HANDLE hSerial = CreateFileA(COM_PORT, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening serial port!" << std::endl;
        return 1;
    }

    // Configure serial port settings
    DCB serialParams = { 0 };
    serialParams.DCBlength = sizeof(serialParams);
    if (!GetCommState(hSerial, &serialParams)) {
        std::cerr << "Error getting serial port state!" << std::endl;
        return 1;
    }

    serialParams.BaudRate = 115200;  // Ignored for USB Serial (Teensy)
    serialParams.ByteSize = 8;
    serialParams.StopBits = ONESTOPBIT;
    serialParams.Parity = NOPARITY;
    serialParams.fBinary = TRUE;
    serialParams.fDtrControl = DTR_CONTROL_ENABLE;

    if (!SetCommState(hSerial, &serialParams)) {
        std::cerr << "Error setting serial port state!" << std::endl;
        return 1;
    }

    // Set optimized timeouts
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;  // Non-blocking reads
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hSerial, &timeouts);

    // Buffer for receiving data
    uint8_t buffer[BUFFER_SIZE];  // Use a large buffer
    DWORD bytesRead;
    size_t totalBytesReceived = 0;
    int count = 0;

    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        // Read large chunks of data asynchronously
        if (ReadFile(hSerial, buffer, BUFFER_SIZE, &bytesRead, &overlapped) || GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(overlapped.hEvent, INFINITE);  // Wait until data is received
            GetOverlappedResult(hSerial, &overlapped, &bytesRead, TRUE);

            if (bytesRead > 0) {
                count += bytesRead / sizeof(uint32_t);  // Count packets (4 bytes per packet)
                totalBytesReceived += bytesRead;
            }
        }

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 1) {
            double mbps = (totalBytesReceived / (1024.0 * 1024.0));  // Convert bytes to MB
            std::cout << "Packets per second: " << count
                << " | Transfer Speed: " << mbps << " MBps" << std::endl;

            // Reset counters
            count = 0;
            totalBytesReceived = 0;
            start = std::chrono::high_resolution_clock::now();
        }
    }

    CloseHandle(hSerial);
    CloseHandle(overlapped.hEvent);
    return 0;
}
