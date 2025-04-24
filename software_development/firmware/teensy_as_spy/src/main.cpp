#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_MAGIC 0x42
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_CONFIG_ADDR 1  // start config at address 1

#define EXT_CLK_PIN 29

#define RUNNING_PIN 2
#define RESET_CPU_PIN 3


enum RunMode : uint8_t {
  INTERRUPT_MODE = 0,
  CONTINUOUS_MODE = 1
};

enum State : uint8_t {
  IDLE = 0,
  RUNNING = 1
};

struct Config {
  uint8_t we_pin;
  uint8_t oe_pin;
  uint8_t cs_pin; 
  RunMode mode;
  uint32_t freq;
  uint32_t xtal_freq;
  uint8_t idle_behavior; // 0 = halt, 1 = run at XTAL
  char chip_name[30];
  uint8_t address_pins[24]; // A1–A24 max
  uint8_t address_count;
  uint8_t data_pins[16];    // D1–D16 max
  uint8_t data_count;
};


Config config;
State state = IDLE;

volatile uint32_t gpio_data[2];
volatile bool dataset_ready = false;

IntervalTimer sample_timer;

// FASTRUN void handle_memory_access() {
//   gpio_data[0] = GPIO6_DR;
//   gpio_data[1] = GPIO7_DR;
//   dataset_ready = true;
// }

FASTRUN void handle_memory_access() {
  if (Serial.availableForWrite() >= 8) {
    uint32_t gpio6 = GPIO6_DR;
    uint32_t gpio7 = GPIO7_DR;
    Serial.write((uint8_t*)&gpio6, 4);
    Serial.write((uint8_t*)&gpio7, 4);
  }
}

void continuous_sample() {
  handle_memory_access();
}

void save_config_to_eeprom() {
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  EEPROM.put(EEPROM_CONFIG_ADDR, config);
}


void load_config_from_eeprom() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_CONFIG_ADDR, config);
  } else {

    Serial.println("Using default values!");

    config.we_pin = 10;
    config.oe_pin = 12;
    config.cs_pin = 11;
    config.mode = INTERRUPT_MODE;
    config.freq = 70000;
    config.xtal_freq = 16000000;
    config.idle_behavior = 0;
    strncpy(config.chip_name, "AT28C64B", sizeof(config.chip_name) - 1);
    config.chip_name[sizeof(config.chip_name) - 1] = '\0';

    uint8_t default_addr[] = {0, 24, 25, 19, 18, 14, 15, 40, 41, 17, 16, 22, 23};
    config.address_count = sizeof(default_addr);
    memcpy(config.address_pins, default_addr, config.address_count);

    uint8_t default_data[] = {1, 35, 34, 8, 32, 9, 6, 13};
    config.data_count = sizeof(default_data);
    memcpy(config.data_pins, default_data, config.data_count);
  }
}

void write_memory_from_serial() {
  uint32_t max_address = (1UL << config.address_count);
  if (max_address > MEMORY_SIZE) max_address = MEMORY_SIZE;

  // Wait until we have enough bytes
  while (Serial.available() < max_address) {
    delay(1);  // Wait a bit
  }

  // Read data into buffer
  static uint8_t buffer[MEMORY_SIZE];
  Serial.readBytes(buffer, max_address);

  // Now wait for confirmation line
  String end_marker;
  while (true) {
    if (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        end_marker.trim();
        if (end_marker == "WRITEALL DONE") break;
        else end_marker = "";
      } else {
        end_marker += ch;
      }
    }
  }

  Serial.printf("Writing %lu bytes to chip...\n", max_address);

  // Setup address & data pins
  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], OUTPUT);

  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], OUTPUT);

  pinMode(config.cs_pin, OUTPUT);
  pinMode(config.oe_pin, OUTPUT);
  pinMode(config.we_pin, OUTPUT);

  digitalWriteFast(config.cs_pin, LOW);
  digitalWriteFast(config.oe_pin, HIGH);  // Disable output
  digitalWriteFast(config.we_pin, HIGH);  // Write is inactive when HIGH

  for (uint32_t addr = 0; addr < max_address; ++addr) {
    uint8_t value = buffer[addr];

    // Set address lines
    for (uint8_t i = 0; i < config.address_count; ++i) {
      digitalWriteFast(config.address_pins[i], (addr >> i) & 1);
    }

    // Set data lines
    for (uint8_t i = 0; i < config.data_count; ++i) {
      digitalWriteFast(config.data_pins[i], (value >> i) & 1);
    }

    // Pulse WE low
    digitalWriteFast(config.we_pin, LOW);
    delayMicroseconds(10);  // adjust as needed
    digitalWriteFast(config.we_pin, HIGH);

    delayMicroseconds(10);  // chip write delay
  }

  // Disable chip
  digitalWriteFast(config.cs_pin, HIGH);
  digitalWriteFast(config.oe_pin, HIGH);
  digitalWriteFast(config.we_pin, HIGH);

  // 🧹 Reset all previously output pins to input
  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], INPUT);
  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], INPUT);

  pinMode(config.cs_pin, INPUT);
  pinMode(config.oe_pin, INPUT);
  pinMode(config.we_pin, INPUT);

  Serial.println("WRITE DONE");
}


