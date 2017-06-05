#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 2

SoftwareSerial BTSerial(3, 4); //Connect HC-06. Use your (TX, RX) settings

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int speakerpin = 12;
int analogPin=0;
int val=0;

int i = 1;
int UserInput = 0;
char tempBuf[3];


void setup()  
{
  Serial.begin(9600);
  Serial.println("Start Program");

  BTSerial.begin(9600);  // set the data rate for the BT port
  tempBuf[3] = '\0';
}

void loop()
{ 
// BT –> Data –> Serial  
if (BTSerial.available()) {
  Serial.print("The ");
  Serial.print(i);
  Serial.println(" th loop");

  UserInput = BTSerial.read();

  Serial.print("User Input data is = ");
  Serial.write(UserInput);
  Serial.println();
  
  if(UserInput == 1+'0'){
   for(int k = 0;k<5;k++){
    tone(speakerpin,500,1000);
   }   
  }
  else if(UserInput == 2+'0'){
    Serial.print("Requesting temperatures...");
    sensors.requestTemperatures(); // Send the command to get temperatures
    Serial.println("DONE"); 
    Serial.print("Temperature for the device 1 (index 0) is: ");

    int data = sensors.getTempCByIndex(0);
    Serial.println(data);

      if(data > 45){
      for(int k = 0;k<5;k++){
        tone(speakerpin,500,200);
        delay(500); 
      }
    }

    tempBuf[0] = data/10+'0';
    tempBuf[1] = data%10+'0';          
    BTSerial.write(tempBuf,3);
  }
  else if(UserInput==3+'0') {
    Serial.print("Requesting Height...");
    val=analogRead(analogPin);
    Serial.print("height :");
    Serial.println(val);

    if(val > 650){
      for(int k = 0;k<5;k++){
        tone(speakerpin,500,200);
        delay(500); 
      }
      tempBuf[0] = '0';
      tempBuf[1] = '1';   
    }
    else {
      tempBuf[0] = '1';
      tempBuf[1] = '0';
    }
          
    BTSerial.write(tempBuf,3);
  }
  
  UserInput = '0';
  i++;
  }
}
