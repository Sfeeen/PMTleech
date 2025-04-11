#include <SD.h>
#include <SPI.h>

#define SRAM_6 23
#define SRAM_7 22
#define SRAM_8 21
#define SRAM_9 20
#define SRAM_10 19
#define SRAM_11 18
#define SRAM_12 17
#define SRAM_13 16
#define SRAM_14 15
#define SRAM_15 14
#define SRAM_16 13
#define SRAM_17 41
#define SRAM_18 40
#define SRAM_19 39
#define SRAM_20 38
#define SRAM_21 37
#define SRAM_22 36
#define SRAM_23 35
#define SRAM_25 34
#define SRAM_26 33
#define SRAM_27 32
#define SRAM_28 31
#define SRAM_29 30
#define SRAM_30 29
#define SRAM_31 28
#define SRAM_32 27
#define SRAM_33 26
#define SRAM_34 25
#define SRAM_35 24
#define SRAM_36 12
#define SRAM_37 11
#define SRAM_39 10
#define SRAM_40 9
#define SRAM_41 8
#define SRAM_42 7
#define SRAM_43 6
#define SRAM_44 5

#define GreenLed 2
#define OrangeLed 3
#define RedLed 4

//MK4116N
#define SRAM_A0 SRAM_21
#define SRAM_A1 SRAM_23
#define SRAM_A2 SRAM_22
#define SRAM_A3 SRAM_28
#define SRAM_A4 SRAM_27
#define SRAM_A5 SRAM_26
#define SRAM_A6 SRAM_14

#define SRAM_DOUT SRAM_30
#define SRAM_DIN SRAM_18
#define SRAM_N_RAS SRAM_20
#define SRAM_N_WRITE SRAM_19
#define SRAM_N_CAS SRAM_31


#define DELAY_US 1

#define ADDRESS_PIN_COUNT 7
int address_pins[ADDRESS_PIN_COUNT] = { SRAM_A0, SRAM_A1, SRAM_A2, SRAM_A3, SRAM_A4, SRAM_A5, SRAM_A6};
#define DATA_PIN_COUNT 1
int data_pins[DATA_PIN_COUNT] = { SRAM_DOUT };

uint16_t address_space = pow(2, ADDRESS_PIN_COUNT);

bool action_OK = false;

bool fout = false;

void verifydump_ledreset()
{
	digitalWrite(GreenLed, LOW);
	digitalWrite(RedLed, LOW);
	action_OK = false;
}

void set_default_signals() {
	for (int i = 0; i < 8; i++) {
		pinMode(address_pins[i], OUTPUT);
		digitalWrite(address_pins[i], LOW);
	}

	pinMode(SRAM_N_RAS, OUTPUT);
	pinMode(SRAM_N_CAS, OUTPUT);
	pinMode(SRAM_DOUT, INPUT);
	pinMode(SRAM_DIN, OUTPUT);

	digitalWrite(SRAM_N_RAS, LOW);
	digitalWrite(SRAM_N_RAS, HIGH);
	
}


void setBus(unsigned int a) {
	for (int i = 0; i < ADDRESS_PIN_COUNT; i++) { //cambiato con bus_size piccolo
		if (bitRead(a, i) == 1) {
			digitalWrite(address_pins[i], HIGH);
		}
		else {
			digitalWrite(address_pins[i], LOW);
		}
	}
}

int delayus = 5;
//int delayus = 0;

void writeAddress(unsigned int r, unsigned int c, bool v) {
	/* row */
	setBus(r);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_RAS, LOW);
	delayMicroseconds(delayus);
	/* rw */
	digitalWrite(SRAM_N_WRITE, LOW);
	delayMicroseconds(delayus);
	/* val */
	digitalWrite(SRAM_DIN, v);
	delayMicroseconds(delayus);
	/* col */
	setBus(c);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_CAS, LOW);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_WRITE, HIGH);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_CAS, HIGH);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_RAS, HIGH);
	delayMicroseconds(delayus);
}