void read_memory() {
  uint32_t max_address = (1UL << config.address_count);

  Serial.printf("Reading 0x%lX addresses...\n", max_address);

  // --- Set all address pins as OUTPUT
  for (uint8_t i = 0; i < config.address_count; ++i) {
    pinMode(config.address_pins[i], OUTPUT);
  }

  // --- Set all data pins as INPUT
  for (uint8_t i = 0; i < config.data_count; ++i) {
    pinMode(config.data_pins[i], INPUT);
  }

  // --- Configure chip control signals
  pinMode(config.cs_pin, OUTPUT);
  pinMode(config.oe_pin, OUTPUT);
  digitalWriteFast(config.cs_pin, LOW);  // Enable chip
  digitalWriteFast(config.oe_pin, LOW);  // Enable output

  // --- Loop over address space
  for (uint32_t addr = 0; addr < max_address; ++addr) {
    // Set address lines
    for (uint8_t i = 0; i < config.address_count; ++i) {
      digitalWriteFast(config.address_pins[i], (addr >> i) & 1);
    }

    delayMicroseconds(50);  // settling time

    // Read data byte
    uint8_t data = 0;
    for (uint8_t i = 0; i < config.data_count; ++i) {
      if (digitalReadFast(config.data_pins[i])) {
        data |= (1 << i);
      }
    }

    Serial.printf("0x%05lX: 0x%02X\n", addr, data);
  }

  // Disable chip
  digitalWriteFast(config.cs_pin, HIGH);
  digitalWriteFast(config.oe_pin, HIGH);
  digitalWriteFast(config.we_pin, HIGH);

  // 🧹 Reset all previously output pins to input
  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], INPUT);
  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], INPUT);

  pinMode(config.cs_pin, INPUT);
  pinMode(config.oe_pin, INPUT);
  pinMode(config.we_pin, INPUT);

  Serial.println("READ DONE");
}



void send_ack(const char* msg) {
  Serial.println(msg);
}

void enter_running_mode() {
    //send_ack("ACK START RUNNING");
  if (config.mode == INTERRUPT_MODE) {
    if (config.we_pin != (uint8_t)-1) {
      pinMode(config.we_pin, INPUT);
      attachInterrupt(digitalPinToInterrupt(config.we_pin), handle_memory_access, FALLING);
    }
    if (config.oe_pin != (uint8_t)-1) {
      pinMode(config.oe_pin, INPUT);
      attachInterrupt(digitalPinToInterrupt(config.oe_pin), handle_memory_access, FALLING);
    }
  } else {
    // sample_timer.begin(continuous_sample, 1000000UL / config.freq);
  }
  analogWriteFrequency(EXT_CLK_PIN, config.freq);
  analogWrite(EXT_CLK_PIN, 128);  // 50% duty cycle

  digitalWrite(RUNNING_PIN, HIGH);
  state = RUNNING;
  //send_ack("ACK RUNNING");
}

