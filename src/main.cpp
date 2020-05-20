#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <STM32RTC.h>

// Get the  rtc object
STM32RTC& rtc = STM32RTC::getInstance();
/* Change these values to set the current initial time */
const byte seconds = 0;
const byte minutes = 0;
const byte hours = 16;
 
/* Change these values to set the current initial date */
/* Monday 15th June 2015 */
const byte weekDay = 1;
const byte day = 15;
const byte month = 6;
const byte year = 15;

const float threshold = 2;

float capVoltage;
bool offSeason;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(74880);
  Wire.setSDA(PB_7); // using pin name PY_n
  Wire.setSCL(PB_6); // using pin number P
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  rtc.begin();
  analogReadResolution(12);
  ////////////////////////////////////////////////////////////////////////
  //CLOCK SYNCHRONIZATION WITH BASE STATTION
  ////////////////////////////////////////////////////////////////////////
  rtc.setTime(hours, minutes, seconds);
  rtc.setDate(weekDay, day, month, year);
  // to get the time/date -> rtc.getDay() for example

  offSeason = false;
  //Wire.onReceive(receiveEvent); // register event
}

void loop() {
  // put your main code here, to run repeatedly:

  //Serial.print(rtc.getSeconds());
  capVoltage = analogRead(A3);
  capVoltage = (capVoltage *3.6) /4095;
  //Serial.print(capVoltage);
  Serial.print("\n");
  if (capVoltage > threshold) //Check if voltage level supercap > threshold
  {
    if (offSeason == false) // do measurement and transmission
    {
      Wire.begin(); // join i2c bus as master
      Serial.print("BeforeBegintransmission");
      Wire.beginTransmission(127); //Begin transmitting to sensor with address 1

      Serial.print("After Begintransmission");
      Wire.write(1);
      Serial.print("After write 1");
      Wire.write(192); //11000000
      Wire.write(160);  //10100000
      Wire.endTransmission();
      delay(1000);
    }
     
  }
  //veranderd2hjljhl
}
