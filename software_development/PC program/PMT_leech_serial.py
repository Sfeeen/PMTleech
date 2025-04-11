import serial
import serial.tools.list_ports
import time

# Pin mappings (bit positions in GPIO registers)
DATA_PINS = [2, 28, 29, 16, 12, 11, 10, 3]  # D2-D8 from GPIO7
ADDRESS_PINS = [3, 12, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]  # A0-A12 from GPIO6
WE_PIN = 0  # Write Enable (N_WE) in GPIO7
OE_PIN = 1  # Output Enable (N_OE) in GPIO7

def find_usb_serial_device():
    """Automatically find the first COM port with name 'USB Serial Device'."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "USB Serial Device" in port.description:  # Look for the specific name
            print(f"Found USB Serial Device on {port.device}")
            return port.device
    print("No USB Serial Device found.")
    return None

def extract_bits(value, bit_positions):
    """Extract specific bits from a value based on given bit positions."""
    result = 0
    for i, bit in enumerate(bit_positions):
        if (value >> bit) & 1:  # Check if the bit is set
            result |= (1 << i)  # Set corresponding bit in result
    return result

def parse_data(raw_bytes):
    """ Parse the received 8-byte data from Teensy """
    if len(raw_bytes) != 8:
        print("Invalid data length:", len(raw_bytes))
        return None

    # Read raw GPIO values
    gpio6 = int.from_bytes(raw_bytes[:4], byteorder='little')  # GPIO1
    gpio7 = int.from_bytes(raw_bytes[4:], byteorder='little')  # GPIO2

    # print(bin(gpio6), bin(gpio7))

    # Extract address (from GPIO6)
    address = extract_bits(gpio6, ADDRESS_PINS)

    # Extract data (D2-D8 from GPIO7, D1 from GPIO6)
    data_value = extract_bits(gpio7, DATA_PINS)  # Extract D2-D8
    d1_bit = (gpio6 >> 2) & 1  # Extract D1 (GPIO6_2)
    data_value |= (d1_bit << 0)  # Set D1 in the LSB position

    # Determine read or write operation
    we_signal = (gpio7 >> WE_PIN) & 1  # Extract Write Enable (WE)
    oe_signal = (gpio7 >> OE_PIN) & 1  # Extract Output Enable (OE)

    if we_signal == 0:
        operation = "WRITE"
    elif oe_signal == 0:
        operation = "READ"
    else:
        operation = "UNKNOWN"

    return address, data_value, operation

def main():
    com_port = find_usb_serial_device()
    if not com_port:
        print("Exiting program. No suitable COM port found.")
        return

    BAUD_RATE = 2000000  # Match the Teensy baud rate
    MEASUREMENT_INTERVAL = 1  # Measure speed every 1 second

    try:
        with serial.Serial(com_port, BAUD_RATE, timeout=1) as ser:
            print(f"Listening on {com_port} at {BAUD_RATE} baud...")

            ser.write("START\n".encode())
            start_time = time.time()
            total_bytes_received = 0

            last_address, last_data_value, last_operation = 0, 0, 0
            operation_counter = 0
            while True:
                raw_bytes = ser.read(8)  # Read 8 bytes from serial
                if raw_bytes:
                    total_bytes_received += len(raw_bytes)  # Count received bytes

                    result = parse_data(raw_bytes)
                    if result:
                        address, data, operation = result
                        #print(f"{operation} - Address: {(address)}, Data: {(hex(data))}")
                        # if (address != 0x463 or operation != "WRITE" or ((data - last_data_value) % 255 != 1 and not (data == 0 and last_data_value == 0xff))) and operation_counter > 1:
                        #
                        #     # print(f"WRONG")
                        #     # print(f"{last_operation} - Address: {hex(last_address)}, Data: {hex(last_data_value)}")
                        #     # print(f"{operation} - Address: {hex(address)}, Data: {hex(data)}")
                        #     print((data - last_data_value) % 255)
                        #     print(bin(last_data_value), hex(last_data_value), "PREV")
                        #     print(bin(data), hex(data))
                        #
                        # last_address = address
                        # last_data_value = data
                        # last_operation = operation
                        #
                        # operation_counter += 1

                # Check if it's time to report speed and mean time
                elapsed_time = time.time() - start_time
                if elapsed_time >= MEASUREMENT_INTERVAL:
                    # Compute data rate in MBps
                    speed_mbps = (total_bytes_received / elapsed_time) / (1024 * 1024)  # Convert bytes to MBps

                    # Compute mean time per packet
                    num_packets = total_bytes_received / 8  # Each packet is 8 bytes
                    mean_time_us = (elapsed_time / num_packets) * 1e6 if num_packets > 0 else 0  # Convert seconds to µs

                    print(f"Data Rate: {speed_mbps:.2f} MBps ({total_bytes_received} bytes) | Mean Time Between Data: {mean_time_us:.2f} µs")

                    # Reset counters
                    start_time = time.time()
                    total_bytes_received = 0

    except serial.SerialException as e:
        print("Serial error:", e)

if __name__ == "__main__":
    main()
