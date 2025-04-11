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

//HY62256A-10
#define SRAM_A0 SRAM_20
#define SRAM_A1 SRAM_19
#define SRAM_A2 SRAM_18
#define SRAM_A3 SRAM_17
#define SRAM_A4 SRAM_16
#define SRAM_A5 SRAM_15
#define SRAM_A6 SRAM_14
#define SRAM_A7 SRAM_13
#define SRAM_A8 SRAM_35
#define SRAM_A9 SRAM_34
#define SRAM_A10 SRAM_31
#define SRAM_A11 SRAM_33
#define SRAM_A12 SRAM_12
#define SRAM_A13 SRAM_36
#define SRAM_A14 SRAM_11
#define SRAM_IO_01 SRAM_21
#define SRAM_IO_02 SRAM_22
#define SRAM_IO_03 SRAM_23
#define SRAM_IO_04 SRAM_25
#define SRAM_IO_05 SRAM_26
#define SRAM_IO_06 SRAM_27
#define SRAM_IO_07 SRAM_28
#define SRAM_IO_08 SRAM_29
#define SRAM_N_OE SRAM_32
#define SRAM_N_WE SRAM_37
#define SRAM_N_CS SRAM_30


#define DELAY_US 1

#define ADDRESS_PIN_COUNT 15
int address_pins[ADDRESS_PIN_COUNT] = { SRAM_A0, SRAM_A1, SRAM_A2, SRAM_A3, SRAM_A4, SRAM_A5, SRAM_A6, SRAM_A7, SRAM_A8, SRAM_A9, SRAM_A10, SRAM_A11, SRAM_A12, SRAM_A13, SRAM_A14 };
#define DATA_PIN_COUNT 8
int data_pins[DATA_PIN_COUNT] = { SRAM_IO_01, SRAM_IO_02, SRAM_IO_03, SRAM_IO_04, SRAM_IO_05, SRAM_IO_06, SRAM_IO_07, SRAM_IO_08};

uint16_t address_space = pow(2, ADDRESS_PIN_COUNT);

bool action_OK = false;

volatile bool should_perform_spyread = false;
volatile int intteruptcount = 0;



void set_default_signals() {
	for (int i = 0; i < ADDRESS_PIN_COUNT; i++) {
		pinMode(address_pins[i], OUTPUT);
		digitalWrite(address_pins[i], LOW);
	}

	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		pinMode(data_pins[i], INPUT);
	}

	pinMode(SRAM_N_CS, OUTPUT);
	digitalWrite(SRAM_N_CS, HIGH);

	pinMode(SRAM_N_OE, OUTPUT);
	digitalWrite(SRAM_N_OE, HIGH);

	pinMode(SRAM_N_WE, OUTPUT);
	digitalWrite(SRAM_N_WE, HIGH);

	chip_enable(0);
	enable_output(0);
	write_enable(0); 
}

void set_all_input() {
	for (int i = 0; i < ADDRESS_PIN_COUNT; i++) {
		pinMode(address_pins[i], INPUT);
	}

	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		pinMode(data_pins[i], INPUT);
	}

	pinMode(SRAM_N_OE, INPUT);
	pinMode(SRAM_N_CS, INPUT);
	pinMode(SRAM_N_WE, INPUT);

}



void setup()
{
	Serial.begin(115200);
	Serial.setTimeout(990000);
	//while (!Serial) {}
	delay(100);
	Serial.println("started!");

	//set_all_input();
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
	Serial.println("Current layout: HY62256A-10 (15 address bits, 8 data bits)");
	Serial.println("");
	Serial.println("\t 1: Read SRAM to console");
	Serial.println("\t 2: Dump SRAM to SD-card");
	Serial.println("\t 3: Verify dump with SRAM");
	Serial.println("\t 4: Fill SRAM with value");
	Serial.println("\t 5: Fill single address with value (10ms write time)");
	Serial.println("\t 6: Load file into RAM!");
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
			//set_all_input();

			break;
		case 2:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			read_memory_to_SD();
      digitalWrite(OrangeLed, LOW);
      if(action_OK == true)
        digitalWrite(GreenLed, HIGH);

      else
        digitalWrite(RedLed, HIGH);
			//set_all_input();

			break;
		case 3:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			verify_dump();
      digitalWrite(OrangeLed, LOW);
      if(action_OK == true)
        digitalWrite(GreenLed, HIGH);

      else
        digitalWrite(RedLed, HIGH);
			//set_all_input();
	
			break;
		case 4:
			//set_default_signals();
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			fill_memory_command();
      digitalWrite(OrangeLed, LOW);
			//set_all_input();
		
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
			//set_all_input();
		
			break;
		case 6:
      verifydump_ledreset();
      digitalWrite(OrangeLed, HIGH);
			//ram_spy();
	  load_file_into_ram();
      digitalWrite(OrangeLed, LOW);
	
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
	do_terminal_commands();
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
		Serial.println("Filling memory with cyclic counting data");
		fill_memory();
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

	byte filler = 0x00;
	if (line.length() != 0)filler = line.charAt(0);

	Serial.print("Filling memory with value DEC: ");
	Serial.print(filler);
	Serial.print(" | HEX: ");
	Serial.println(filler, HEX);

	write_byte_long_wait(write_address, filler);
	Serial.println("Writing memory done!");
	Serial.println("Reading memory as check!");

	byte read_data = read_byte(write_address);

	if (read_data == filler){
    Serial.println("Read data matches written data!");
    action_OK = true;
  }
	else {
		Serial.print("Error: read data does not match write data, read hex: ");
		Serial.println(read_data, HEX);
	}

	
}

