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

const float threshold_SPSM = 3;
const float threshold_LPSM = 2;
const float threshold_CM = 1;
const int sensorCount = 5;
const float maxTempSlope = 8;
const float criticalTemp = 0.5;


float Energy;
bool offSeason;
float temperature[sensorCount];



HardwareSerial Serial1(PA_10, PA_9);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(74880);
  Wire.setSDA(PB_7); // using pin name PY_n
  Wire.setSCL(PB_6); // using pin number P
  Wire.setClock(10000);
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
  Energy = analogRead(A3);
  Energy = (Energy *3.6) /4095;
  //Serial.print(Energy);
  Serial.print("\n");
  if (Energy > threshold_SPSM) //Check if voltage level supercap > threshold_SPSM
  {
    if (offSeason == false) // do measurement and transmission
    {

      //Initiate measurement on  sensor//
      Wire.begin(); // join i2c bus as master
      Serial.print("BeforeBegintransmission");
      Wire.beginTransmission(127); //Begin transmitting to sensor with address 1

      Serial.print("After Begintransmission");
      Wire.write(1);
      Serial.print("After write 1");
      Wire.write(192); //11000000
      Wire.write(160);  //10100000
      Wire.endTransmission();
      //

      //put MCU in sleep mode//
      //revive mcu//
      //ask mcu for data & receive//

      //Send data to LoRa module
      Serial1.begin(57600);
      Serial1.write("radio tx 1C1C1C1C"); // Data in hexadecimal pairs in the following order: Battery/Time/Data
      Serial1.end();

      //After the data has been sent to the wireless communication module, the module takes some time to transmit the data. As we want to know the energy after transmission, we put the
      //MCU in sleep mode.

      //Get energy level
      if (Energy < threshold_LPSM)
      {
        //put MCU in sleep mode
        return;
      }
      else if(min_func(temperature) < threshold_CM)
      {
        //put MCU in sleep mode
        return;
      }
      else{


      }

      


      
    }
     
  }
  //veranderd2
}

float min_func(float *temperatures){
  float min = temperatures[0];
  for (size_t i = 1; i < sensorCount; i++)
  {
    if (temperatures[i]<min)
    {
      min = temperatures[i];
    }
    
  }
  return min;


}

int calcwaitingtime(float minimumTemp){
  return (minimumTemp - criticalTemp)/(maxTempSlope *3600000);
}