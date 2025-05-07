#include <Arduino.h>
#include <EEPROM.h>
#include <SD.h>


#define EEPROM_MAGIC 0x42
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_CONFIG_ADDR 1  // start config at address 1

//Special pins:
#define RUNNING_PIN 2
#define EXT_CLK_PIN 3
#define EXTRA_PIN_1 4
#define MEMORY_VCC_CONTROL 5
#define EXTRA_PIN_2 7
#define SERIAL7_RX 28
#define SERIAL7_TX 29
#define DEBUG_SPEED_CLK_ACTIVE_LED 30
#define RESET_CPU_PIN 31 
#define EXTRA_PIN_4 37
#define EXTRA_PIN_3 36
#define HIGH_SPEED_CLK_ACTIVE_LED 33

#define MEMORY_CHIP_WRITE_DELAY_MS 5 // Needs a lot time ms vs us because we drive signals through 330R resistors!! (min. value = 5ms), maybe better without leds on WE signal?
#define MEMORY_CHIP_READ_DELAY_US 10



enum RunMode : uint8_t {
  INTERRUPT_MODE = 0,
  CONTINUOUS_MODE = 1
};

enum State : uint8_t {
  IDLE = 0,
  RUNNING = 1,
  EMULATING = 2
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

EXTMEM uint8_t psram_buffer[1 << 18];  // 2^18 = 262144 bytes = 256 KB


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

void send_pc(char type, const String& message) {
  // Calculate total payload size: type + message
  uint16_t payload_len = 1 + message.length();  // 1 byte for type char

  // Send header
  Serial.write('>');  // Start of message
  Serial.write((payload_len >> 8) & 0xFF);  // High byte
  Serial.write(payload_len & 0xFF);         // Low byte

  // Send type + message
  Serial.write(type);           // Type as single byte
  Serial.print(message);        // Message content as ASCII
}

void send_pc(char type, const uint8_t* data, uint16_t len) {
  uint16_t payload_len = 1 + len;

  // Send header
  Serial.write('>');
  Serial.write((payload_len >> 8) & 0xFF);
  Serial.write(payload_len & 0xFF);

  // Send type and raw data
  Serial.write(type);
  Serial.write(data, len);
}

void load_config_from_eeprom() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_CONFIG_ADDR, config);
  } else {

    send_pc('I', "Using default values!");

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

bool setup_for_reading_writing_chip(bool check_we_pin){

  if(check_we_pin && (config.we_pin == (uint8_t)-1)){
    send_pc('E', "Error WE PIN");
    return false;
  }

  if(config.cs_pin == (uint8_t)-1){
    send_pc('E', "Error CS PIN");
    return false;
  }

  if(config.oe_pin == (uint8_t)-1){
    send_pc('E', "Error OE PIN");
    return false;
  }

  pinMode(config.cs_pin, OUTPUT);
  pinMode(config.oe_pin, OUTPUT);
  pinMode(config.we_pin, OUTPUT);

  // Make sure we cannot write accidently...
  digitalWriteFast(config.cs_pin, HIGH);
  digitalWriteFast(config.we_pin, HIGH);
  digitalWriteFast(config.oe_pin, HIGH);  

  digitalWrite(MEMORY_VCC_CONTROL, LOW); // VCC = ON -> power the chip
  
  digitalWriteFast(config.cs_pin, LOW);

  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], OUTPUT);

  return true;
}

void set_memory_address(uint32_t addr){
  for (uint8_t j = 0; j < config.address_count; ++j)
    digitalWriteFast(config.address_pins[j], (addr >> j) & 1); // set address lines
}

void set_memory_value(uint8_t value){
  for (uint8_t j = 0; j < config.data_count; ++j)
    digitalWriteFast(config.data_pins[j], (value >> j) & 1); // set data lines
}

uint8_t read_memory_value(){
  uint8_t data = 0;
  for (uint8_t i = 0; i < config.data_count; ++i)
    if (digitalReadFast(config.data_pins[i])) data |= (1 << i);

  return data;
}

void close_memory_power_and_set_pins_high_impedance(){
  digitalWriteFast(config.we_pin, HIGH); // disable chip writing
  digitalWriteFast(config.oe_pin, HIGH); // disable data output
  digitalWriteFast(config.cs_pin, HIGH); // disable chip entirely

  //set all pins high impedance
  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], INPUT);
  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], INPUT);

  delay(50); // CRUCIAL OTHERWISE WRITES FAIL

  digitalWriteFast(MEMORY_VCC_CONTROL, HIGH); // stop powering the chip

  pinMode(config.oe_pin, INPUT);
  pinMode(config.we_pin, INPUT);
  pinMode(config.cs_pin, INPUT);
}