void fill_memory(byte data) {
	//Serial.print("Filling memory with value: ");
	//Serial.println(data, HEX);
	for (int adr = 0; adr < address_space; adr++) {
		write_byte(adr, data);
	}
	Serial.print("Filling memory done!");
}

void fill_memory() {
	Serial.print("Filling memory with address values (cyclic count data)");

	for (int adr = 0; adr < address_space; adr++) {
		write_byte(adr, adr);
	}

	Serial.print("Filling memory done!");
}

void PrintHex16(uint16_t* data, uint8_t length) // prints 16-bit data in hex with leading zeroes
{
	Serial.print("0x");
	for (int i = 0; i < length; i++)
	{
		uint8_t MSB = byte(data[i] >> 8);
		uint8_t LSB = byte(data[i]);

		if (MSB < 0x10) { Serial.print("0"); } Serial.print(MSB, HEX); 
		if (LSB < 0x10) { Serial.print("0"); } Serial.print(LSB, HEX); 
	}
}

void PrintHex8(uint8_t* data, uint8_t length) // prints 8-bit data in hex with leading zeroes
{
	Serial.print("0x");
	for (int i = 0; i < length; i++) {
		if (data[i] < 0x10) { Serial.print("0"); }
		Serial.print(data[i], HEX);
	}
}

void read_memory_to_serial(byte bytes_per_row) {

	char blist[bytes_per_row];

	//for (uint16_t adr = 0; adr < address_space; adr++) {
	for (uint16_t adr = 0; adr < 20; adr++) {
		delay(4); //terminal seems to crash if to fast?
		byte list_index = adr % bytes_per_row;
		if (list_index == 0) {
			//Serial.println("");
			PrintHex16(&adr, 1);
			Serial.print("/");
			PrintHex16(&address_space, 1);
			Serial.print("\t");
		}

		Serial.print("  ");
		byte read = read_byte(adr);
		blist[list_index] = char(read);
		PrintHex8(&read, 1);

		if (list_index == (bytes_per_row - 1)) {
			Serial.print("  | ");
			for (byte i = 0; i < bytes_per_row; i++) {
				char val = blist[i];
				if (isAscii(val)) {
					Serial.print(val);
				}
				else {
					Serial.print(".");
				}
				
			}
			Serial.println(" |");
		}

	}

	// Disable the data output
	enable_output(0);

	// Disable the chip
	chip_enable(0);


}

void read_memory_to_SD() {
	Serial.println("Choose filename max. 8 character long, empty = dump.bin");
	while (Serial.available() > 0) {}     //wait for data available
	String inputstr = Serial.readStringUntil('\n').trim();  //read until timeout

	if (inputstr.length() > 8) {
		inputstr = inputstr.substring(0, 8);
	}

	String filename = "dump.bin";
	if (inputstr.length() != 0) {
		filename = inputstr + ".bin";
	}

	Serial.print("Selected filename: ");
	Serial.println(filename);

	if (!SD.begin(BUILTIN_SDCARD)) {
		Serial.println("Card failed, or not present");
		return;
	}

	if (SD.exists(filename.c_str())) {
		Serial.print("file with name '");
		Serial.print(filename);
		Serial.print("' already exist!");
		Serial.println("");
		if (filename == "dump.bin") {
			Serial.print("File wil be overwritten!");
			SD.remove(filename.c_str());
		}
		else {
			Serial.print("Dump aborted, choose different filename!");
			return;
		}
		
	}

	File dataFile = SD.open(filename.c_str(), FILE_WRITE);



	Serial.println("Start writing SD card!");
	for (uint16_t adr = 0; adr < address_space; adr++) {
		if (adr % 100 == 0) {
			PrintHex16(&adr, 1);
			Serial.print("/");
			PrintHex16(&address_space, 1);
			int percent = (100 * adr) / address_space;
			Serial.print(" (");
			Serial.print(percent);
			Serial.print(" %)");
			Serial.println("");
		}
		
		dataFile.write(read_byte(adr));
	}

	dataFile.close();

	Serial.println("Done writing SD card!");
  action_OK = true;
	verify_dump_file(filename);
}

void enable_output(bool on) {
	digitalWrite(SRAM_N_OE, !on);
}

