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

const float maxTempSlope = 80 / 60; //10th degrees per minute
const int minute = 60000;
const int offSeasonIterations = 5;
const int offSeasonSleepDuration = 60 * minute;
const int timeUntilReceive = 3000;
const int groupNumber = 1;
const int energyUnit = 100;
const int maxRetransmit = 5;

int criticalTemp;
int timeSlotOffset;
int tempTemperatures[sensorCount];
bool offSeason;
int *temperature;
bool joined;

int tsMilli;
int tsSeconds;
int tsMinutes;

HardwareSerial UARTInterface(PB_7, PB_6);
//TwoWire i2cInterface(PA_9,PA_10);

//void receive1()
//{
//}

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
      char receiveBuffTime[9];
      char receiveBuffTs[7];
      (receivedData.substring(0, 7)).toCharArray(receiveBuffTime, 9);
      int timeInteger = strtoul(receiveBuffTime, NULL, 16);
      (receivedData.substring(8, 13)).toCharArray(receiveBuffTs, 7);
      int timeIntegerTs = strtoul(receiveBuffTs, NULL, 16);

      rtc.setHours(((timeInteger) / 3600000) % 24);
      rtc.setMinutes(((timeInteger) / 60000) % 60);
      rtc.setSeconds(((timeInteger) / 1000) % 60);
      rtc.setSubSeconds((timeInteger) % 1000);

      tsMinutes = ((timeIntegerTs) / 60000) % 60;
      tsSeconds = ((timeIntegerTs) / 1000) % 60;
      tsMilli = (timeInteger) % 1000;

      criticalTemp = uint8_t((receivedData.substring(14, 17)).toInt());
      offSeason = uint8_t((receivedData.substring(18, 19)).toInt());

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
  float capVoltage = analogRead(PA_1); //read voltage from io pin
  //Calculate energy from capvoltage
  float availableEnergy = 0.5 * 15 * sq(capVoltage);
  float currentConsumption = (23.2 + 85 * (capVoltage*5.5/1.7)) * pow(10,-6);
  float energyLeft = availableEnergy - currentConsumption *12;
  float energyPerTransmission = 0.04968;
  float numberOfTransmissions = energyLeft / energyPerTransmission;
  return (int) numberOfTransmissions; 
}

bool request() //Function that is called when a response from the base station is desired.
{
  UARTInterface.begin(4096);
  UARTInterface.write("mac tx uncnf 2 FF"); //send transmission on port 2 so that the base station knows that it should send a time slot to the device.
  return receive();
}

bool join()
{
  //Set RN2483 module's baudrate to match system clock
  UARTInterface.end();
  pinMode(PA_9, OUTPUT);
  digitalWrite(PA_9, LOW); //send break condition to rn2483 to trigger automatic baud rate detection
  delay(1);
  UARTInterface.begin(4096);
  UARTInterface.write(0x55);

  //setup lora module over uart
  UARTInterface.write("mac set deveui FFFFFFFF");
  UARTInterface.readString();
  UARTInterface.write("radio set sf sf7"); //enter spreadings factor (sf7,sf8,sf9,sf10,sf11 or sf12)
  UARTInterface.readString();
  UARTInterface.write("mac set nwkskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  UARTInterface.readString();
  UARTInterface.write("mac set appskey AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  UARTInterface.readString();

  UARTInterface.write("mac join abp");
  if (UARTInterface.readString() == "ok")
  {
    LowPower.deepSleep();
    if (UARTInterface.readString() == "accepted")
    {
      return request();
    }
  }
  return 0;
}

void setup()
{
  joined = 0;
  LowPower.enableWakeupFrom(&UARTInterface, NULL);
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  rtc.begin(24);            //Set time format to 24h time format
  analogReadResolution(12); // Set ADC accuracy to 12 bits
  LowPower.begin();         //Initialize low power functionalities
  while (joined == 0)       //try to join as long as it has not joined yet
  {
    if (checkEnergy() > threshold_Join)
    {
      LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin.
      joined = join();                                   //join the LoRa network
    }
    else
    {
      LowPower.deepSleep(5 * minutes);
    }
  }
}

int min_func(int *temperatures)
{
  int min = temperatures[0];
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
int nextTimeSeg(int currentTimeSegment, int measurementPeriod) // Calculate the next time segment a device should measure.
{
  int availableTimeSegments[60 / measurementPeriod];
  int groupAmount = measurementPeriod / 5;
  for (size_t i = 0; i < 12; i++) //make a list of all timesegments that belong to the group in this mode. Time segments from 0-11 and groups from 1-12
  {
    if (i * measurementPeriod / 5 + ((groupNumber - 1) % groupAmount) < 12)
    {
      availableTimeSegments[i] = i * measurementPeriod / 5 + ((groupNumber - 1) % groupAmount); //put timesegment in array if lower than 12
    }
    else
    {
      break; // stop earlier if subsequent timeslots will be higher or equal to 12
    }
  }

  for (int i = 0; i < 60 / measurementPeriod; i++) // determine what timesegment comes next
  {
    if (availableTimeSegments[i] > currentTimeSegment) // since availableTimeSegments is in ascending order, the first time segment that is larger than the current time segment is chosen
    {
      return availableTimeSegments[i];
    }
  }
  return availableTimeSegments[0]; //If the next time slot is after the passing of the hour, return the first segment in the hour.
}
bool shortPSM()
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
    int currentHours = rtc.getHours();
    int currentMinutes = rtc.getMinutes();
    int currentTimeSegment = currentMinutes / 5;
    int nextTimeSegment = nextTimeSeg(currentTimeSegment, 12);
    int nextHours = currentHours;
    int nextMinutes = nextTimeSegment * 5 + tsMinutes;
    rtc.setAlarmTime(nextHours, nextMinutes, tsSeconds, tsMilli);
    LowPower.deepSleep();
    return 0;
  }
  return 1;
}

String padLeft(String input, uint16_t desiredLength)
{

  while (input.length() < desiredLength)
  {
    input = "0" + input;
  }
  return input;
}

bool transmitOffSeason(int energyLevel, int **temperatureArray)
{
  char command[(offSeasonIterations * sensorCount * 4) + 18];
  char payload[(offSeasonIterations * sensorCount * 4) + 3];
  String EL = String(int16_t(energyLevel), HEX);
  String concatenated = padLeft(EL, 2);
  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    for (size_t j = 0; i < sensorCount; j++)
    {
      concatenated = concatenated + padLeft(String(int16_t(temperatureArray[i][j]), HEX), 4);
    }
  }

  concatenated.toCharArray(payload, (offSeasonIterations * sensorCount * 4) + 3);

  strcpy(command, "mac tx uncnf 3 ");
  strcat(command, payload);

  UARTInterface.begin(57600);
  UARTInterface.write(command);
  return receive();
}

