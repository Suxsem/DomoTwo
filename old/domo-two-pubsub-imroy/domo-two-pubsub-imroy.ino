#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define P2S(P) (((P) == (true) ? ("1") : ("0")))
#define SECINDAY 86400

#define SETUPssid           "DomoSetup"             //setup wifi network ssid
#define SETUPpsw            "***"          //setup wifi netwrok password
#define MQTTid              "DomoTwo"               //id of this mqtt client
#define MQTTip              "***"  //ip address or hostname of the mqtt broker
#define MQTTport            1883                    //port of the mqtt broker
#define MQTTuser            "***"               //username of this mqtt client
#define MQTTpsw             "***"      //password of this mqtt client
#define MQTTalive           60                      //mqtt keep alive interval (seconds)
#define MQTTpubQos          1
#define MQTTsubQos          1

#define SETUPssidAddr 0
#define SETUPpswAddr 64
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
boolean config = false;

unsigned long epoch = 0;
unsigned long lastEpoch = 0;
unsigned long lastComputedSeconds = 0;
unsigned long lastTimerShow = 0;

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
PubSubClient client(wclient, MQTTip, MQTTport);

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

unsigned long time() {
  unsigned long relativeSeconds = millis() / 1000;
  if (relativeSeconds - lastEpoch >= 0)
    return epoch + (relativeSeconds - lastEpoch);
  else
    return epoch + (sizeof(unsigned long) / 1000 - lastEpoch + relativeSeconds);
}

void port_switch(int port, boolean up) {
  Serial.printf("[%d%d", port, up);
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
  Serial.printf("[0%d", up);
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
    client.subscribe(MQTT::Subscribe()
                  .add_topic(MQTTid "/1/ton", MQTTsubQos)
                  .add_topic(MQTTid "/2/ton", MQTTsubQos)
                  .add_topic(MQTTid "/3/ton", MQTTsubQos)
                  .add_topic(MQTTid "/4/ton", MQTTsubQos)
                  .add_topic(MQTTid "/1/toff", MQTTsubQos)
                  .add_topic(MQTTid "/2/toff", MQTTsubQos)
                  .add_topic(MQTTid "/3/toff", MQTTsubQos)
                  .add_topic(MQTTid "/4/toff", MQTTsubQos)
                 );
}

