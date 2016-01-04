#define P1_OUT 7
#define P2_OUT 6
#define P3_OUT 8
#define P4_OUT 5

#define P1_IN 9
#define P2_IN 10
#define P3_IN 11
#define P4_IN 12

#define RED 3
#define GREEN 4

boolean button1released = true;
boolean button2released = true;
boolean button3released = true;
boolean button4released = true;

boolean isGreen = false;

void port_switch(byte port, byte up) {
  switch(port) {
    case 0:
      if (up >= 2) {
        blink(up);
      } else {
        isGreen = up;
        digitalWrite(GREEN, up);
        digitalWrite(RED, !up);
      }
      break;
    case 1:
      digitalWrite(P1_OUT, !up);
      break;
    case 2:
      digitalWrite(P2_OUT, !up);
      break;
    case 3:
      digitalWrite(P3_OUT, !up);
      break;
    case 4:
      digitalWrite(P4_OUT, !up);
      break;
  }
}

void blink(byte speed) {
  int pDelay;
  byte loops;
  switch (speed) {
    case 2:
      pDelay = 500;
      loops = 8;
      break;
    case 3:
      pDelay = 250;
      loops = 4;
      break;
    case 4:
      pDelay = 100;
      loops = 8;
      break;
  }
  for (int i = 0; i < loops; i++) {
    if (i > 0)
      delay(pDelay);
    isGreen = !isGreen;
    digitalWrite(RED, !isGreen);
    digitalWrite(GREEN, isGreen);
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(0);
  
  pinMode(RED, OUTPUT);
  digitalWrite(RED, HIGH);
  pinMode(GREEN, OUTPUT);

  digitalWrite(P1_OUT, HIGH);
  digitalWrite(P2_OUT, HIGH);
  digitalWrite(P3_OUT, HIGH);
  digitalWrite(P4_OUT, HIGH);
  pinMode(P1_OUT, OUTPUT);
  pinMode(P2_OUT, OUTPUT);
  pinMode(P3_OUT, OUTPUT);
  pinMode(P4_OUT, OUTPUT);
  
  if (digitalRead(P1_IN) && digitalRead(P4_IN)) {
    blink(4);
    Serial.print("[000111");
  } else {
    blink(2);
    Serial.print("[000000");
  }
    
}


void loop() {
  if (Serial.find((char*)"[")) {
    while(Serial.available() < 6);
    byte port = 255;
    byte up = 255;
    byte port1 = Serial.read() - '0';
    byte port2 = Serial.read() - '0';
    byte port3 = Serial.read() - '0';
    byte up1 = Serial.read() - '0';
    byte up2 = Serial.read() - '0';
    byte up3 = Serial.read() - '0';
    if (port1 == port2 || port2 == port3)
      port = port2;
    else if (port3 == port1)
      port = port3;
    if (up1 == up2 || up2 == up3)
      up = up2;
    else if (up3 == up1)
      up = up3;
    if (port < 255 && up < 255)
      port_switch(port, up);
  }

  if (button1released && digitalRead(P1_IN)) {
    button1released = false;
    if (digitalRead(P1_OUT))
      Serial.print("[111111");
    else
      Serial.print("[111000");
  } else if (!button1released && !digitalRead(P1_IN)) {
    button1released = true;
    delay(100);
  }
  if (button2released && digitalRead(P2_IN)) {
    button2released = false;
    if (digitalRead(P2_OUT))
      Serial.print("[222111");
    else
      Serial.print("[222000");
  } else if (!button2released && !digitalRead(P2_IN)) {
    button2released = true;
    delay(100);
  }
  if (button3released && digitalRead(P3_IN)) {
    button3released = false;
    if (digitalRead(P3_OUT))
      Serial.print("[333111");
    else
      Serial.print("[333000");
  } else if (!button3released && !digitalRead(P3_IN)) {
    button3released = true;
    delay(100);
  }
  if (button4released && digitalRead(P4_IN)) {
    button4released = false;
    if (digitalRead(P4_OUT))
      Serial.print("[444111");
    else
      Serial.print("[444000");
  } else if (!button4released && !digitalRead(P4_IN)) {
    button4released = true;
    delay(100);
  }

}