bool transmit(int energyLevel, int *temperature)
{
  char command[sensorCount * 4 + 18];
  char payload[sensorCount * 4 + 3];
  String EL = String(int16_t(energyLevel), HEX);
  String concatenated = padLeft(EL, 2);
  for (size_t i = 0; i < 5; i++)
  {
    concatenated = concatenated + padLeft(String(int16_t(temperature[i]), HEX), 4);
  }

  concatenated.toCharArray(payload, sensorCount * 4 + 3);

  strcpy(command, "mac tx uncnf 1 ");
  strcat(command, payload);

  UARTInterface.begin(57600);
  UARTInterface.write(command);
  return receive();
}

void tempMeasurement()
{
  Wire.setClock(100000);
  Wire.setSDA(PA_10);
  Wire.setSCL(PA_9);
  Wire.begin();
  for (int i = 0; i < sensorCount; i++)
  {
    Wire.beginTransmission(0x44 + i); // transmit to device
    Wire.write(0x1);                  // select configuration register
    Wire.write(0b11000000);           //set configuration registers (select one-shot mode)
    Wire.write(0b10000000);

    Wire.beginTransmission(0x44 + i); // select tvalue register
    Wire.write(0x0);

    Wire.beginTransmission(0x44 + i);
    int16_t MSB = Wire.read() * 256;
    int16_t LSB = Wire.read();
    int16_t value = MSB + LSB;
    temperature[i] = value / 12.8; //due to roundoff error of +-0.2
  }
  Wire.endTransmission(); // stop transmitting
  return;
}

void offSeasonState()
{
  int *tempArray[5];

  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    LowPower.deepSleep(offSeasonSleepDuration);
    tempMeasurement();
    tempArray[i] = temperature;
  }

  if (transmitOffSeason(checkEnergy(), tempArray) == 0)
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

  return;
}

int closestFrequency()
{
  int minTimeToNext = (min_func(temperature) - criticalTemp) / maxTempSlope;
  int availablePeriods[6] = {60, 30, 20, 15, 10, 5};
  int minPeriod = 5;
  for (size_t i = 0; i < 6; i++)
  {
    if (availablePeriods[i] < minTimeToNext)
    {
      minPeriod = availablePeriods[i];
      break;
    }
  }
  return minPeriod;
}



void normalMode()
{
  int measurementPeriodEL;
  int measurementPeriodTemp;
  int measurementPeriod;
  int energyLevel = checkEnergy();
  if (energyLevel < 144 * energyUnit)
  {
    measurementPeriodEL = 5;
  }
  else if (energyLevel < 72 * energyUnit)
  {
    measurementPeriodEL = 10;
  }
  else if (energyLevel < 48 * energyUnit)
  {
    measurementPeriodEL = 15;
  }
  else if (energyLevel < 36 * energyUnit)
  {
    measurementPeriodEL = 20;
  }
  else if (energyLevel < 24 * energyUnit)
  {
    measurementPeriodEL = 30;
  } 
  else if (energyLevel < 12 * energyUnit)
  {
    measurementPeriodEL = 60;
  }
  tempMeasurement();
  measurementPeriodTemp = closestFrequency(); //calculate the minimum amount of groups needed for correct temperature measurement time interval
  measurementPeriod = max(measurementPeriodEL, measurementPeriodTemp);
  ///////////////////////////////////////////////////////
  //CALCULATE THE NEXT TRANSMISSION TIME/////////////////
  ///////////////////////////////////////////////////////
  int currentHours = rtc.getHours();
  int currentMinutes = rtc.getMinutes();
  int currentTimeSegment = currentMinutes / 5;
  int nextTimeSegment = nextTimeSeg(currentTimeSegment, measurementPeriod);
  int nextHours = currentHours;
  //if (nextTimeSegment >= 12) // dit hoeft niet meer vgm
  //{
  //  nextHours = (currentHours + 1) % 24;
  //  nextTimeSegment = nextTimeSegment % 12;
  //}
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
  if (shortPSM())
  {
    if (offSeason == 1)
    {
      offSeasonState();
    }
    else
    {
      normalMode();
    }
  }
}