void setup() {
    Serial.begin(115200);  // Baud rate ignored for USB Serial
    while (!Serial) {}  // Wait for Serial connection
}

void loop() {
    static uint32_t counter = 0;
    uint32_t buffer[512];  // Send 512 packets at a time

    for (int i = 0; i < 512; i++) {
        buffer[i] = counter++;
    }

    Serial.write((uint8_t*)buffer, sizeof(buffer));  // Send 2048 bytes per transfer
}
