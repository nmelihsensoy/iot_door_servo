#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <Servo.h>
#include "RFIDRdm630.h"

String lastRead = "0";
int oldValue;
boolean lockStatus = false;
boolean locked = false;
char message_buff[100];
boolean lockButtonState;
long lastMsg = 0;

//Wifi Settings
const char* host = "esp8266-doorservo";
const char* ssid = "wifi-ssid";
const char* password = "wifi-pass";
const char* mqtt_server = "orangepi.local"; //Mqtt broker address

//OTA
const char* update_path = "/firmware"; 
const char* update_username = "admin";
const char* update_password = "ota-pass";

//Gpio
const int rxPin = 14;  //Arduino 3 ESP D5
const int txPin = 16;  //Arduino 4 ESP D0 

const int outLockButton = 13;//D7

const int doorReed = 4;//D0
int doorState = 0;

const int doorRelay = 5;//D1

const int servoPin = 2;//D4
int servoPosition = 1400;

//Call&Create Library Instance
Servo servo;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient client(espClient);
RFIDtag  tag;
RFIDRdm630 reader = RFIDRdm630(rxPin,txPin);

void setup() {
  //Pin Config
  pinMode(doorRelay, OUTPUT);
  pinMode(doorReed, INPUT);
  pinMode(outLockButton, INPUT);
    
  Serial.begin(115200, SERIAL_8N1,SERIAL_TX_ONLY);
  setup_wifi();
  
  //OTA Config
  MDNS.begin(host); 
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void servoDondur(int ms, int yon){
    if(yon == 1){
        servo.writeMicroseconds(9000);
        delay(ms); 
      }else if(yon == 0){
          servo.writeMicroseconds(-1);
          delay(ms); 
      }
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
   String strPayload = "";
   String strTopic(topic);
  for (int i = 0; i < length; i++) {
    strPayload += (char)payload[i];
  }

  if(strTopic == "/melih/doorServo"){   
     if(strPayload == "OP"){
          if(digitalRead(doorReed) == LOW){ //check door state
            //outLockButton is outside button for lock and unlock with 1 rfid tag
            if(digitalRead(outLockButton) == HIGH){ 
              Serial.println("OPEN DOOR");
              servo.attach(servoPin);
              servo.writeMicroseconds(-1);
              locked = true;
            }else{
                Serial.println("LOCK DOOR");
                servo.attach(servoPin);
                servoDondur(3200, 1);
                servo.detach();
                lockStatus = true;
              }         
          }         
      }else if(strPayload == "LO"){
          if((digitalRead(doorReed) == LOW) && (lockStatus == false)){
            Serial.println("LOCK DOOR");
            lockStatus = true;
            servo.attach(servoPin);
            servoDondur(3200, 1);
            servo.detach();
          }
      }     
    }else if(strTopic == "/melih/outDoor"){
       if(strPayload == "ON"){
          digitalWrite(doorRelay, HIGH);
          delay(500);
          digitalWrite(doorRelay, LOW);
       }
    }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266SERVO")) {
      Serial.println("connected");
      client.publish("/melih/connections", "DOOR ESP CONNECTED");
      //Subscribe Channels
      client.subscribe("/melih/doorServo");
      client.subscribe("/melih/outDoor");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  httpServer.handleClient();

  //Interrupt variables
	static unsigned long prev = 0;
	unsigned long now = millis();

  //Rfid tag publish
  if (reader.isAvailable()){ 
    tag = reader.getTag(); 
    String str(tag.getTag());

      //publish spam solution
      if(!lastRead.equals(str)){
          Serial.println(str);
		      client.publish("/melih/rfid", tag.getTag());
        }

        //publish spam solution
        if((lastRead.equals(str)) && (now - prev >= 4000)){
            lastRead = "0";
            prev = now;
          }else{
             lastRead = str;
             prev = now;
          }
          
      }
  
  //Auto reset servo position after unlock
  if(locked == true){
     doorState = digitalRead(doorReed);
     if(doorState == HIGH){
        locked = false;
        lockStatus = false;
        servo.writeMicroseconds(servoPosition);
        servoDondur(500, 1);
        servo.detach();
      }
    }

  //Publish door state every 1 second
	doorState = digitalRead(doorReed);
    if (now - lastMsg > 1000) {
      lastMsg = now;
      if(doorState == HIGH){
        client.publish("/melih/doorState", "ON");
       }else{
        client.publish("/melih/doorState", "OFF");
       }
    }

  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();  
}
