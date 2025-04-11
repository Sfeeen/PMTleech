#include <Arduino.h>
#include "pins.h"

const unsigned int delay_us = 5; //125 spy faalt //6147  read write faalt, read is the culprit                                     // Add a delay prior to/after a writing and other operations to ensure correct behavior (in micro-seconds)


const int nr_of_pins = 28;                                                   
const int nr_of_CS = 1;                                                     
const int nr_of_A    = 13;                                                     
const int nr_of_IO   = 8;    

uint16_t address_space = pow(2, nr_of_A);

bool OE_active_high = false;                                                    
bool WE_active_high = false;                                                   
bool CS_active_high = false;                                         

int mapped_CS_pins[nr_of_CS] = {N_CE};
int mapped_A_pins[nr_of_A] = {A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12};
int mapped_IO_pins[nr_of_IO] = {D1, D2, D3, D4, D5, D6, D7, D8};
int mapped_OE_pins[1] = {N_OE};
int mapped_WE_pins[1] = {N_WE}; 


void set_pinmodes();
void read_all();
void fill_all();
void enable_output(bool OE_active);
void write_enable(bool WE_active);
void chip_enable(bool CS_active);
byte read_byte(int address);
void write_byte(int address, byte data);
void set_address(int address);
void PrintHex16(uint16_t* data, uint8_t length);
void printBinary(byte val);
void delay_cycles(uint32_t cycles);


void setup() {
  Serial.begin(115200);
  set_pinmodes();

  chip_enable(1);
  enable_output(0);                                                     // Do not enable reading
  write_enable(0);  

  delay(15);
  write_byte(2730, 85);
  delay(15);
  write_byte(5461, 170);
  delay(15);


  // 55 01010101
  // 8D 10001101
  // 85 10000101
  // E4 11100100

//   #define T_GPIO2_28 35 gpio 7
// #define T_GPIO2_29 34

 //53 65 45 4e
 write_byte(1, 0x53); // 01010011 // 55 01010101 /2 3  D8 staat op D2, D7 staat op D3, D6 staat op D4, D4 staat op D6, D3 staat op D8, D2 staat op D7 876432 234687
 delay(15);
 write_byte(2, 0x56); // 01100101 // 8D 10001101 /876432
 delay(15);
 write_byte(3, 0x45); // 01000101 // 85 10000101 /234687
 delay(15);
 write_byte(4, 0x4e); // 01001110 // E4 11100100
 delay(15);


}

// byte counter = 0;
// void loop() {
//   counter++;
//   write_byte(1123, counter);
//   delay(7);
//   byte data = read_byte(1123);
//   if(data != counter){
//       Serial.println("BAD READ");
//       printBinary(counter);
//       printBinary(data);
//       Serial.print("Diff:    ");
//       printBinary(counter ^ data); // XOR to highlight differences
//       delay(1000);
//   }

// }

int delay_between = 0;
void loop() {
    //53 65 45 4e
  write_byte(1, 0x53);
  // delay(delay_between);
  write_byte(2, 0x56);
  // delay(delay_between);
  write_byte(3, 0x45);
  // delay(delay_between);
  write_byte(4, 0x4e);
  // delay(delay_between);

  byte data = read_byte(1);
  //printBinary(data);
  // delay(delay_between);
  data = read_byte(2);
  //printBinary(data);
  // delay(delay_between);
  data = read_byte(3);
  //printBinary(data);
  // delay(delay_between);
  data = read_byte(4);
  //printBinary(data);

  // byte data = read_byte(12);
  // delay(10);
  // data = read_byte(5461);
  // delay(10);
  // data = read_byte(2730);

  // delay(delay_between);
  
  // printBinary(data);

  // if(data != 85){
  //     Serial.println("BAD READ");
  //     printBinary(data);
  //     delay(1000);
  // }

  // delay(10);

  // data = read_byte(5461);
  // // // printBinary(data);

  // // if(data != 170){
  // //     Serial.println("BAD READ");
  // //     printBinary(data);
  // //     delay(1000);
  // // }
  // delay(10);
}

void printBinary(byte val) {
  Serial.println(val, HEX);
  for (int i = 7; i >= 0; i--) {
    Serial.print(bitRead(val, i)); // Print each bit from MSB to LSB
  }
  Serial.println();
}

void read_all(){
  for (uint16_t address = 0; address <= address_space; address++) {
    if (address % 100 == 0 || address == address_space) {
      PrintHex16(&address, 1);
      Serial.print(" / ");
      PrintHex16(&address_space, 1);
      
      int percent = (100 * address) / address_space;
      Serial.println(" (" + String(percent) + "%)");
    }

    if (address == address_space) break;                                // The final index was increased by 1 and this line was added to show 100% completion
    
    byte data = read_byte(address);
    Serial.println(data);
  }
}

void fill_all(){
    for (int address = 0; address < address_space; address++) {
      write_byte(address, address);
    }
}


