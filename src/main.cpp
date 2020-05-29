#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <STM32RTC.h>
#include <STM32LowPower.h>

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
const float maxTempSlope = 8/3600000;
const float criticalTemp = 0.5;
const int minute = 60000;
const int offSeasonIterations = 5;
const int offSeasonSleepDuration = 60 * minute;
const int timeUntilReceive = 3000;
float tempTemperatures[sensorCount];
bool offSeason;
float *temperature;
bool normalOperation;



HardwareSerial i2cInterface (PA_10, PA_9);
HardwareSerial UARTInterface (PB_7, PB_6);


void receive1(){
return;
  
}

void setup() {
    rtc.setClockSource(STM32RTC::LSE_CLOCK);
    rtc.begin();
    analogReadResolution(12);
    LowPower.begin();
    normalOperation = 1;
    LowPower.enableWakeupFrom(&UARTInterface, receive1);
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

  return (minimumTemp - criticalTemp)/(maxTempSlope);
}

float checkEnergy(){
    float capVoltage;
    capVoltage = analogRead(A3); //read voltage from io pin
    //Calculate energy from capvoltage
    return 1*capVoltage;
}

void shortPSM(){
    float previousEnergy, currentEnergy;
    int sleepDuration;
    currentEnergy = checkEnergy();
    if (currentEnergy < threshold_SPSM)
    {
        sleepDuration = 300000;
        LowPower.deepSleep(sleepDuration); //sleep for 5 minutes
        previousEnergy = currentEnergy;
        currentEnergy = checkEnergy();

        while (currentEnergy < threshold_SPSM)
        {
            float slope;
            slope = (currentEnergy - previousEnergy)/sleepDuration;
            sleepDuration = (threshold_SPSM-currentEnergy)/slope;
            if(sleepDuration<(minute)) // if calculated sleepduration is shorter than a minute, sleep for a minute
            {
                sleepDuration = minute;
                       
            }
            else if(sleepDuration > 15*minute) // if calculated sleepduration is larger than 15 minutes, sleep for 15 minutes
            {
                sleepDuration = 15*minute;
            }

            LowPower.deepSleep(sleepDuration);
            previousEnergy = currentEnergy;
            currentEnergy = checkEnergy();
        }
    }
    return;
    
    
    
}

void transmitOffSeason(float **tempartureArray){

  
}

void transmit(float *temperature){
  //battery, data, time, state
  
  checkEnergy();
  UARTInterface.begin(57600);
  char payload[36];
  char timeMinutes[2];
  char timeHours[2];
  char timeDay[2];
  char timeMonth[2];
  char timeYear[4];
  char batteryLevel[3];
  char data[20];
  char state;
  itoa(rtc.getMinutes(),timeMinutes,16);
  itoa(rtc.getHours(),timeHours,16);
  itoa(rtc.getDay(),timeDay,16);
  itoa(rtc.getMonth(),timeMonth,16);
  itoa(rtc.getYear(),timeYear,16);
  data = 
  
  payload = (batteryLevel | data | timeYear | timeMonth | timeDay | timeHours | timeMinutes | state);


  UARTInterface.write(buffer);
  LowPower.deepSleep(timeUntilReceive);
  UARTInterface.end();
  
}

float *tempMeasurement()
{
  
  tempTemperatures[0] = 1;
  tempTemperatures[1] = 2;
  tempTemperatures[2] = 3;
  tempTemperatures[3] = 4;
  tempTemperatures[4] = 5;
  return tempTemperatures;
  
}

void offSeasonState(){
  float *tempArray[5];

  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    tempArray[i] = tempMeasurement();
    LowPower.deepSleep(offSeasonSleepDuration);
    
  }

  transmitOffSeason(tempArray);
  return;  
}



void normalMode(){
  int sleepDuration = (min_func(temperature)-criticalTemp)/maxTempSlope;
  LowPower.deepSleep(sleepDuration);
  return;
}

void criticalMode(){
  LowPower.deepSleep(5*minute);
}

void longPSM(){
  LowPower.deepSleep(60*minute);
}



void loop() {
  // put your main code here, to run repeatedly:
    if(normalOperation == 1)
    {
      shortPSM();
      if(offSeason == 1)
      {
        offSeasonState();
      }
      else 
      {

        temperature = tempMeasurement();
        transmit(temperature);
          if((checkEnergy()) < threshold_LPSM)
          {
            longPSM();
          }
          else if (min_func(temperature) < criticalTemp)
          {
            criticalMode();
          }
          else 
          {
            normalMode();
          }
      }
    }
}