uint8_t read_single_address(uint32_t addr){
  // Serial.print("Reading address ");
  // Serial.print(String(addr));

  if(!setup_for_reading_writing_chip(true))return;

  set_memory_address(addr);

  digitalWriteFast(config.oe_pin, LOW); // enable reading to chip
  delayMicroseconds(MEMORY_CHIP_READ_DELAY_US);
  // delay(1000);
  uint8_t value = read_memory_value();

  close_memory_power_and_set_pins_high_impedance();

  // Serial.print(" value: ");
  // Serial.println(value);

  return value;
}

void read_partial_memory(uint32_t start_addr, uint32_t length) {
  const size_t MAX_DATA_PER_PACKET = 500;
  uint32_t max_address = (1UL << config.address_count);

  // Default to full memory size if length == 0
  if (length == 0) {
    length = max_address - start_addr;
  }

  if (start_addr + length > max_address) {
    send_pc('I', "ERROR: Out of bounds");
    return;
  }

  if(!setup_for_reading_writing_chip(true))return;

  digitalWriteFast(config.oe_pin, LOW); // enable reading from chip

  uint32_t addr = start_addr;
  while (addr < start_addr + length) {
    size_t chunk_len = min(MAX_DATA_PER_PACKET, (start_addr + length) - addr);

    // Compose message into a temporary buffer
    uint8_t buf[5 + MAX_DATA_PER_PACKET]; // 4 for addr, 1 for '.', then data
    buf[0] = (addr >> 24) & 0xFF;
    buf[1] = (addr >> 16) & 0xFF;
    buf[2] = (addr >> 8) & 0xFF;
    buf[3] = (addr >> 0) & 0xFF;

    for (size_t i = 0; i < chunk_len; ++i) {
      uint32_t curr_addr = addr + i;
      set_memory_address(curr_addr);
      delayMicroseconds(MEMORY_CHIP_READ_DELAY_US);
      uint8_t value = read_memory_value();
      Serial.println(value);
      buf[4 + i] = value;
    }

    send_pc('R', buf, 4 + chunk_len);
    addr += chunk_len;
  }

  close_memory_power_and_set_pins_high_impedance();

  char buf[64];
  snprintf(buf, sizeof(buf), "done reading %lu bytes from address 0x%lX", length, start_addr);
  send_pc('I', String(buf));
}

void write_single_address(uint32_t addr, const uint8_t value){
  // Serial.print("Writing address ");
  // Serial.print(String(addr));
  // Serial.print(" value: ");
  // Serial.println(String(value));

  if(!setup_for_reading_writing_chip(true))return;

  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], OUTPUT);

  set_memory_address(addr);
  set_memory_value(value);

  digitalWriteFast(config.we_pin, LOW); // enable writing to chip
  delay(MEMORY_CHIP_WRITE_DELAY_MS);  // give a good 10ms to chip so write has certainly happenned // Needs a lot time ms vs us because we drive signals through 330R resistors!!
  close_memory_power_and_set_pins_high_impedance();
}

void write_partial_memory(uint32_t start_addr, const uint8_t* data, uint32_t length) {
  uint32_t max_address = (1UL << config.address_count);

  if (start_addr + length > max_address) {
    send_pc('I', "ERROR: Write out of bounds");
    return;
  }

  if(!setup_for_reading_writing_chip(true))return;

  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], OUTPUT);

  for (uint32_t i = 0; i < length; ++i) {
    uint32_t curr_addr = start_addr + i;
    uint8_t value = data[i];

    set_memory_address(curr_addr);
    //set_memory_value(value);

    digitalWriteFast(config.we_pin, LOW); // enable writing to chip
    set_memory_value(value);
    delay(MEMORY_CHIP_WRITE_DELAY_MS);  // give a good 10ms to chip so write has certainly happenned // Needs a lot time ms vs us because we drive signals through 330R resistors!!
    digitalWriteFast(config.we_pin, HIGH);
    //delay(MEMORY_CHIP_WRITE_DELAY_MS);
  }

  close_memory_power_and_set_pins_high_impedance();

  char msg[64];
  snprintf(msg, sizeof(msg), "WROTE %lu bytes to address 0x%lX", length, start_addr);
  send_pc('I', String(msg));
}

