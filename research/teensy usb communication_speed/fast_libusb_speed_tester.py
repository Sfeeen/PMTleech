import usb.core
import usb.util
import numpy as np
import time

# Find Teensy
dev = usb.core.find(idVendor=0x16C0, idProduct=0x0486)

if dev is None:
    raise ValueError("Teensy not found")

dev.set_configuration()
endpoint = dev[0][(0, 0)][0]  # USB Bulk IN endpoint

BUFFER_SIZE = 4096  # Read large chunks
raw_buffer = bytearray(BUFFER_SIZE)

with open("sram_log.bin", "wb") as f:
    start_time = time.time()
    bytes_received = 0

    while True:
        try:
            num_bytes = dev.read(endpoint.bEndpointAddress, BUFFER_SIZE, timeout=1000)
            f.write(num_bytes)  # Save binary data
            bytes_received += len(num_bytes)

            # Monitor speed
            if time.time() - start_time > 1:
                print(f"USB Speed: {bytes_received / 1e6:.2f} MB/s")
                bytes_received = 0
                start_time = time.time()

        except usb.core.USBError as e:
            if e.errno == 110:  # Timeout
                continue