int readAddress(unsigned int r, unsigned int c) {
	int ret = 0;

	/* row */
	setBus(r);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_RAS, LOW);
	delayMicroseconds(delayus);
	/* col */
	setBus(c);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_CAS, LOW);
	delayMicroseconds(delayus);

	/* get current value */
	ret = digitalRead(SRAM_DOUT);
	delayMicroseconds(delayus);

	digitalWrite(SRAM_N_CAS, HIGH);
	delayMicroseconds(delayus);
	digitalWrite(SRAM_N_RAS, HIGH);
	delayMicroseconds(delayus);
	return ret;
}

void fill(bool v, bool rd) {
	int r, c;
	for (c = 0; c < (1 << ADDRESS_PIN_COUNT); c++) {
		for (r = 0; r < (1 << ADDRESS_PIN_COUNT); r++) {
			writeAddress(r, c, v);
			if (rd) {
				if (v != readAddress(r, c)) {
					fout = true;
					Serial.print("row: ");
					Serial.println(r);
					Serial.print(" column: ");
					Serial.println(c);
					return;
				}
			}
		}
	}
}


void fillx(bool v) {
	int r, c;
	v %= 1;
	for (c = 0; c < (1 << ADDRESS_PIN_COUNT); c++) {
		for (r = 0; r < (1 << ADDRESS_PIN_COUNT); r++) {
			writeAddress(r, c, v);
			if (v != readAddress(r, c)) {
				fout = true;
				return;
			}
			v ^= 1;
		}
	}
}

void readonly(int v) {
	int r, c;
	for (c = 0; c < (1 << ADDRESS_PIN_COUNT); c++) {
		for (r = 0; r < (1 << ADDRESS_PIN_COUNT); r++) {
			if (v != readAddress(r, c)) {
				fout = true;
				Serial.print("row: ");
				Serial.println(r);
				Serial.print(" column: ");
				Serial.println(c);
				return;
			}
		}
	}
}

void startTest() {
	set_default_signals();
	digitalWrite(OrangeLed, HIGH);
	digitalWrite(RedLed, LOW);
	digitalWrite(GreenLed, LOW);

	bool totalfault = false;

	if (!totalfault) {
		fillx(0);
		if (fout) {
			totalfault = true;
			Serial.println("fillx(0) -> error");
			fout = false;
		}
		else {
			Serial.println("fillx(0) -> OK");
		}
	}

	if (!totalfault) {
		fillx(1);
		if (fout) {
			totalfault = true;
			Serial.println("fillx(1) -> error");
			fout = false;
		}
		else {
			Serial.println("fillx(1) -> OK");
		}
	}

	if (!totalfault) {
		fill(1, false);
		if (fout) {
			totalfault = true;
			Serial.println("fill(1,false); -> error");
			fout = false;
		}
		else {
			Serial.println("fill(1,false); -> OK");
		}
	}

	if (!totalfault) {
		fill(1, true);
		if (fout) {
			totalfault = true;
			Serial.println("fill(1,true); -> error");
			fout = false;
		}
		else {

			Serial.println("fill(1,true); -> OK");
		}
	}

	if (!totalfault) {
		readonly(1);
		if (fout) {
			totalfault = true;
			Serial.println("readonly(1); -> error");
			fout = false;
		}
		else {
			Serial.println("readonly(1); -> OK");
		}
	}

	digitalWrite(OrangeLed, LOW);
	if (totalfault) {
		digitalWrite(RedLed, HIGH);
	}
	else {
		digitalWrite(GreenLed, HIGH);
	}

	delay(3000);

}

void setup()
{
	Serial.begin(115200);
	Serial.setTimeout(990000);
	//while (!Serial) {}
	delay(100);
	Serial.println("started!");

	set_default_signals();

  pinMode(GreenLed, OUTPUT);
  pinMode(OrangeLed, OUTPUT);
  pinMode(RedLed, OUTPUT);
}

