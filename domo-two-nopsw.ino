#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define P2S(P) (((P) == (true) ? ("1") : ("0")))
#define SECINDAY 86400

#define DEFAULTssid         "Domo"                  //setup wifi network ssid
#define DEFAULTpsw          "***"          //setup wifi netwrok password
#define MQTTid              "DomoTwo"               //id of this mqtt client
#define MQTTip              "***"  //ip address or hostname of the mqtt broker
#define MQTTport            1883                    //port of the mqtt broker
#define MQTTuser            "***"               //username of this mqtt client
#define MQTTpsw             "***"      //password of this mqtt client
#define MQTTalive           60                      //mqtt keep alive interval (seconds)
#define MQTTpubQos          1
#define MQTTsubQos          1

#define DEFAULTssidAddr 0
#define DEFAULTpswAddr 64
#define P1onAddr 128
#define P2onAddr 129
#define P3onAddr 130
#define P4onAddr 131
#define P1tonAddr 132
#define P2tonAddr 136
#define P3tonAddr 140
#define P4tonAddr 144
#define P1toffAddr 148
#define P2toffAddr 152
#define P3toffAddr 156
#define P4toffAddr 160
#define nextAddr 164
#define NewAddr 250
#define EPversion 3

#define EVNT_CONN 1
#define EVNT_DISCONN 2
#define EVNT_CONFIG 3
#define EVNT_P1 4
#define EVNT_P2 5
#define EVNT_P3 6
#define EVNT_P4 7
#define EVNT_TIMER 8
#define EVNT_TIME 9

boolean timeReceived = false;
boolean pendingDisconnect = false;

unsigned long epoch = 0;
unsigned long lastEpoch = 0;
unsigned long lastComputedSeconds = 0;
unsigned long lastTimerShow = 0;
unsigned long freeze = 0;

boolean P1;
boolean P2;
boolean P3;
boolean P4;

long ton1;
long ton2;
long ton3;
long ton4;

long toff1;
long toff2;
long toff3;
long toff4;

boolean ton1armed = false;
boolean ton2armed = false;
boolean ton3armed = false;
boolean ton4armed = false;

boolean toff1armed = false;
boolean toff2armed = false;
boolean toff3armed = false;
boolean toff4armed = false;

#define EVENTS_SIZE 64
unsigned char events[EVENTS_SIZE];
unsigned char nextEvent = 0;
unsigned char currentEvent = 0;

WiFiClient wclient;
void mqttDataCb(char* topic, byte* payload, unsigned int length);
PubSubClient client(MQTTip, MQTTport, mqttDataCb, wclient);

void EPBW(int address, boolean value) {
  EEPROM.write(address, value);
}

boolean EPBR(int address) {
  return EEPROM.read(address);
}

void EPLW(int address, long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);
  
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

