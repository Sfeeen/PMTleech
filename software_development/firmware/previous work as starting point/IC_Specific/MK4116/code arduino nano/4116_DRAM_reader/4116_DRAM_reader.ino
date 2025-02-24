#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>

#define BUS_SIZE 7

#define DI          A1
#define DO           8
#define CAS          9
#define RAS         A3
#define WE          A2

#define XA0         A4
#define XA1          2
#define XA2         A5
#define XA3          6
#define XA4          5
#define XA5          4
#define XA6          7

const unsigned int a_bus[BUS_SIZE] = {
  XA0, XA1, XA2, XA3, XA4, XA5, XA6
};

bool fout = false;

void  initDram()
{
  for (int i = 0 ; i < 8 ; i++)  {
    digitalWrite(a_bus[i], LOW);
  }
  
  digitalWrite(RAS, LOW);
  digitalWrite(RAS, HIGH);
}

void setBus(unsigned int a) {
  for (int i = 0; i < BUS_SIZE; i++) { //cambiato con bus_size piccolo
    if (bitRead(a, i) == 1) {
      digitalWrite(a_bus[i], HIGH);
    } else {
      digitalWrite(a_bus[i], LOW);
    }
  }
}

void writeAddress(unsigned int r, unsigned int c, bool v) {
  /* row */
  setBus(r);
  digitalWrite(RAS, LOW);
  /* rw */
  digitalWrite(WE, LOW);
  /* val */
  digitalWrite(DI, v);
  /* col */
  setBus(c);
  digitalWrite(CAS, LOW);
  digitalWrite(WE, HIGH);
  digitalWrite(CAS, HIGH);
  digitalWrite(RAS, HIGH);
}

int readAddress(unsigned int r, unsigned int c) {
  int ret = 0;

  /* row */
  setBus(r);
  digitalWrite(RAS, LOW);
  /* col */
  setBus(c);
  digitalWrite(CAS, LOW);
  /* get current value */
  ret = digitalRead(DO);

  digitalWrite(CAS, HIGH);
  digitalWrite(RAS, HIGH);
  return ret;
}

void fill(bool v, bool rd) {
  int r, c;
  for (c = 0; c < (1 << BUS_SIZE); c++) {
    for (r = 0; r < (1 << BUS_SIZE); r++) {
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
  for (c = 0; c < (1 << BUS_SIZE); c++) {
    for (r = 0; r < (1 << BUS_SIZE); r++) {
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
  for (c = 0; c < (1 << BUS_SIZE); c++) {
    for (r = 0; r < (1 << BUS_SIZE); r++) {
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

void setup() {
  Serial.begin(9600);
  delay(100);
  Serial.println("start code!");

  for (int i = 0; i < BUS_SIZE; i++) {
    pinMode(a_bus[i], OUTPUT);
  }

  pinMode(CAS, OUTPUT);
  pinMode(RAS, OUTPUT);
  pinMode(WE, OUTPUT);
  pinMode(DI, OUTPUT);
  pinMode(DO, INPUT);

  digitalWrite(WE, HIGH);
  digitalWrite(RAS, HIGH);
  digitalWrite(CAS, HIGH);

}

void loop() {
  //  writeAddress(0, 0, 0);
  //  readAddress(0, 0);
  //delay(1000);
  //writeAddress(0, 0, 0);
  //readAddress(0, 0);
  //delay(1000);
  startTest();
  delay(10000);

}


void startTest() {
  Serial.println("start test!");
  initDram();

  Serial.println("fillx(0);");
  fillx(0);
  if (fout) {
    Serial.println("fillx(0) -> error");
    fout = false;
  } else {
    Serial.println("fillx(0) -> OK");
  }

  Serial.println("fillx(1);");
  fillx(1);
  if (fout) {
    Serial.println("fillx(1) -> error");
    fout = false;
  } else {
    Serial.println("fillx(1) -> OK");
  }

  Serial.println("fill(1,false);");
  fill(1, false);
  if (fout) {
    Serial.println("fill(1,false); -> error");
    fout = false;
  } else {
    Serial.println("fill(1,false); -> OK");
  }

  Serial.println("fill(1,true);");
  fill(1, true);
  if (fout) {
    Serial.println("fill(1,true); -> error");
    fout = false;
  } else {
    Serial.println("fill(1,true); -> OK");
  }

  Serial.println("readonly(1);");
  readonly(1);
  if (fout) {
    Serial.println("readonly(1); -> error");
    fout = false;
  } else {
    Serial.println("readonly(1); -> OK");
  }

}