void verify_dump() {
	

	Serial.println("Choose filename to verify against, empty = dump.bin");
	while (Serial.available() > 0) {}     //wait for data available
	String inputstr = Serial.readStringUntil('\n').trim();  //read until timeout

	String filename = "dump.bin";
	if (inputstr.length() != 0) {
		filename = inputstr + ".bin";
	}
	
	verify_dump_file(filename);
}

void write_filename_into_ram(String filename) {
	Serial.print("Loading data from '");
	Serial.print(filename);
	Serial.println("' ...");

	if (!SD.begin(BUILTIN_SDCARD)) {
		Serial.println("Card failed, or not present");
		return;
	}

	bool dump_verifies = true;

	File dataFile = SD.open(filename.c_str());
	if (dataFile) {
		//Serial.println("Here");
		int count_address = 0;
		while (dataFile.available()) {
			unsigned char readout_file = dataFile.read();
			write_byte(count_address, readout_file);
			count_address++;
		}

		Serial.println("Data written!");

	}
	else {
		Serial.print("Command aborted: ");
		Serial.print(filename);
		Serial.println(" file does not exist!");
	}

	dataFile.close();
}



void load_file_into_ram() {


	Serial.println("Choose filename to write into ram, empty = dump.bin");
	while (Serial.available() > 0) {}     //wait for data available
	String inputstr = Serial.readStringUntil('\n').trim();  //read until timeout

	String filename = "dump.bin";
	if (inputstr.length() != 0) {
		filename = inputstr + ".bin";
	}

	write_filename_into_ram(filename);
}



void verify_dump_file(String filename) {

	Serial.print("Verifying against '");
	Serial.print(filename);
	Serial.println("' ...");

	if (!SD.begin(BUILTIN_SDCARD)) {
		Serial.println("Card failed, or not present");
		return;
	}

	bool dump_verifies = true;

	File dataFile = SD.open(filename.c_str());
	if (dataFile) {
		//Serial.println("Here");
		int count_address = 0;
		while (dataFile.available()) {
			unsigned char readout_file = dataFile.read();
			unsigned char readout_SRAM = read_byte(count_address);
			if (readout_file != readout_SRAM) {
				dump_verifies = false;
				Serial.print("addr: ");
				Serial.print(count_address);
				Serial.print("| file_value: ");
				Serial.print(readout_file, HEX);
				Serial.print(" does not match SRAM value: ");
				Serial.println(readout_SRAM, HEX);
			}
			//Serial.println(readout, HEX);
			count_address++;
		}

		if (address_space != count_address) {
			dump_verifies = false;
			Serial.print("Size of dump does not match size of chip! DUMP-size: ");
			Serial.print(count_address);
			Serial.print("| SRAM-size: ");
			Serial.println(address_space, HEX);
		}
		if (dump_verifies) {
			Serial.println("file and SRAM content match!");
      action_OK = true;
		}
		else Serial.println("ERROR: file and SRAM content DO NOT match!");
		Serial.println("Comparison Done!");

	}
	else {
		Serial.print("Command aborted: ");
		Serial.print(filename);
		Serial.println(" file does not exist!");
	}

	dataFile.close();
}

void write_enable(bool on) {
	digitalWrite(SRAM_N_WE, !on);
}

void chip_enable(bool on) {
	digitalWrite(SRAM_N_CS, !on);
}

void write_byte(int address, byte data) {

	// Output moet altijd disabled zijn
	enable_output(0);
	
	// Selecteer een address
	__set_address(address);

	// Enable de chip
	chip_enable(1);

	// Enable write modus
	write_enable(1);

	// Zet de data op de bus
	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		pinMode(data_pins[i], OUTPUT);
	}
	__write_byte(data);

	// Wacht tot data er goed opstaat
	delayMicroseconds(DELAY_US);

	// Disable write modus en chip
	write_enable(0);
	chip_enable(0);
}

void write_byte_long_wait(int address, byte data) {
	__set_address(address);
	write_enable(1);
	chip_enable(1);

	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		pinMode(data_pins[i], OUTPUT);
	}

	__write_byte(data);
	delay(10);

	write_enable(0);
	chip_enable(0);
}

byte read_byte(int address) {

	// Make inputs HIGH-Z
	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		pinMode(data_pins[i], INPUT);
	}
	
	// Set address data
	__set_address(address);

	// Enable the chip
	chip_enable(1);

	// Enable the data outputs
	enable_output(1);

	// Wait for the data to be put on the lines
	delay(DELAY_US);

	// Read the data on the line
	byte read_data = __read_byte();

	// Disable the data output
	enable_output(0);

	// Disable the chip
	chip_enable(0);

	return read_data;

}

void __write_byte(byte data) {
	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		digitalWrite(data_pins[i], bitRead(data, i));
	}
}

byte __read_byte() {
	byte data;
	for (int i = 0; i < DATA_PIN_COUNT; i++) {
		bitWrite(data, i, digitalRead(data_pins[i]));
	}
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

void verifydump_ledreset()
{
  digitalWrite(GreenLed, LOW);
  digitalWrite(RedLed, LOW);
  action_OK = false;
}
