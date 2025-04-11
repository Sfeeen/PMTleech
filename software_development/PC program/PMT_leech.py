import serial
import struct
import time

# Open USB Serial Port (Adjust for Windows COM or Linux /dev/ttyACM)
ser = serial.Serial('COM15', 10000000, timeout=0)

# Use a larger buffer to minimize USB overhead
BUFFER_SIZE = 4096  # Read larger chunks
raw_buffer = bytearray(BUFFER_SIZE)

counter = 0
start_time = time.time()
bytes_received = 0
last_value = 0  # Store last received 32-bit value

# Use `memoryview` to avoid unnecessary copies
buffer_view = memoryview(raw_buffer)

while True:
    num_bytes = ser.readinto(buffer_view)  # Efficient bulk read
    if num_bytes:
        counter += num_bytes // 4  # Count uint32_t packets
        bytes_received += num_bytes

        # Extract the last 32-bit value received
        if num_bytes >= 4:
            last_value = struct.unpack('<I', raw_buffer[num_bytes-4:num_bytes])[0]

    # Print speed and last received value every second
    if time.time() - start_time >= 1:
        print(f"Speed: {bytes_received / 1024 / 1024:.2f} MB/s, "
              f"Packets: {counter}, "
              f"Last Value: 0b{last_value:032b}")

        bytes_received = 0
        start_time = time.time()


if __name__ == '__main__':
    pass