void mqttConnectedCb() {

  events[newEvent()] = EVNT_CONN;
  
  if (!config) {

    client.publish(MQTT::Publish("/1/on", P2S(P1)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/2/on", P2S(P2)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/3/on", P2S(P3)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/4/on", P2S(P4)).set_qos(MQTTpubQos).set_retain());

    client.publish(MQTT::Publish("/1/ton", String(ton1)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/2/ton", String(ton2)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/3/ton", String(ton3)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/4/ton", String(ton4)).set_qos(MQTTpubQos).set_retain());
    
    client.publish(MQTT::Publish("/1/toff", String(toff1)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/2/toff", String(toff2)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/3/toff", String(toff3)).set_qos(MQTTpubQos).set_retain());
    client.publish(MQTT::Publish("/4/toff", String(toff4)).set_qos(MQTTpubQos).set_retain());

    if (timeReceived)
      timerSubscribe();

    client.subscribe(MQTT::Subscribe()
                  .add_topic("time", MQTTsubQos)
                  .add_topic(MQTTid "/1/on", MQTTsubQos)
                  .add_topic(MQTTid "/2/on", MQTTsubQos)
                  .add_topic(MQTTid "/3/on", MQTTsubQos)
                  .add_topic(MQTTid "/4/on", MQTTsubQos)
                 );
    
  } else {

    client.subscribe(MQTT::Subscribe()
                  .add_topic(MQTTid "/ssid", MQTTsubQos)
                  .add_topic(MQTTid "/psw", MQTTsubQos)
                 );

  }

  client.publish(MQTT::Publish(MQTTid "/status", "1").set_qos(MQTTpubQos).set_retain());

}

void mqttDisconnectedCb() {

  events[newEvent()] = EVNT_DISCONN;

}

void mqttDataCb(const MQTT::Publish& pub) {

  String message = pub.payload_string();

  Serial.println(pub.topic());
  Serial.println(message);
  
  if (config) {
    
    if (pub.topic() == MQTTid "/ssid")
      EPSW(SETUPssidAddr, message.c_str());
    else if (pub.topic() == MQTTid "/psw")
      EPSW(SETUPpswAddr, message.c_str());

    if (pub.topic() == MQTTid "/ssid" || pub.topic() == MQTTid "/psw")
      events[newEvent()] = EVNT_CONFIG;

  } else {
    
    unsigned long seconds = time() % SECINDAY;
    
    if (pub.topic() == MQTTid "/1/on") {
      P1 = message.toInt();
      events[newEvent()] = EVNT_P1;
    } else if (pub.topic() == MQTTid "/2/on") {
      P2 = message.toInt();
      events[newEvent()] = EVNT_P2;
    } else if (pub.topic() == MQTTid "/3/on") {
      P3 = message.toInt();
      events[newEvent()] = EVNT_P3;
    } else if (pub.topic() == MQTTid "/4/on") {
      P4 = message.toInt();
      events[newEvent()] = EVNT_P4;
  
    } else if (pub.topic() == MQTTid "/1/ton") {
      ton1 = message.toInt();
      EPLW(P1tonAddr, ton1);
      ton1armed = seconds < ton1;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/2/ton") {
      ton2 = message.toInt();
      EPLW(P2tonAddr, ton2);
      ton2armed = seconds < ton2;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/3/ton") {
      ton3 = message.toInt();
      EPLW(P3tonAddr, ton3);
      ton3armed = seconds < ton3;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/4/ton") {
      ton4 = message.toInt();
      EPLW(P4tonAddr, ton4);
      ton4armed = seconds < ton4;
      events[newEvent()] = EVNT_TIMER;
      
    } else if (pub.topic() == MQTTid "/1/toff") {
      toff1 = message.toInt();
      EPLW(P1toffAddr, toff1);
      toff1armed = seconds < toff1;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/2/toff") {
      toff2 = message.toInt();
      EPLW(P2toffAddr, toff2);
      toff2armed = seconds < toff2;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/3/toff") {
      toff3 = message.toInt();
      EPLW(P3toffAddr, toff3);
      toff3armed = seconds < toff3;
      events[newEvent()] = EVNT_TIMER;
    } else if (pub.topic() == MQTTid "/4/toff") {
      toff4 = message.toInt();
      EPLW(P4toffAddr, toff4);
      toff4armed = seconds < toff4;
      events[newEvent()] = EVNT_TIMER;
      
    } else if (pub.topic() == "time") {
      epoch = message.toInt();
      lastEpoch = millis() / 1000;
      
      if (!timeReceived) {
        timerSubscribe();
        timeReceived = true;
      }
    }
    
  }

}

void process_events() {
    while (events[currentEvent] > 0) {
      switch(events[currentEvent]) {
        case EVNT_CONN:
          status_led(HIGH);
        break;
        case EVNT_DISCONN:
          status_led(LOW);
        break;
        case EVNT_CONFIG:
          EEPROM.commit();
          status_led(LOW);
          delay(100);
          status_led(HIGH);
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
    EPSW(SETUPssidAddr, SETUPssid);
    EPSW(SETUPpswAddr, SETUPpsw);
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

  if (EPSR(SETUPssidAddr) == SETUPssid)
    config = true;

  ton1 = EPLR(P1tonAddr);
  ton2 = EPLR(P2tonAddr);
  ton3 = EPLR(P3tonAddr);
  ton4 = EPLR(P4tonAddr);

  toff1 = EPLR(P1toffAddr);
  toff2 = EPLR(P2toffAddr);
  toff3 = EPLR(P3toffAddr);
  toff4 = EPLR(P4toffAddr);

  WiFi.mode(WIFI_STA);
  WiFi.begin(EPSR(SETUPssidAddr).c_str(), EPSR(SETUPpswAddr).c_str());

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
      if (client.connect(MQTT::Connect(MQTTid)
           .set_auth(MQTTuser, MQTTpsw)
           .set_clean_session()
           .set_keepalive(MQTTalive)
           .set_will(MQTTid "/status", "0", MQTTpubQos, true))) {
          client.set_callback(mqttDataCb);
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
    while(Serial.available() < 2);
    byte port = Serial.read() - '0';
    byte up = Serial.read() - '0';
    if (port == 0 && up == 1) {
      EPSW(SETUPssidAddr, SETUPssid);
      EPSW(SETUPpswAddr, SETUPpsw);
      EEPROM.commit();
      ESP.restart();
    }
    if (port > 0 && !config && client.connected())
      client.publish(MQTT::Publish(String(MQTTid "/") + port + "/on", String(up)).set_qos(MQTTpubQos).set_retain());
  }
  
  if (!config) {
      
    unsigned long seconds = time() % SECINDAY;

    if (seconds < lastComputedSeconds)
      seconds = SECINDAY;
  
    if (ton1 >= 0 && ton1armed && seconds >= ton1) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/1/on", "1").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(1, HIGH);
      ton1armed = false;
    }
    if (ton2 >= 0 && ton2armed && seconds >= ton2) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/2/on", "1").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(2, HIGH);
      ton2armed = false;
    }
    if (ton3 >= 0 && ton3armed && seconds >= ton3) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/3/on", "1").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(3, HIGH);
      ton3armed = false;
    }
    if (ton4 >= 0 && ton4armed && seconds >= ton4) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/4/on", "1").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(4, HIGH);
      ton4armed = false;
    }

    if (toff1 >= 0 && toff1armed && seconds >= toff1) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/1/on", "0").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(1, LOW);
      toff1armed = false;
    }
    if (toff2 >= 0 && toff2armed && seconds >= toff2) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/2/on", "0").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(2, LOW);
      toff2armed = false;
    }
    if (toff3 >= 0 && toff3armed && seconds >= toff3) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/3/on", "0").set_qos(MQTTpubQos).set_retain());
      else
        port_switch(3, LOW);
      toff3armed = false;
    }
    if (toff4 >= 0 && toff4armed && seconds >= toff4) {
      if (client.connected())
        client.publish(MQTT::Publish(MQTTid "/4/on", "0").set_qos(MQTTpubQos).set_retain());
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

    if (seconds - lastTimerShow >= 7 && (ton1 >= 0 || toff1 >= 0 || ton2 >= 0 || toff2 >= 0 || ton3 >= 0 || toff3 >= 0 || ton4 >= 0 || toff4 >= 0)) {
      blink(3);
      lastTimerShow = seconds;
    }

  }  
}
