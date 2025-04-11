import sys
import time
import serial
import serial.tools.list_ports
from PyQt5 import QtWidgets, QtCore

class SerialReader(QtCore.QThread):
    new_packets = QtCore.pyqtSignal(list)
    stats_update = QtCore.pyqtSignal(int, int, int, int, float)

    def __init__(self, serial_port, address_pins, data_pins, we_pin, oe_pin):
        super().__init__()
        self.serial_port = serial_port
        self.running = True
        self.ADDRESS_PINS = address_pins
        self.DATA_PINS = data_pins
        self.WE_PIN = we_pin
        self.OE_PIN = oe_pin
        self.start_time = time.time()
        self.total_bytes = 0
        self.packet_counter = 0
        self.valid_packet_counter = 0
        self.read_counter = 0
        self.write_counter = 0
        self.valid_packet_start = False

    def extract_bits(self, value, bit_positions):
        result = 0
        for i, bit in enumerate(bit_positions):
            if (value >> bit) & 1:
                result |= (1 << i)
        return result

    def check_operation(self, gpio7):
        we_signal = (gpio7 >> self.WE_PIN) & 1
        oe_signal = (gpio7 >> self.OE_PIN) & 1
        if we_signal == 0:
            return "WRITE"
        elif oe_signal == 0:
            return "READ"
        else:
            return None

    def parse_data(self, gpio6, gpio7):
        address = self.extract_bits(gpio6, self.ADDRESS_PINS)
        data_value = self.extract_bits(gpio7, self.DATA_PINS)
        d1_bit = (gpio6 >> 2) & 1
        data_value |= (d1_bit << 0)
        return address, data_value

    def run(self):
        while self.running:
            available = self.serial_port.in_waiting
            if available >= 8:
                read_bytes = self.serial_port.read(available - (available % 8))
                buffer_view = memoryview(read_bytes)
                packets = []

                for i in range(0, len(buffer_view), 8):
                    raw = buffer_view[i:i+8]
                    self.packet_counter += 1
                    self.total_bytes += 8

                    gpio6 = int.from_bytes(raw[:4], 'little')
                    gpio7 = int.from_bytes(raw[4:], 'little')
                    operation = self.check_operation(gpio7)

                    if operation in ["READ", "WRITE"]:
                        if not self.valid_packet_start:
                            self.valid_packet_start = True

                        address, value = self.parse_data(gpio6, gpio7)

                        self.valid_packet_counter += 1
                        if operation == "READ":
                            self.read_counter += 1
                        elif operation == "WRITE":
                            self.write_counter += 1

                        packets.append((self.valid_packet_counter, operation, address, value))
                    else:
                        self.valid_packet_start = False

                if packets:
                    self.new_packets.emit(packets)

                elapsed = time.time() - self.start_time
                speed_mbps = (self.total_bytes / elapsed) / (1024 * 1024)
                mean_time_us = (elapsed / self.valid_packet_counter) * 1e6 if self.valid_packet_counter > 0 else 0
                self.stats_update.emit(self.packet_counter, self.read_counter, self.write_counter, self.total_bytes, mean_time_us)
            time.sleep(0.001)

    def stop(self):
        self.running = False
        self.wait()

class SerialMonitor(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.init_ui()
        self.serial_port = None
        self.reader_thread = None

        self.ADDRESS_PINS = [3, 12, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]
        self.DATA_PINS = [2, 28, 29, 16, 12, 11, 10, 3]
        self.WE_PIN = 0
        self.OE_PIN = 1

    def init_ui(self):
        layout = QtWidgets.QVBoxLayout()

        self.port_combo = QtWidgets.QComboBox()
        self.refresh_button = QtWidgets.QPushButton("Refresh COM Ports")
        self.refresh_button.clicked.connect(self.refresh_ports)

        port_layout = QtWidgets.QHBoxLayout()
        port_layout.addWidget(self.port_combo)
        port_layout.addWidget(self.refresh_button)
        layout.addLayout(port_layout)

        self.start_button = QtWidgets.QPushButton("Start Listening")
        self.stop_button = QtWidgets.QPushButton("Stop Listening")
        self.stop_button.setEnabled(False)
        self.start_button.clicked.connect(self.start_listening)
        self.stop_button.clicked.connect(self.stop_listening)

        control_layout = QtWidgets.QHBoxLayout()
        control_layout.addWidget(self.start_button)
        control_layout.addWidget(self.stop_button)
        layout.addLayout(control_layout)

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["Counter", "Operation", "Address", "Value"])
        layout.addWidget(self.table)

        self.speed_label = QtWidgets.QLabel("Total Speed: 0 MBps | Avg Time: 0 µs")
        self.counter_label = QtWidgets.QLabel("Packets: 0 | Reads: 0 | Writes: 0")
        layout.addWidget(self.speed_label)
        layout.addWidget(self.counter_label)

        self.setLayout(layout)
        self.setWindowTitle("Teensy Serial Monitor")
        self.resize(600, 400)

        self.refresh_ports()

    def refresh_ports(self):
        self.port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            label = f"{port.device} - {port.description}"
            self.port_combo.addItem(label, port.device)
            if "USB Serial Device" in port.description:
                index = self.port_combo.findText(label)
                self.port_combo.setCurrentIndex(index)

    def start_listening(self):
        port_name = self.port_combo.currentData()
        if not port_name:
            QtWidgets.QMessageBox.warning(self, "Error", "No COM port selected")
            return

        try:
            self.serial_port = serial.Serial(port_name, 2000000, timeout=0)
            self.table.setRowCount(0)
            self.reader_thread = SerialReader(self.serial_port, self.ADDRESS_PINS, self.DATA_PINS, self.WE_PIN, self.OE_PIN)
            self.reader_thread.new_packets.connect(self.append_packets)
            self.reader_thread.stats_update.connect(self.update_stats)
            self.reader_thread.start()
            self.start_button.setEnabled(False)
            self.stop_button.setEnabled(True)
        except serial.SerialException as e:
            QtWidgets.QMessageBox.critical(self, "Serial Error", str(e))

    def stop_listening(self):
        if self.reader_thread:
            self.reader_thread.stop()
        if self.serial_port:
            self.serial_port.close()
        self.start_button.setEnabled(True)
        self.stop_button.setEnabled(False)

    def append_packets(self, packets):
        for counter, operation, address, value in packets:
            row = self.table.rowCount()
            self.table.insertRow(row)
            self.table.setItem(row, 0, QtWidgets.QTableWidgetItem(str(counter)))
            self.table.setItem(row, 1, QtWidgets.QTableWidgetItem(operation))
            self.table.setItem(row, 2, QtWidgets.QTableWidgetItem(hex(address)))
            self.table.setItem(row, 3, QtWidgets.QTableWidgetItem(hex(value)))

    def update_stats(self, packet_counter, read_counter, write_counter, total_bytes, mean_time_us):
        speed_mbps = (total_bytes / (time.time() - self.reader_thread.start_time)) / (1024 * 1024)
        self.speed_label.setText(f"Total Speed: {speed_mbps:.2f} MBps | Avg Time: {mean_time_us:.2f} µs")
        self.counter_label.setText(f"Packets: {packet_counter} | Reads: {read_counter} | Writes: {write_counter}")

if __name__ == '__main__':
    app = QtWidgets.QApplication(sys.argv)
    window = SerialMonitor()
    window.show()
    sys.exit(app.exec_())