void dump_memory_to_sd() {
  if (!SD.begin(BUILTIN_SDCARD)) {
    send_pc('I', "ERROR: No SD card or init failed.");
    return;
  }

  char filename[40];
  snprintf(filename, sizeof(filename), "/%s.bin", config.chip_name);

  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    send_pc('I', "ERROR: Could not create file.");
    return;
  }

  uint32_t max_address = (1UL << config.address_count);

  // Set WE pin HIGH before enabling memory power
  if (config.we_pin != (uint8_t)-1) {
    pinMode(config.we_pin, OUTPUT);
    digitalWriteFast(config.we_pin, HIGH);
  }

  pinMode(config.cs_pin, OUTPUT);
  digitalWriteFast(config.cs_pin, HIGH);

  digitalWrite(MEMORY_VCC_CONTROL, LOW);

  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], OUTPUT);
  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], INPUT);

  pinMode(config.oe_pin, OUTPUT);
  digitalWriteFast(config.cs_pin, LOW);
  digitalWriteFast(config.oe_pin, LOW);

  const size_t BUF_SIZE = 512;
  uint8_t buffer[BUF_SIZE];
  size_t buf_index = 0;

  for (uint32_t addr = 0; addr < max_address; ++addr) {
    for (uint8_t i = 0; i < config.address_count; ++i)
      digitalWriteFast(config.address_pins[i], (addr >> i) & 1);

    delayMicroseconds(1);  // settling time

    uint8_t data = 0;
    for (uint8_t i = 0; i < config.data_count; ++i)
      if (digitalReadFast(config.data_pins[i])) data |= (1 << i);

    buffer[buf_index++] = data;
    if (buf_index == BUF_SIZE) {
      file.write(buffer, BUF_SIZE);
      buf_index = 0;
    }
  }

  if (buf_index > 0) {
    file.write(buffer, buf_index);
  }

  file.close();

  digitalWriteFast(config.cs_pin, HIGH);
  digitalWriteFast(config.oe_pin, HIGH);
  if (config.we_pin != (uint8_t)-1)
    digitalWriteFast(config.we_pin, HIGH);

  digitalWrite(MEMORY_VCC_CONTROL, HIGH);

  for (uint8_t i = 0; i < config.address_count; ++i)
    pinMode(config.address_pins[i], INPUT);
  for (uint8_t i = 0; i < config.data_count; ++i)
    pinMode(config.data_pins[i], INPUT);

  pinMode(config.cs_pin, INPUT);
  pinMode(config.oe_pin, INPUT);
  if (config.we_pin != (uint8_t)-1)
    pinMode(config.we_pin, INPUT);

  char buf[100];
  snprintf(buf, sizeof(buf), "Dumped %lu bytes to %s", max_address, filename);
  send_pc('I', String(buf));
}

void enter_running_mode() {
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
  digitalWrite(DEBUG_SPEED_CLK_ACTIVE_LED, HIGH);
  digitalWrite(HIGH_SPEED_CLK_ACTIVE_LED, LOW);
  analogWriteFrequency(EXT_CLK_PIN, config.freq);
  analogWrite(EXT_CLK_PIN, 128);  // 50% duty cycle

  digitalWrite(RUNNING_PIN, HIGH);
  state = RUNNING;
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
    digitalWrite(DEBUG_SPEED_CLK_ACTIVE_LED, LOW);
    digitalWrite(HIGH_SPEED_CLK_ACTIVE_LED, HIGH);
    analogWriteFrequency(EXT_CLK_PIN, config.xtal_freq);
    analogWrite(EXT_CLK_PIN, 128); // 50% duty
  } else {
    // ❌ HALT mode — disable clock
    digitalWrite(DEBUG_SPEED_CLK_ACTIVE_LED, LOW);
    digitalWrite(HIGH_SPEED_CLK_ACTIVE_LED, LOW);
    analogWrite(EXT_CLK_PIN, 0);          // Optional: reduces duty to 0%
    digitalWriteFast(EXT_CLK_PIN, LOW);   // Drive LOW
  }

  digitalWrite(RUNNING_PIN, LOW);
  state = IDLE;

  send_pc('I', "ACK IDLE");
}

void enter_emulate_mode() {
  state = EMULATING;

  // Set up address pins as INPUT
  for (uint8_t i = 0; i < config.address_count; ++i) {
    pinMode(config.address_pins[i], INPUT);
  }

  // Set up data pins as OUTPUT
  for (uint8_t i = 0; i < config.data_count; ++i) {
    pinMode(config.data_pins[i], OUTPUT);
  }

  // Disable memory chip power output
  digitalWrite(MEMORY_VCC_CONTROL, HIGH);  // Assume HIGH = off

  digitalWrite(DEBUG_SPEED_CLK_ACTIVE_LED, HIGH);
  digitalWrite(HIGH_SPEED_CLK_ACTIVE_LED, HIGH);
  digitalWrite(RUNNING_PIN, HIGH);

  send_pc('I', "Entered EMULATE mode");
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

    enter_idle_mode();
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
  send_pc('C', "ACK CONFIG OK");
}