void print_menu() {
	Serial.println("");
	Serial.println("---------------------------------");
	Serial.println("--------- ABC SRAM READER -------");
	Serial.println("---------------------------------");
	Serial.println("");
	Serial.println("Current layout: MK4116-2 (7 address bits, 1 data bits)");
	Serial.println("");
	Serial.println("\t 1: Read SRAM to console");
	Serial.println("\t 2: (not implemented) Dump SRAM to SD-card");
	Serial.println("\t 3: (not implemented) Verify dump with SRAM");
	Serial.println("\t 4: Fill SRAM with value");
	Serial.println("\t 5: Fill single address with value (10ms write time)");
	Serial.println("");
	Serial.println("---------------------------------");
	Serial.println("Take your pick > ");
 }

void do_terminal_commands() {
	print_menu();
	while (Serial.available() > 0) {}     //wait for data available
	String line = Serial.readStringUntil('\n').trim();  //read until timeout


		byte var = String(line.charAt(0)).toInt();
		switch (var) {
		case 1:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
	read_memory_to_serial(8);
      digitalWrite(OrangeLed, LOW);


			break;
		case 4:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			fill_memory_command();
      digitalWrite(OrangeLed, LOW);

		
			break;
		case 5:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			fill_single_address();
      digitalWrite(OrangeLed, LOW);
      if(action_OK == true)
        digitalWrite(GreenLed, HIGH);

      else
        digitalWrite(RedLed, HIGH);

		
			break;

		default:
			Serial.print("'");
			Serial.print(line);
			Serial.print("'");
			Serial.print(" is not a number or is bigger then 5!");
			break;
		}

	
}

void loop()
{
	//set_default_signals();
	//set_all_high();
	//delay(100);
	// 
	// 
	//do_terminal_commands();

	//write_bit(5, 0);

	//byte read = read_bit(5);



	//byte read = readshit();
	//if (read)Serial.print("1");
	//else Serial.print("0");

	//delayNanoseconds(100);

	//Serial.println("fill(0,true);");
	//digitalWrite(RedLed, HIGH);
	//fill(0, true);
	//if (fout) {
	//	Serial.println("fill(0,true); -> error");
	//	fout = false;
	//}
	//else {
	//	Serial.println("fill(0,true); -> OK");
	//}

	//Serial.println("fill(1,true);");
	//fill(1, true);
	//if (fout) {
	//	Serial.println("fill(1,true); -> error");
	//	fout = false;
	//}
	//else {
	//	Serial.println("fill(1,true); -> OK");
	//}

	//delay(1000);

	startTest();



}

void fill_memory_command() {
	Serial.println("Enter a ASCII letter to fill the memory, empty = use a count value for each address");
	while (Serial.available() > 0) {}     //wait for data available
	String line = Serial.readStringUntil('\n').trim();  //read until timeout

	if (line.length() != 0) {
		byte filler = line.charAt(0);
		Serial.print("Filling memory with value DEC: ");
		Serial.print(filler);
		Serial.print(" | HEX: ");
		Serial.println(filler, HEX);
		fill_memory(filler);
	}
	else {
		Serial.println("Filling memory with 0");
		fill_memory(0);
	}
	
}

