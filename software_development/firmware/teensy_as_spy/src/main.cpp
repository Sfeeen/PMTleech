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
  RunMode mode;
  uint32_t freq;
  char chip_name[30];
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
    // Default values
    config.we_pin = 10;
    config.oe_pin = 12;
    config.mode = INTERRUPT_MODE;
    config.freq = 500000;
    strncpy(config.chip_name, "AT28C64B", sizeof(config.chip_name) - 1);
    config.chip_name[sizeof(config.chip_name) - 1] = '\0';
  }
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
  } else {
    //sample_timer.end();
  }


  analogWrite(EXT_CLK_PIN, 0);      // Optional: reduces duty to 0%
  pinMode(EXT_CLK_PIN, OUTPUT);     // Force pin as GPIO output
  digitalWriteFast(EXT_CLK_PIN, LOW); // Drive LOW


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
  int name = line.indexOf("NAME=");

  if (we != -1) config.we_pin = line.substring(we + 3).toInt();
  if (oe != -1) config.oe_pin = line.substring(oe + 3).toInt();
  if (mode != -1) config.mode = (RunMode)line.substring(mode + 5).toInt();
  if (freq != -1) config.freq = line.substring(freq + 5).toInt();
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
  char buf[128];
  snprintf(buf, sizeof(buf), "WE=%d OE=%d MODE=%d FREQ=%lu NAME=%s",
           config.we_pin, config.oe_pin, (int)config.mode, config.freq, config.chip_name);
  send_ack(buf);
}

void setup() {
  Serial.begin(2000000);
  pinMode(RUNNING_PIN, OUTPUT);
  digitalWrite(RUNNING_PIN, LOW);
  pinMode(EXT_CLK_PIN, OUTPUT);
  digitalWrite(EXT_CLK_PIN, LOW);
  load_config_from_eeprom();
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
      
        digitalWriteFast(RUNNING_PIN, LOW);
        send_ack("STEPS DONE");
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