void handle_get_config() {
  send_pc('I', "will send");

  String msg;
  msg.reserve(256);  // Optional: pre-allocate to reduce reallocations
  
  msg += "WE=" + String(config.we_pin);
  msg += " OE=" + String(config.oe_pin);
  msg += " CS=" + String(config.cs_pin);
  msg += " MODE=" + String(config.mode);
  msg += " FREQ=" + String(config.freq);
  msg += " XTAL=" + String(config.xtal_freq);
  msg += " IDLE=" + String(config.idle_behavior);
  
  msg += " ADDRS=";
  for (int i = 0; i < config.address_count; ++i) {
    msg += String(config.address_pins[i]);
    if (i != config.address_count - 1) msg += ",";
  }
  
  msg += " DATAS=";
  for (int i = 0; i < config.data_count; ++i) {
    msg += String(config.data_pins[i]);
    if (i != config.data_count - 1) msg += ",";
  }
  
  msg += " NAME=" + String(config.chip_name);
  
  // Send it with message type 'G'
  send_pc('G', msg);
  
}

void setup() {
  Serial.begin(2000000);
  Serial.setTimeout(200);  // <- Set timeout only once here
  while(!Serial);

  for (uint32_t i = 0; i < sizeof(psram_buffer); ++i) {
    psram_buffer[i] = 0xFF;
  }

  pinMode(MEMORY_VCC_CONTROL, OUTPUT);
  digitalWrite(MEMORY_VCC_CONTROL, HIGH);
  pinMode(RUNNING_PIN, OUTPUT);
  digitalWrite(RUNNING_PIN, LOW);
  pinMode(EXT_CLK_PIN, OUTPUT);
  pinMode(HIGH_SPEED_CLK_ACTIVE_LED, OUTPUT);
  pinMode(DEBUG_SPEED_CLK_ACTIVE_LED, OUTPUT);

  if (!SD.begin(BUILTIN_SDCARD)) {
    send_pc('I', "SD init failed!");
  } else {
    send_pc('I', "SD init OK");
  }

  load_config_from_eeprom();
  enter_idle_mode();
  send_pc('I', "PMT Leech BOOTED");

  static uint8_t temp_buf[5];  // Can write up to 1024 bytes

  for (uint32_t i = 0; i < 5; ++i) {
    temp_buf[i] = 6;
  }

  write_partial_memory(0, temp_buf, 5);
  read_partial_memory(0, 5);

  for (uint32_t i = 0; i < 5; ++i) {
    temp_buf[i] = 255;
  }

  write_partial_memory(0, temp_buf, 5);
  read_partial_memory(0, 5);

  for (uint32_t i = 0; i < 5; ++i) {
    temp_buf[i] = 0;
  }

  write_partial_memory(0, temp_buf, 5);
  read_partial_memory(0, 5);

  // for(int i = 0; i < 2000; i++){
  //   write_single_address(0, i);
  //   read_single_address(0);
  // }

}