void enter_idle_mode() {
  if (config.mode == INTERRUPT_MODE) {
    if (config.we_pin != (uint8_t)-1) {
      detachInterrupt(digitalPinToInterrupt(config.we_pin));
    }
    if (config.oe_pin != (uint8_t)-1) {
      detachInterrupt(digitalPinToInterrupt(config.oe_pin));
    }
  }

  if (config.idle_behavior == 1 && config.xtal_freq > 0) {
    // ✳️ XTAL mode — run EXT_CLK at specified xtal_freq
    analogWriteFrequency(EXT_CLK_PIN, config.xtal_freq);
    analogWrite(EXT_CLK_PIN, 128); // 50% duty
  } else {
    // ❌ HALT mode — disable clock
    analogWrite(EXT_CLK_PIN, 0);          // Optional: reduces duty to 0%
    digitalWriteFast(EXT_CLK_PIN, LOW);   // Drive LOW
  }

  digitalWrite(RUNNING_PIN, LOW);
  state = IDLE;
  send_ack("ACK IDLE");
}

void parse_config_line(const String& line) {
  if (!line.startsWith("CFG")) return;

  int we = line.indexOf("WE=");
  int oe = line.indexOf("OE=");
  int mode = line.indexOf("MODE=");
  int freq = line.indexOf("FREQ=");
  int xtal = line.indexOf("XTAL=");
  int idle = line.indexOf("IDLE=");
  int addrs = line.indexOf("ADDRS=");
  int datas = line.indexOf("DATAS=");
  int name = line.indexOf("NAME=");
  int cs = line.indexOf("CS=");


  if (we != -1) config.we_pin = line.substring(we + 3, line.indexOf(' ', we + 3)).toInt();
  if (oe != -1) config.oe_pin = line.substring(oe + 3, line.indexOf(' ', oe + 3)).toInt();
  if (mode != -1) config.mode = (RunMode)line.substring(mode + 5, line.indexOf(' ', mode + 5)).toInt();
  if (freq != -1) config.freq = line.substring(freq + 5, line.indexOf(' ', freq + 5)).toInt();
  if (xtal != -1) config.xtal_freq = line.substring(xtal + 5, line.indexOf(' ', xtal + 5)).toInt();
  if (idle != -1) config.idle_behavior = line.substring(idle + 5, line.indexOf(' ', idle + 5)).toInt();
  if (cs != -1) config.cs_pin = line.substring(cs + 3, line.indexOf(' ', cs + 3)).toInt();


  // Parse address pins
  if (addrs != -1) {
    String pinlist = line.substring(addrs + 6, line.indexOf(' ', addrs + 6));
    config.address_count = 0;
    int start = 0;
    while (start < pinlist.length()) {
      int comma = pinlist.indexOf(',', start);
      if (comma == -1) comma = pinlist.length();
      config.address_pins[config.address_count++] = pinlist.substring(start, comma).toInt();
      start = comma + 1;
      if (config.address_count >= sizeof(config.address_pins)) break;
    }
  }

  // Parse data pins
  if (datas != -1) {
    String pinlist = line.substring(datas + 6, line.indexOf(' ', datas + 6));
    config.data_count = 0;
    int start = 0;
    while (start < pinlist.length()) {
      int comma = pinlist.indexOf(',', start);
      if (comma == -1) comma = pinlist.length();
      config.data_pins[config.data_count++] = pinlist.substring(start, comma).toInt();
      start = comma + 1;
      if (config.data_count >= sizeof(config.data_pins)) break;
    }
  }

  // Name comes last
  if (name != -1) {
    String n = line.substring(name + 5);
    n.trim();
    strncpy(config.chip_name, n.c_str(), sizeof(config.chip_name) - 1);
    config.chip_name[sizeof(config.chip_name) - 1] = '\0';
  }

  save_config_to_eeprom();
  send_ack("ACK CONFIG OK");
}

void handle_get_config() {

  Serial.printf("WE=%d OE=%d CS=%d MODE=%d FREQ=%lu XTAL=%lu IDLE=%d ",
              config.we_pin, config.oe_pin, config.cs_pin, config.mode,
              config.freq, config.xtal_freq, config.idle_behavior);


  Serial.print("ADDRS=");
  for (int i = 0; i < config.address_count; ++i) {
    Serial.print(config.address_pins[i]);
    if (i != config.address_count - 1) Serial.print(",");
  }
  Serial.print(" DATAS=");
  for (int i = 0; i < config.data_count; ++i) {
    Serial.print(config.data_pins[i]);
    if (i != config.data_count - 1) Serial.print(",");
  }

  Serial.print(" NAME=");
  Serial.println(config.chip_name);
}


