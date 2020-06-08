#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <STM32RTC.h>
#include <STM32LowPower.h>

// Get the  rtc object
STM32RTC &rtc = STM32RTC::getInstance();
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

const float maxTempSlope = 80 / 3600000;
const int minute = 60000;
const int offSeasonIterations = 5;
const int offSeasonSleepDuration = 60 * minute;
const int timeUntilReceive = 3000;
const int groupNumber = 1;
const int energyUnit = 100;
const int maxRetransmit = 5;

int criticalTemp;
int timeSlotOffset;
float tempTemperatures[sensorCount];
bool offSeason;
float *temperature;
bool normalOperation;
bool joined;

int tsMilli;
int tsSeconds;
int tsMinutes;

HardwareSerial i2cInterface(PA_10, PA_9);
HardwareSerial UARTInterface(PB_7, PB_6);

void receive1()
{
}

bool receive()
{
  if (UARTInterface.readString() == "ok")
  {
    LowPower.deepSleep(5000); //sleep for a maximum of 5 seconds to ensure wakup after transmission
                              //mac_rx <portno> <data>
    String receivedMessage = UARTInterface.readString();
    UARTInterface.end();
    if (receivedMessage.startsWith("mac_rx"))
    { // adjust for different port numbers
      String receivedData = receivedMessage.substring(receivedMessage.lastIndexOf(" ") + 1);

      rtc.setHours(uint8_t((receivedData.substring(0, 1)).toInt()));
      rtc.setMinutes(uint8_t((receivedData.substring(2, 3)).toInt()));
      rtc.setSeconds(uint8_t((receivedData.substring(4, 5)).toInt()));
      rtc.setSubSeconds(uint8_t((receivedData.substring(6, 9)).toInt()));
      tsMinutes = uint8_t((receivedData.substring(10, 11)).toInt());
      tsSeconds = uint8_t((receivedData.substring(12, 13)).toInt());
      tsMilli = uint8_t((receivedData.substring(14, 17)).toInt());
      criticalTemp = uint8_t((receivedData.substring(18, 21)).toInt());
      offSeason = uint8_t((receivedData.substring(22, 23)).toInt());

      return 1;
    }
    else if (receivedMessage == "mac_tx_ok")
    {
      return 1;
    }
  }
  return 0;
}
int checkEnergy()
{
  float capVoltage;
  capVoltage = analogRead(A3); //read voltage from io pin
  //Calculate energy from capvoltage
  return (int)1000 * capVoltage; //here the 1000 is put there so that an integer value with the data is obtained. The comma should be moved upon reception.
}

void request() //Function that is called when a response from the base station is desired.
{
  UARTInterface.begin(57600);
  UARTInterface.write("mac tx uncnf 2 FF"); //send transmission on port 2 so that the base station knows that it should send a time slot to the device.
}

bool join()
{
  //checkenergy
  //setup lora module over uart

  UARTInterface.begin(57600);
  UARTInterface.write("mac set deveui FFFFFFFF");
  UARTInterface.write("radio set sf sf7"); //enter spreadings factor (sf7,sf8,sf9,sf10,sf11 or sf12)
  UARTInterface.write("mac set nwkskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  UARTInterface.write("mac set appskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  tsMilli = rtc.getSubSeconds();
  tsSeconds = rtc.getSeconds();
  tsMinutes = rtc.getMinutes();
  LowPower.enableWakeupFrom(&UARTInterface, receive1);
  UARTInterface.write("mac join abp");
  if (UARTInterface.readString() == "ok")
  {
    LowPower.deepSleep();
    if (UARTInterface.readString() == "accepted")
    {
      request();
      return 1;
    }
  }

  return 0;
}

void setup()
{
  joined = 0;
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  rtc.begin(24);
  analogReadResolution(12);
  LowPower.begin();
  normalOperation = 1;
  while (joined == 0)
  {
    if (checkEnergy() > threshold_Join)
    {
      LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin.
      joined = join();
    }
    else
    {
      LowPower.deepSleep(5 * minutes);
    }
  }
}

float min_func(float *temperatures)
{
  float min = temperatures[0];
  for (size_t i = 1; i < sensorCount; i++)
  {
    if (temperatures[i] < min)
    {
      min = temperatures[i];
    }
  }
  return min;
}

int calcwaitingtime(float minimumTemp)
{
  return (minimumTemp - criticalTemp) / (maxTempSlope);
}

void shortPSM()
{
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
      slope = (currentEnergy - previousEnergy) / sleepDuration;
      sleepDuration = (threshold_SPSM - currentEnergy) / slope;
      if (sleepDuration < (minute)) // if calculated sleepduration is shorter than a minute, sleep for a minute
      {
        sleepDuration = minute;
      }
      else if (sleepDuration > 15 * minute) // if calculated sleepduration is larger than 15 minutes, sleep for 15 minutes
      {
        sleepDuration = 15 * minute;
      }

      LowPower.deepSleep(sleepDuration);
      previousEnergy = currentEnergy;
      currentEnergy = checkEnergy();
    }
  }
  return;
}

