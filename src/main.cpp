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

const int threshold_SPSM = 3;
const int threshold_LPSM = 2;
const int threshold_CM = 1;
const int threshold_Join = 2;
const int sensorCount = 5;

const float maxTempSlope = 8/3600000;
const float criticalTemp = 0.5;
const int minute = 60000;
const int offSeasonIterations = 5;
const int offSeasonSleepDuration = 60 * minute;
const int timeUntilReceive = 3000;
const int groupNumber = 1;
const int energyUnit = 100;

int timeSlotOffset;
float tempTemperatures[sensorCount];
bool offSeason;
float *temperature;
bool normalOperation;
bool joined;



int offsetMilli;
int offsetSecond;
int offsetMinute;


HardwareSerial i2cInterface (PA_10, PA_9);
HardwareSerial UARTInterface (PB_7, PB_6);


void receive1(){
return;
  
}
bool join(){
  //checkenergy
  //setup lora module over uart
  UARTInterface.begin(57600);
  UARTInterface.write("mac set deveui FFFFFFFF");
  UARTInterface.write("radio set sf sf7"); //enter spreadings factor (sf7,sf8,sf9,sf10,sf11 or sf12)
  UARTInterface.write("mac set nwkskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  UARTInterface.write("mac set appskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  offsetMilli = rtc.getSubSeconds();
  offsetSecond = rtc.getSeconds();
  offsetMinute = rtc.getMinutes();
  LowPower.enableWakeupFrom(&UARTInterface, receive1);
  UARTInterface.write("mac join abp");
  LowPower.deepSleep(); // sleep until command comes back
  if(UARTInterface.readString() == "ok")
  {
    LowPower.deepSleep();
    if(UARTInterface.readString() == "accepted"){
    return 1;
    }
  }
  return 0;
}

void setup() {
    joined = 0;
    rtc.setClockSource(STM32RTC::LSE_CLOCK);
    rtc.begin();
    analogReadResolution(12);
    LowPower.begin();
    normalOperation = 1;
    while(joined == 0){
      if (checkEnergy() > threshold_Join){
        joined = join();
      }
      else{
        LowPower.deepSleep(5*minutes);
      }
    }
    join();
    
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

int checkEnergy(){
    float capVoltage;
    capVoltage = analogRead(A3); //read voltage from io pin
    //Calculate energy from capvoltage
    return (int) 1000*capVoltage; //here the 1000 is put there so that an integer value with the data is obtained. The comma should be moved upon reception.
}

void shortPSM(){
    int previousEnergy, currentEnergy;
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

  UARTInterface.begin(57600);
  char payload[36];
  char timeMinutes[3];
  char timeHours[3];
  char timeDay[3];
  char timeMonth[3];
  char timeYear[5];
  char energyLevel[4];
  char data[21];
  char state[2];
  char command[44];
  //char txcommand[43]d
  itoa(rtc.getMinutes(),timeMinutes,16);
  itoa(rtc.getHours(),timeHours,16);
  itoa(rtc.getDay(),timeDay,16);
  itoa(rtc.getMonth(),timeMonth,16);
  itoa(rtc.getYear(),timeYear,16);
  itoa(checkEnergy(),energyLevel,16);
  
  strcpy(payload, energyLevel);
  strcat(payload, data);
  strcat(payload, timeYear);
  strcat(payload, timeMonth);
  strcat(payload, timeDay);
  strcat(payload, timeHours);
  strcat(payload, timeMinutes);
  strcat(payload, state);
  strcpy(command, "mac tx ");
  strcat(command, payload);
  Serial.write(command);
  UARTInterface.write(command);
  LowPower.deepSleep(timeUntilReceive);
  UARTInterface.end();
  // 2 receive windows
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



int timeToNextTransmission(int numberOfGroups){
  int currentMinutes = rtc.getMinutes();
  int currentSeconds = rtc.getSeconds();
  int currentSubseconds = rtc.getSubSeconds();

  int timeSegment = currentMinutes/5;
  int nextTimeSegment = (timeSegment + numberOfGroups) % 12;
  
}

void criticalMode(){
  
  int sleepDuration;   
  int numberOfGroups;
  int energyLevel = checkEnergy();
  if(energyLevel < 144 * energyUnit){
    numberOfGroups = 1;
  }
  else if(energyLevel < 72 * energyUnit){
    numberOfGroups = 2;
  }
  else if(energyLevel < 48 * energyUnit){
    numberOfGroups = 3;
  }
  else if(energyLevel < 36 * energyUnit){
    numberOfGroups = 4;
  }
  else if(energyLevel < 24 * energyUnit){
    numberOfGroups = 6;
  }
  else if(energyLevel < 12 * energyUnit){
    numberOfGroups = 12;
  }
  
  sleepDuration = timeToNextTransmission(numberOfGroups);

  LowPower.deepSleep(5*minute);
}

void longPSM(){
  LowPower.deepSleep(60*minute);
}

void loop(){

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
          else if (min_func(temperature) < threshold_CM)
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