void setup() {
  Serial.begin(2000000);
  pinMode(RUNNING_PIN, OUTPUT);
  digitalWrite(RUNNING_PIN, LOW);
  pinMode(EXT_CLK_PIN, OUTPUT);
  load_config_from_eeprom();
  enter_idle_mode();
  Serial.println("BOOTED");
}

FASTRUN void loop() {
  static String serial_buffer;

  if (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      serial_buffer.trim();
      if (serial_buffer == "START" && state == IDLE) {
        enter_running_mode();
      } else if (serial_buffer == "STOP" && state == RUNNING) {
        enter_idle_mode();
      } else if (serial_buffer.startsWith("CFG") && state == IDLE) {
        parse_config_line(serial_buffer);
      } else if (serial_buffer == "GETCFG") {
        handle_get_config();
      } else if (serial_buffer == "RESET_CPU") {
        pinMode(RESET_CPU_PIN, OUTPUT);
        digitalWriteFast(RESET_CPU_PIN, LOW);
        delay(100);
        pinMode(RESET_CPU_PIN, INPUT);
        send_ack("ACK RESET_CPU");
      } else if (serial_buffer.startsWith("STEP_")) {
        int steps = serial_buffer.substring(5).toInt();
        if (steps <= 0) steps = 1;
      
        // --- Enter STEP MODE (simulate running)
        digitalWriteFast(RUNNING_PIN, HIGH);
      
        if (config.we_pin != (uint8_t)-1) {
          pinMode(config.we_pin, INPUT);
          attachInterrupt(digitalPinToInterrupt(config.we_pin), handle_memory_access, FALLING);
        }
        if (config.oe_pin != (uint8_t)-1) {
          pinMode(config.oe_pin, INPUT);
          attachInterrupt(digitalPinToInterrupt(config.oe_pin), handle_memory_access, FALLING);
        }
      
        // --- Pulse EXT_CLK
        
        unsigned long half_period_us = 500000UL / config.freq;
      
        for (int i = 0; i < steps; ++i) {
          digitalWriteFast(EXT_CLK_PIN, HIGH);
          delayMicroseconds(half_period_us);
          digitalWriteFast(EXT_CLK_PIN, LOW);
          delayMicroseconds(half_period_us);
        }
      
        // --- Cleanup: detach interrupts and set idle state
        if (config.we_pin != (uint8_t)-1) {
          detachInterrupt(digitalPinToInterrupt(config.we_pin));
        }
        if (config.oe_pin != (uint8_t)-1) {
          detachInterrupt(digitalPinToInterrupt(config.oe_pin));
        }

        uint64_t marker = 0xFFFFFFFFFFFFFFFFULL;
        Serial.write((uint8_t*)&marker, 8);  // 👈 Send end-of-step marker packet

        digitalWriteFast(RUNNING_PIN, LOW);
        delay(100);
        send_ack("STEPS DONE");
      } else if (serial_buffer == "READALL"  && state == IDLE) {
          read_memory();
          send_ack("READ DONE");
      } else if (serial_buffer == "WRITEALL" && state == IDLE) {
        write_memory_from_serial();
      } else {
        Serial.println("Unknown command:");
        Serial.println(serial_buffer);
      }
      serial_buffer = "";
    } else {
      serial_buffer += ch;
    }
  }

  // if (state == RUNNING && dataset_ready) {
  //   Serial.write((uint8_t*)gpio_data, 8);
  //   dataset_ready = false;
  // }

  if (state == RUNNING && config.mode == CONTINUOUS_MODE && Serial.availableForWrite() >= 8) {
    uint32_t gpio6 = GPIO6_DR;
    uint32_t gpio7 = GPIO7_DR;
    Serial.write((uint8_t*)&gpio6, 4);
    Serial.write((uint8_t*)&gpio7, 4);
}
}