void transmitOffSeason(float **tempartureArray)
{
}

String padLeft(String input, uint16_t desiredLength)
{

  while (input.length() < desiredLength)
  {
    input = "0" + input;
  }
  return input;
}

bool transmit(int energyLevel, float *temperature)
{
  char command[38];
  char payload[23];
  String EL = String(int16_t(energyLevel), HEX);
  String t1 = String(int16_t(temperature[0]), HEX);
  String t2 = String(int16_t(temperature[1]), HEX);
  String t3 = String(int16_t(temperature[2]), HEX);
  String t4 = String(int16_t(temperature[3]), HEX);
  String t5 = String(int16_t(temperature[4]), HEX);
  String concatenated = padLeft(EL, 2) + padLeft(t1, 4) + padLeft(t2, 4) + padLeft(t3, 4) + padLeft(t4, 4) + padLeft(t5, 4);
  concatenated.toCharArray(payload, 23);

  strcpy(command, "mac tx uncnf 1 ");
  strcat(command, payload);

  UARTInterface.begin(57600);
  UARTInterface.write(command);
  return receive();
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

void offSeasonState()
{
  float *tempArray[5];

  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    tempArray[i] = tempMeasurement();
    LowPower.deepSleep(offSeasonSleepDuration);
  }

  transmitOffSeason(tempArray);
  return;
}

void normalMode()
{
  int sleepDuration = (min_func(temperature) - criticalTemp) / maxTempSlope;
  LowPower.deepSleep(sleepDuration);
  return;
}

void criticalMode()
{
  int numberOfGroups;
  int energyLevel = checkEnergy();
  if (energyLevel < 144 * energyUnit)
  {
    numberOfGroups = 1;
  }
  else if (energyLevel < 72 * energyUnit)
  {
    numberOfGroups = 2;
  }
  else if (energyLevel < 48 * energyUnit)
  {
    numberOfGroups = 3;
  }
  else if (energyLevel < 36 * energyUnit)
  {
    numberOfGroups = 4;
  }
  else if (energyLevel < 24 * energyUnit)
  {
    numberOfGroups = 6;
  }
  else if (energyLevel < 12 * energyUnit)
  {
    numberOfGroups = 12;
  }
  ///////////////////////////////////////////////////////
  //CALCULATE THE NEXT TRANSMISSION TIME/////////////////
  ///////////////////////////////////////////////////////
  int currentHours = rtc.getHours();
  int currentMinutes = rtc.getMinutes();
  int timeSegment = currentMinutes / 5;
  int nextTimeSegment = timeSegment + numberOfGroups;
  int nextHours = currentHours;
  if (nextTimeSegment >= 12)
  {
    nextHours = (currentHours + 1) % 24;
    nextTimeSegment = nextTimeSegment % 12;
  }
  int nextMinutes = nextTimeSegment * 5 + tsMinutes;

  //transceive meuk
  if (transmit(energyLevel, temperature) == 0)
  {
    joined = 0;
    while (joined == 0)
    {
      if (checkEnergy() > threshold_Join)
      {
        LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin.
        joined = join();
      }
      else
      {
        LowPower.deepSleep(5 * minutes);
      }
    }
  }

  rtc.setAlarmTime(nextHours, nextMinutes, tsSeconds, tsMilli);
  LowPower.deepSleep();
  return;
}

void longPSM()
{
  LowPower.deepSleep(60 * minute);
}

void loop()
{
  shortPSM();
  if (offSeason == 1)
  {
    offSeasonState();
  }
  else
  {

    temperature = tempMeasurement();
    //transmit(temperature);
    if ((checkEnergy()) < threshold_LPSM)
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