void set_pinmodes() {
  
  pinMode(mapped_OE_pins[0], OUTPUT);
  pinMode(mapped_WE_pins[0], OUTPUT);
  for (int i = 0; i < nr_of_CS; i++) {
    pinMode(mapped_CS_pins[i], OUTPUT);   
  } 

  enable_output(0);                                                     // Disable write enable pin
  write_enable(0);                                                      // Disable output enable pin
  chip_enable(0);                                                       // Disable chip select pins
  

  
  for (int i = 0; i < nr_of_A; i++) {
    pinMode(mapped_A_pins[i], OUTPUT);
    digitalWriteFast(mapped_A_pins[i], LOW);
  }

  for (int i = 0; i < nr_of_IO; i++) {
    pinMode(mapped_IO_pins[i], INPUT);
  }
}

void enable_output(bool OE_active) {
  digitalWriteFast(mapped_OE_pins[0], (OE_active_high ? OE_active : !OE_active));
}

void write_enable(bool WE_active) {
  digitalWriteFast(mapped_WE_pins[0], (WE_active_high ? WE_active : !WE_active));
}

void chip_enable(bool CS_active) {
  for (int i = 0; i < nr_of_CS; i++) {
    digitalWriteFast(mapped_CS_pins[i], (CS_active_high ? CS_active : !CS_active));     
  }
}

byte read_byte(int address) {
  byte read_data = 0;
  uint32_t gpio1_value;
  uint32_t gpio2_value;

  for (int i = 0; i < nr_of_IO; i++) {
    pinMode(mapped_IO_pins[i], INPUT);
  }
  
  set_address(address);

  while(1){
    digitalWriteFast(N_OE, LOW);
    delayMicroseconds(delay_us);
    digitalWriteFast(N_OE, HIGH);
    delayMicroseconds(delay_us);
    
  }
  // chip_enable(1);                                                       // Enable chip
  enable_output(1); 
  // delayMicroseconds(1);
  // delay(1);                                                    // Enable reading
  // write_enable(0);                                                      // Do not enable writing

  delayMicroseconds(delay_us); //culprit waits 6ms to set the data
  // delayMicroseconds(delay_us);
  // delay_cycles(30);  // Insert 10 NOP cycles

  // delay(1);

  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");

  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");

  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");
  // __asm__ volatile ("nop");

  
                                                     // Read the data
  // for (int i = 0; i < nr_of_IO; i++) { //400ns
  //   bitWrite(read_data, i, digitalReadFast(mapped_IO_pins[i]));
  // }

      // Read full GPIO register values
      gpio1_value = GPIO6_DR;  // Replace with actual register read function
      gpio2_value = GPIO7_DR;  // Replace with actual register read function
  


  // chip_enable(0);                                                       // Reset signals
  enable_output(0);
  // write_enable(0);

        // Extract bit values based on pin mapping
        read_data |= (gpio1_value & (1 << 2)) ? (1 << 0) : 0;  // D1 -> GPIO1_2
        read_data |= (gpio2_value & (1 << 28)) ? (1 << 1) : 0; // D2 -> GPIO2_28
        read_data |= (gpio2_value & (1 << 29)) ? (1 << 2) : 0; // D3 -> GPIO2_29
        read_data |= (gpio2_value & (1 << 16)) ? (1 << 3) : 0; // D4 -> GPIO2_16
        read_data |= (gpio2_value & (1 << 12)) ? (1 << 4) : 0; // D5 -> GPIO2_12
        read_data |= (gpio2_value & (1 << 11)) ? (1 << 5) : 0; // D6 -> GPIO2_11
        read_data |= (gpio2_value & (1 << 10)) ? (1 << 6) : 0; // D7 -> GPIO2_10
        read_data |= (gpio2_value & (1 << 3)) ? (1 << 7) : 0;  // D8 -> GPIO2_3

  return read_data;
}

void delay_cycles(uint32_t cycles) {
  while (cycles--) {
      __asm__ volatile ("nop");
  }
}

void write_byte(int address, byte data) {
  set_address(address);
  // chip_enable(1);                                                       // Enable chip
  // enable_output(0);                                                     // Do not enable reading
                                                    // Enable writing

  for (int i = 0; i < nr_of_IO; i++) {
    pinMode(mapped_IO_pins[i], OUTPUT);
  }

  for (int i = 0; i < nr_of_IO; i++) {
    digitalWriteFast(mapped_IO_pins[i], bitRead(data, i));
  }

  write_enable(1);    
  
  delayMicroseconds(delay_us);
  // delayMicroseconds(delay_us);
  

  // chip_enable(0);                                                       // Reset signals
  write_enable(0);
}

void set_address(int address) {
  for (int i = 0; i < nr_of_A; i++) {
    digitalWriteFast(mapped_A_pins[i], bitRead(address, i));                // BitRead is used to obtain each individual bit of the address and set the corresponding microcontrolelr pins HIGH/LOW
  }
}

void PrintHex16(uint16_t* data, uint8_t length) {                       // Prints 16-bit data in HEX with leading zeroes
  Serial.print("0x");
  for (int i = 0; i < length; i++) {
    uint8_t MSB = byte(data[i] >> 8);
    uint8_t LSB = byte(data[i]);

    if (MSB < 0x10) Serial.print("0"); 
    Serial.print(MSB, HEX); 
    
    if (LSB < 0x10) Serial.print("0");
    Serial.print(LSB, HEX); 
  }
}