void fill_single_address() {
	Serial.print("Enter address number between 0 - ");
	Serial.print(address_space);
	Serial.println(" | empty = 0");
	while (Serial.available() > 0) {}     //wait for data available
	String line = Serial.readStringUntil('\n').trim();  //read until timeout
	int write_address = line.toInt();

	Serial.print("Chosen address: ");
	Serial.print(write_address);
	Serial.print(" | hex: 0x");
	Serial.println(write_address, HEX);

	Serial.println("Enter a ASCII letter to fill the memory, empty = 0");
	while (Serial.available() > 0) {}     //wait for data available
	line = Serial.readStringUntil('\n').trim();  //read until timeout

	byte filler = 0;
	if (line.length() != 0)filler = line.charAt(0);

	Serial.print("Filling memory with value : ");
	Serial.println(bool(filler));

	write_bit_long_wait(write_address, filler);
	Serial.println("Writing memory done!");
	Serial.println("Reading memory as check!");

	byte read_data = read_bit(write_address);

	if (read_data == filler){
    Serial.println("Read data matches written data!");
    action_OK = true;
  }
	else {
		Serial.print("Error: read data does not match write data, read hex: ");
		Serial.println(read_data, HEX);
	}

	
}

void fill_memory(bool data) {
	//Serial.print("Filling memory with value: ");
	//Serial.println(data, HEX);
	for (int adr = 0; adr < address_space; adr++) {
		write_bit(adr, data);
	}
	Serial.print("Filling memory done!");
}

void read_memory_to_serial(byte bytes_per_row) {

	for (uint16_t adr = 0; adr < address_space; adr++) {
		delay(4); //terminal seems to crash if to fast?
		Serial.print("  ");
		byte read = read_bit(adr);
		if (read)Serial.print("1");
		else Serial.print("0");

		if (adr % bytes_per_row == 0)Serial.println();
	}
}

void write_enable(bool on) {
	digitalWrite(SRAM_N_WRITE, !on);
}

void row_enable(bool on) {
	digitalWriteFast(SRAM_N_RAS, !on);
}

void column_enable(bool on) {
	digitalWriteFast(SRAM_N_CAS, !on);
}

void write_bit(int address, byte data) {
	write_bit_long_wait(address, data);
}

void write_bit_long_wait(int address, byte data) {
	__set_address(address);
	//__write_bit(data);
	delayMicroseconds(1);
	row_enable(true);
	delayMicroseconds(1);
	write_enable(true);
	delayMicroseconds(1);
	__write_bit(data);
	delayMicroseconds(1);
	column_enable(true);
	delayMicroseconds(1);
	//delayNanoseconds(400);
	
	//reset signals
	write_enable(false);
	column_enable(false);
	row_enable(false);
	
}

byte read_bit(int address) {

	__set_address(address);
	delayMicroseconds(1);
	row_enable(true);
	delayMicroseconds(1);
	write_enable(false);
	delayNanoseconds(50);
	delayMicroseconds(1);
	column_enable(true);
	delayMicroseconds(1);
	delayNanoseconds(200);
	byte read_data = __read_bit();

	//write_enable(1); DO NOT DO THIS CORRUPTS DATA OFC

	//Serial.print("Address: ");
	//Serial.print(address);
	//Serial.print(" value: ");
	//Serial.print(read_data, HEX);

	//reset signals
	write_enable(false);
	column_enable(false);
	row_enable(false);


	return read_data;
}
byte readshit(){
		write_enable(false);
		delayNanoseconds(1);
		row_enable(true);
		delayNanoseconds(1);
		column_enable(true);
		delayNanoseconds(60);
		byte read_data = __read_bit();
		delayNanoseconds(5);
		row_enable(false);
		column_enable(false);
		


		return read_data;

}

void __write_bit(byte data) {
	digitalWrite(SRAM_DIN, data);
}

byte __read_bit() {
	byte data = digitalRead(SRAM_DOUT);
	return data;
}

void __set_address(int address) {

	//Serial.print("Address: ");
	//Serial.print(address);
	//Serial.print(" hex: ");
	//Serial.print(address, HEX);
	//Serial.print(" bytes: ");
	//Serial.println(address, BIN);

	for (int i = 0; i < ADDRESS_PIN_COUNT; i++) {

		//Serial.print("Address pin: ");
		//Serial.print(i);
		//Serial.print(" bit: ");
		//Serial.println(bitRead(address, i));

		digitalWrite(address_pins[i], bitRead(address, i));
	}
}