long EPLR(int address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
      
void EPSW(int p_start_posn, const char p_string[] ) {

  int str_len = strlen(p_string);
  for (int l_posn = 0; l_posn < str_len; l_posn ++) {
    byte l_byte = (byte) p_string[l_posn];
    byte l_read = EEPROM.read(p_start_posn + l_posn);
    if (l_read != l_byte)
      EEPROM.write(p_start_posn + l_posn, l_byte);
  }
  //write the NULL termination
  if (EEPROM.read(p_start_posn + str_len) != 0)
  EEPROM.write(p_start_posn + str_len, 0);
}

String EPSR(int p_start_posn) { //EEPROMStringRead
  //Read a NULL terminated string from EEPROM
  //Only strings up to 128 bytes are supported
  byte l_byte;

  //Count first, reserve exact string length and then extract
  int l_posn = 0;
  while (true) {
    l_byte = EEPROM.read(p_start_posn + l_posn);
    if (l_byte == 0)
      break;
    l_posn ++;
  }

  //Now extract the string
  String l_string = "";
  l_string.reserve(l_posn + 1);
  l_posn = 0;
  while (true) {
    l_byte = EEPROM.read(p_start_posn + l_posn);
    if (l_byte == 0)
      break;
    l_string += (char) l_byte;
    l_posn ++;
    if (l_posn == 128)
      break;
  }
  
  return l_string;
}

// this code will works until 2106, when epoch will overflow an unsigned long
unsigned long time() {
  unsigned long seconds = epoch + (millis() / 1000 - lastEpoch);
  return seconds >= freeze ? seconds : freeze;    
}

void port_switch(int port, boolean up) {
  Serial.printf("[%d%d%d%d%d%d", port, port, port, up, up, up);
  switch (port) {
    case 1:
      P1 = up;
      EPBW(P1onAddr, up);
      break;
    case 2:
      P2 = up;
      EPBW(P2onAddr, up);
      break;
    case 3:
      P3 = up;
      EPBW(P3onAddr, up);
      break;
    case 4:
      P4 = up;
      EPBW(P4onAddr, up);
      break;
  }
  EEPROM.commit();
}
void status_led(boolean up) {
  Serial.printf("[000%d%d%d", up, up, up);
}
void blink(byte speed) {
  status_led(speed);
}

unsigned char newEvent() {
  unsigned char old = nextEvent++;
  if (nextEvent == EVENTS_SIZE)
    nextEvent = 0;
  return old;
}

void timerSubscribe() {
    client.subscribe(MQTTid "/+/ton", MQTTsubQos);
    client.subscribe(MQTTid "/+/toff", MQTTsubQos);
}

void mqttConnectedCb() {

  events[newEvent()] = EVNT_CONN;

}

void mqttDisconnectedCb() {

  events[newEvent()] = EVNT_DISCONN;

}

void mqttDataCb(char* topic, byte* payload, unsigned int length) {

  char* message = (char *) payload;
  message[length] = 0;

  unsigned long seconds = time() % SECINDAY;
  long lMessage = strtol(message, NULL, 10);
  
  if (!strcmp(topic, MQTTid "/1/on")) {
    P1 = lMessage;
    events[newEvent()] = EVNT_P1;
  } else if (!strcmp(topic, MQTTid "/2/on")) {
    P2 = lMessage;
    events[newEvent()] = EVNT_P2;
  } else if (!strcmp(topic, MQTTid "/3/on")) {
    P3 = lMessage;
    events[newEvent()] = EVNT_P3;
  } else if (!strcmp(topic, MQTTid "/4/on")) {
    P4 = lMessage;
    events[newEvent()] = EVNT_P4;

  } else if (!strcmp(topic, MQTTid "/1/ton")) {
    ton1 = lMessage;
    EPLW(P1tonAddr, ton1);
    ton1armed = seconds < ton1;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/2/ton")) {
    ton2 = lMessage;
    EPLW(P2tonAddr, ton2);
    ton2armed = seconds < ton2;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/3/ton")) {
    ton3 = lMessage;
    EPLW(P3tonAddr, ton3);
    ton3armed = seconds < ton3;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/4/ton")) {
    ton4 = lMessage;
    EPLW(P4tonAddr, ton4);
    ton4armed = seconds < ton4;
    events[newEvent()] = EVNT_TIMER;
    
  } else if (!strcmp(topic, MQTTid "/1/toff")) {
    toff1 = lMessage;
    EPLW(P1toffAddr, toff1);
    toff1armed = seconds < toff1;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/2/toff")) {
    toff2 = lMessage;
    EPLW(P2toffAddr, toff2);
    toff2armed = seconds < toff2;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/3/toff")) {
    toff3 = lMessage;
    EPLW(P3toffAddr, toff3);
    toff3armed = seconds < toff3;
    events[newEvent()] = EVNT_TIMER;
  } else if (!strcmp(topic, MQTTid "/4/toff")) {
    toff4 = lMessage;
    EPLW(P4toffAddr, toff4);
    toff4armed = seconds < toff4;
    events[newEvent()] = EVNT_TIMER;
    
  } else if (!strcmp(topic, "time")) {
    freeze = time();
    if (lMessage < epoch) { //new time is less then the previous (daylight saving?)
      ESP.restart();
      while(1);
    } else{
      epoch = lMessage;
    }
    lastEpoch = millis() / 1000;
    
    if (!timeReceived) {
      events[newEvent()] = EVNT_TIME;
      timeReceived = true;
    }
  
  } else if (!strcmp(topic, MQTTid "/ssid")) {
    EPSW(DEFAULTssidAddr, message);
    events[newEvent()] = EVNT_CONFIG;
  } else if (!strcmp(topic, MQTTid "/psw")) {
    EPSW(DEFAULTpswAddr, message);
    events[newEvent()] = EVNT_CONFIG;
  }

}

void process_events() {
    while (events[currentEvent] > 0) {
      switch(events[currentEvent]) {
        case EVNT_CONN: {
        
          client.publish(MQTTid "/1/on", P2S(P1), MQTTpubQos, true);
          client.publish(MQTTid "/2/on", P2S(P2), MQTTpubQos, true);
          client.publish(MQTTid "/3/on", P2S(P3), MQTTpubQos, true);
          client.publish(MQTTid "/4/on", P2S(P4), MQTTpubQos, true);
      
          client.publish(MQTTid "/1/ton", String(ton1).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/2/ton", String(ton2).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/3/ton", String(ton3).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/4/ton", String(ton4).c_str(), MQTTpubQos, true);
          
          client.publish(MQTTid "/1/toff", String(toff1).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/2/toff", String(toff2).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/3/toff", String(toff3).c_str(), MQTTpubQos, true);
          client.publish(MQTTid "/4/toff", String(toff4).c_str(), MQTTpubQos, true);

          client.publish(MQTTid "/status", "1", MQTTpubQos, true);
          
          if (timeReceived)
            timerSubscribe();
      
          client.subscribe("time", MQTTsubQos);
          client.subscribe(MQTTid "/+", MQTTsubQos);
          client.subscribe(MQTTid "/+/on", MQTTsubQos);

          status_led(HIGH);
        break;
        }
        case EVNT_DISCONN:
          status_led(LOW);
        break;
        case EVNT_CONFIG:
          EEPROM.commit();
          blink(3);
        break;
        case EVNT_P1:
          port_switch(1, P1);
        break;
        case EVNT_P2:
          port_switch(2, P2);
        break;
        case EVNT_P3:
          port_switch(3, P3);
        break;
        case EVNT_P4:
          port_switch(4, P4);
        break;
        case EVNT_TIMER:
          EEPROM.commit();
        break;
        case EVNT_TIME:
          timerSubscribe();
        break;
      }
      events[currentEvent] = 0;
      currentEvent++;
      if (currentEvent == EVENTS_SIZE)
        currentEvent = 0;
    }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(0);
  
  EEPROM.begin(256);
  memset(events, 0, sizeof(events));
  
  if (EPLR(NewAddr) != EPversion) {
    EPSW(DEFAULTssidAddr, DEFAULTssid);
    EPSW(DEFAULTpswAddr, DEFAULTpsw);
    EPBW(P1onAddr, false);
    EPBW(P2onAddr, false);
    EPBW(P3onAddr, false);
    EPBW(P4onAddr, false);
    EPLW(P1tonAddr, -1);
    EPLW(P2tonAddr, -1);
    EPLW(P3tonAddr, -1);
    EPLW(P4tonAddr, -1);
    EPLW(P1toffAddr, -1);
    EPLW(P2toffAddr, -1);
    EPLW(P3toffAddr, -1);
    EPLW(P4toffAddr, -1);
    EPLW(NewAddr, EPversion);
    EEPROM.commit();
  }

  ton1 = EPLR(P1tonAddr);
  ton2 = EPLR(P2tonAddr);
  ton3 = EPLR(P3tonAddr);
  ton4 = EPLR(P4tonAddr);

  toff1 = EPLR(P1toffAddr);
  toff2 = EPLR(P2toffAddr);
  toff3 = EPLR(P3toffAddr);
  toff4 = EPLR(P4toffAddr);

  WiFi.mode(WIFI_STA);
  WiFi.begin(EPSR(DEFAULTssidAddr).c_str(), EPSR(DEFAULTpswAddr).c_str());

  delay(2000); //wait for arduino to start serial
  status_led(LOW);

  port_switch(1, EPBR(P1onAddr));
  port_switch(2, EPBR(P2onAddr));
  port_switch(3, EPBR(P3onAddr));
  port_switch(4, EPBR(P4onAddr));

}

void process_net() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.begin();
    ArduinoOTA.handle();
    if (client.connected()) {
      client.loop();
    } else {
      if (client.connect(MQTTid, MQTTuser, MQTTpsw, MQTTid "/status", 2, true, "0")) {
          pendingDisconnect = false;
          mqttConnectedCb();
      }
    }
  } else {
    if (client.connected())
      client.disconnect();
  }
  if (!client.connected() && !pendingDisconnect) {
    pendingDisconnect = true;
    mqttDisconnectedCb();
  }  
}

