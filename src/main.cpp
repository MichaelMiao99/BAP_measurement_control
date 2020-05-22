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
const float maxTempSlope = 8;
const float criticalTemp = 0.5;
const int minute = 60000;


bool offSeason;
float temperature[sensorCount];



HardwareSerial Serial1(PA_10, PA_9);

void setup() {
    rtc.setClockSource(STM32RTC::LSE_CLOCK);
    rtc.begin();
    analogReadResolution(12);
    LowPower.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
    
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
            else if(sleepDuration > 15*minute) // if calculated sleepduration is larger than 15 minute, sleep for 15 minutes
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