void handle_command(const String& serial_buffer) {
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
    send_pc('X', "ACK RESET_CPU");
  } else if (serial_buffer.startsWith("STEP_")) {
    int steps = serial_buffer.substring(5).toInt();
    if (steps <= 0) steps = 1;

    digitalWriteFast(RUNNING_PIN, HIGH);
    pinMode(EXT_CLK_PIN, OUTPUT);
    digitalWrite(DEBUG_SPEED_CLK_ACTIVE_LED, HIGH);
    digitalWrite(HIGH_SPEED_CLK_ACTIVE_LED, LOW);

    if (config.we_pin != (uint8_t)-1)
      attachInterrupt(digitalPinToInterrupt(config.we_pin), handle_memory_access, FALLING);
    if (config.oe_pin != (uint8_t)-1)
      attachInterrupt(digitalPinToInterrupt(config.oe_pin), handle_memory_access, FALLING);

    unsigned long half_period_us = 500000UL / config.freq;
    for (int i = 0; i < steps; ++i) {
      digitalWriteFast(EXT_CLK_PIN, HIGH);
      delayMicroseconds(half_period_us);
      digitalWriteFast(EXT_CLK_PIN, LOW);
      delayMicroseconds(half_period_us);
    }

    if (config.we_pin != (uint8_t)-1)
      detachInterrupt(digitalPinToInterrupt(config.we_pin));
    if (config.oe_pin != (uint8_t)-1)
      detachInterrupt(digitalPinToInterrupt(config.oe_pin));

    uint64_t marker = 0xFFFFFFFFFFFFFFFFULL;
    Serial.write((uint8_t*)&marker, 8);
    enter_idle_mode();
    delay(100);
    send_pc('I',"STEPS DONE");

  } else if (serial_buffer.startsWith("READ_") && state == IDLE) {
    int sep1 = serial_buffer.indexOf('_', 5);
    if (sep1 != -1) {
      String addr_str = serial_buffer.substring(5, sep1);
      String len_str = serial_buffer.substring(sep1 + 1);
      uint32_t start_addr = strtoul(addr_str.c_str(), nullptr, 0);
      uint32_t length = strtoul(len_str.c_str(), nullptr, 0);
      read_partial_memory(start_addr, length);
    }
  
  } 
  else if (serial_buffer.startsWith("WRITE_") && state == IDLE) {
    int pos1 = serial_buffer.indexOf('_', 5);
    int pos2 = serial_buffer.indexOf('_', pos1 + 1);
    int pos3 = serial_buffer.indexOf('_', pos2 + 1);
  
    if (pos1 == -1 || pos2 == -1 || pos3 == -1) {
      send_pc('I', "ERROR: Invalid WRITE format");
      return;
    }
  
    String addr_str = serial_buffer.substring(5, pos2);
    String len_str = serial_buffer.substring(pos2 + 1, pos3);
    String hex_str = serial_buffer.substring(pos3 + 1);
  
    uint32_t addr = strtoul(addr_str.c_str(), nullptr, 0);
    uint32_t length = strtoul(len_str.c_str(), nullptr, 0);
  
    if (hex_str.length() != length * 2) {
      send_pc('I', "ERROR: Hex length mismatch");
      return;
    }
  
    static uint8_t temp_buf[1024];  // Can write up to 1024 bytes
    if (length > sizeof(temp_buf)) {
      send_pc('I', "ERROR: Max 1024 bytes");
      return;
    }
  
    for (uint32_t i = 0; i < length; ++i) {
      char byte_str[3] = { hex_str[i * 2], hex_str[i * 2 + 1], 0 };
      temp_buf[i] = strtoul(byte_str, nullptr, 16);
    }
  
    write_partial_memory(addr, temp_buf, length);
  }
  else if (serial_buffer == "DUMP" && state == IDLE) {
    dump_memory_to_sd();
  } 
  else if (serial_buffer == "EMULATE" && state == IDLE) {
    enter_emulate_mode();
  } else {
    send_pc('U', "Unknown command: " + serial_buffer );
  }
}

FASTRUN void loop() {
  static String serial_buffer;
  static bool inside_command = false;

  if (Serial.available()) {
    char start = Serial.read();
    if (start != '<') return;

    // Read 4-character length field
    char len_buf[5] = {0};
    size_t len_read = Serial.readBytes(len_buf, 4);
    if (len_read != 4) {
      send_pc('U', "INVALID: length parse failed");
      return;
    }

    uint16_t payload_len = strtoul(len_buf, nullptr, 16);
    if (payload_len == 0 || payload_len > 1024) {
      send_pc('U', "INVALID: bad length " + String(payload_len));
      return;
    }

    String serial_buffer;
    serial_buffer.reserve(payload_len);
    for (uint16_t i = 0; i < payload_len; ++i) {
      int ch = Serial.read();
      if (ch == -1) {
        send_pc('U', "TIMEOUT: only " + String(i) + " of " + String(payload_len) + " bytes received");
        return;
      }
      serial_buffer += (char)ch;
    }

    serial_buffer.trim();
    handle_command(serial_buffer);
  }

  if (state == RUNNING && config.mode == CONTINUOUS_MODE && Serial.availableForWrite() >= 8) {
    uint32_t gpio6 = GPIO6_DR;
    uint32_t gpio7 = GPIO7_DR;
    Serial.write((uint8_t*)&gpio6, 4);
    Serial.write((uint8_t*)&gpio7, 4);
  }

  static uint32_t last_addr = 0xFFFFFFFF;

  if (state == EMULATING) {
    uint32_t addr = 0;
    for (uint8_t i = 0; i < config.address_count; ++i) {
      addr |= digitalReadFast(config.address_pins[i]) << i;
    }
  
    if (addr != last_addr) {
      last_addr = addr;
  
      // Direct lookup without bounds check — safe as max address is (1 << address_count)
      uint8_t value = psram_buffer[addr];
  
      for (uint8_t i = 0; i < config.data_count; ++i) {
        digitalWriteFast(config.data_pins[i], (value >> i) & 1);
      }
    }
  }
  

}