void loop() {

  process_net();

  process_events();

  if (Serial.find("[")) {
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
    if (port < 255 && up < 255) {
      if (port == 0 && up == 1) {
        EPSW(DEFAULTssidAddr, DEFAULTssid);
        EPSW(DEFAULTpswAddr, DEFAULTpsw);
        EEPROM.commit();
        ESP.restart();
      }
      if (port > 0) {
        if (client.connected())
          client.publish((String(MQTTid "/") + port + "/on").c_str(), P2S(up), MQTTpubQos, true);
        else
          port_switch(port, up);
      }
    }
  }
  
  if (timeReceived) {
      
    unsigned long seconds = time() % SECINDAY;

    if (seconds < lastComputedSeconds)
      seconds = SECINDAY;
  
    if (ton1 >= 0 && ton1armed && seconds >= ton1) {
      if (client.connected())
        client.publish(MQTTid "/1/on", "1", MQTTpubQos, true);
      else
        port_switch(1, HIGH);
      ton1armed = false;
    }
    if (ton2 >= 0 && ton2armed && seconds >= ton2) {
      if (client.connected())
        client.publish(MQTTid "/2/on", "1", MQTTpubQos, true);
      else
        port_switch(2, HIGH);
      ton2armed = false;
    }
    if (ton3 >= 0 && ton3armed && seconds >= ton3) {
      if (client.connected())
        client.publish(MQTTid "/3/on", "1", MQTTpubQos, true);
      else
        port_switch(3, HIGH);
      ton3armed = false;
    }
    if (ton4 >= 0 && ton4armed && seconds >= ton4) {
      if (client.connected())
        client.publish(MQTTid "/4/on", "1", MQTTpubQos, true);
      else
        port_switch(4, HIGH);
      ton4armed = false;
    }

    if (toff1 >= 0 && toff1armed && seconds >= toff1) {
      if (client.connected())
        client.publish(MQTTid "/1/on", "0", MQTTpubQos, true);
      else
        port_switch(1, LOW);
      toff1armed = false;
    }
    if (toff2 >= 0 && toff2armed && seconds >= toff2) {
      if (client.connected())
        client.publish(MQTTid "/2/on", "0", MQTTpubQos, true);
      else
        port_switch(2, LOW);
      toff2armed = false;
    }
    if (toff3 >= 0 && toff3armed && seconds >= toff3) {
      if (client.connected())
        client.publish(MQTTid "/3/on", "0", MQTTpubQos, true);
      else
        port_switch(3, LOW);
      toff3armed = false;
    }
    if (toff4 >= 0 && toff4armed && seconds >= toff4) {
      if (client.connected())
        client.publish(MQTTid "/4/on", "0", MQTTpubQos, true);
      else
        port_switch(4, LOW);
      toff4armed = false;
    }

    if (seconds == SECINDAY) {
      ton1armed = true;
      ton2armed = true;
      ton3armed = true;
      ton4armed = true;
      toff1armed = true;
      toff2armed = true;
      toff3armed = true;
      toff4armed = true;
      lastComputedSeconds = 0;
    } else {
      lastComputedSeconds = seconds;
    }

    if (millis() - lastTimerShow >= 7000 && (ton1 >= 0 || toff1 >= 0 || ton2 >= 0 || toff2 >= 0 || ton3 >= 0 || toff3 >= 0 || ton4 >= 0 || toff4 >= 0)) {
      blink(3);
      lastTimerShow = millis();
    }

  }  
}
