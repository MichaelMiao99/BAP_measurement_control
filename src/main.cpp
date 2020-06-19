#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <STM32RTC.h>
#include <STM32LowPower.h>

// Get the  rtc object
STM32RTC &rtc = STM32RTC::getInstance();

const int threshold_PSM = 2;
//const int threshold_CM = 1;
const int threshold_Join = 10;
const int sensorCount = 5;
const float maximumTemperatureSlope = 8.3;

const int minute = 60000; // 1 minute = 60000ms
const int offSeasonIterations = 4;
const int offSeasonSleepDuration = 60 * minute;
const int groupNumber = 1;
const int syncThresholdNormal = 20;
const int syncThresholdOffSeason = 5;

const char deviceID[9] = "";
const char spreadingFactor[5] = "";
const char nwkskey[33] = "";
const char appskey[33] = "";

int criticalTemp;
bool offSeason;
int *temperature;
bool joined;

int tsMilli;
int tsSeconds;
int tsMinutes;
int syncCounter = 0;
const float maxTempSlope = (10.0 * maximumTemperatureSlope) / 60; //10th degrees per minute

HardwareSerial UARTInterface(PB_7, PB_6);

int checkEnergy() // Reads voltage and returns number of transmissions that can be done with available energy
{
  analogWrite(PA_1, HIGH);             // Turn on energy monitoring circuit
  delay(4);                            // wait for settling time
  float capVoltage = analogRead(PA_2); //read voltage from io pin
  capVoltage = map(capVoltage, 0, 4095, 0, 1.7);
  float availableEnergy = 0.5 * 15 * sq(capVoltage);
  float currentConsumption = (24.5 + 85 * (capVoltage * 5.5 / 1.7)) * pow(10, -6);
  float energyLeft = availableEnergy - currentConsumption * 12; // calculate energy left after 12 hours of operation in current mode
  float energyPerTransmission = 0.04968;
  float numberOfTransmissions = energyLeft / energyPerTransmission; //calculate number of transmissions left
  return (int)numberOfTransmissions;
}

bool receive()
{
  if (UARTInterface.readString() == "ok") //UART transmission succesfull
  {
    LowPower.deepSleep(5000); //sleep for a maximum of 5 seconds to ensure wakeup after transmission
    String receivedMessage = UARTInterface.readString();
    UARTInterface.end();
    if (receivedMessage.startsWith("mac_rx")) //mac_rx <portno> <data>
    {
      String receivedData = receivedMessage.substring(receivedMessage.lastIndexOf(" ") + 1); //extract data from message, everything after last space
      char receiveBuffTime[9];
      char receiveBuffTs[7];
      (receivedData.substring(0, 7)).toCharArray(receiveBuffTime, 9);
      int timeInteger = strtoul(receiveBuffTime, NULL, 16); //convert hexadecimal time to integer
      (receivedData.substring(8, 13)).toCharArray(receiveBuffTs, 7);
      int timeIntegerTs = strtoul(receiveBuffTs, NULL, 16);

      rtc.setHours(((timeInteger) / 3600000) % 24); // synchronise time
      rtc.setMinutes(((timeInteger) / 60000) % 60);
      rtc.setSeconds(((timeInteger) / 1000) % 60);
      rtc.setSubSeconds((timeInteger) % 1000);

      tsMinutes = ((timeIntegerTs) / 60000) % 60; // set time slot
      tsSeconds = ((timeIntegerTs) / 1000) % 60;
      tsMilli = (timeInteger) % 1000;

      criticalTemp = uint8_t((receivedData.substring(14, 17)).toInt()); //set critical temperture
      offSeason = uint8_t((receivedData.substring(18, 19)).toInt());    //set offseason mode on or off based on received setting

      return 1;
    }
    else if (receivedMessage == "mac_tx_ok") // if no data is received, continue without changing settings
    {
      return 1;
    }
  }
  return 0;
}

bool request() //Function that is called when a response from the base station is desired.
{
  UARTInterface.begin(4096);
  UARTInterface.write("mac tx uncnf 2 FF"); //send transmission so the base station can return a time slot.
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
  UARTInterface.write(0x55); //Send 0x55 as specified in the rn2483 datasheet.

  //setup lora module over uart
  char setDeveui[24];
  char setSF[18];
  char setNwks[49];
  char setApps[49];

  strcpy(setDeveui, "mac set deveui "); // set device ID
  strcat(setDeveui, deviceID);
  UARTInterface.write(setDeveui);
  UARTInterface.readString();

  strcpy(setSF, "radio set sf "); // set spreading factor
  strcat(setSF, spreadingFactor);
  UARTInterface.write(setSF);
  UARTInterface.readString();

  strcpy(setNwks, "mac set nwkskey "); // set network session key
  strcat(setNwks, nwkskey);
  UARTInterface.write(setNwks);
  UARTInterface.readString();

  strcpy(setApps, "mac set appskey "); // set application session key
  strcat(setApps, appskey);
  UARTInterface.write(setApps);
  UARTInterface.readString();

  UARTInterface.write("mac join abp");    //send mac join command
  if (UARTInterface.readString() == "ok") //if command transmission is succesful, go to sleep till next confirmation.
  {
    LowPower.deepSleep();
    if (UARTInterface.readString() == "accepted")
    {
      return request(); // return to main loop if accepted to the network
    }
  }
  return 0;
}

int minFunc(int *temperatures) //determine the lowest temperature measured at a certain time by the 5 sensors.
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

String padLeft(String input, uint16_t desiredLength) // to make sure all inputs are the same length
{

  while (input.length() < desiredLength)
  {
    input = "0" + input;
  }
  return input;
}

void tempMeasurement()
{
  Wire.setClock(32768); // set i2c clock to 32768 kHz (max periperal clock)
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
    temperature[i] = value / 12.8; //due to roundoff error of +-0.2. Interpret data as specified by datasheet
  }
  Wire.endTransmission(); // stop transmitting
  return;
}

bool transmitOffSeason(int energyLevel, int **temperatureArray) // transmit temperature data of the measurements of 4 different times
{
  char command[(offSeasonIterations * sensorCount * 4) + 18];
  char payload[(offSeasonIterations * sensorCount * 4) + 3];
  String EL = String(int16_t(energyLevel), HEX); //convert current energy level to hexadecimal
  String concatenated = padLeft(EL, 2);          //zero pad string to comply with length requirement
  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    for (size_t j = 0; i < sensorCount; j++)
    {
      concatenated = concatenated + padLeft(String(int16_t(temperatureArray[i][j]), HEX), 4); //add all temperatures measurements to string  in hexadecimal
    }
  }

  concatenated.toCharArray(payload, (offSeasonIterations * sensorCount * 4) + 3);

  strcpy(command, "mac tx uncnf 3 ");
  strcat(command, payload);

  UARTInterface.begin(4096);
  UARTInterface.write(command);
  return receive();
}

bool transmit(int energyLevel, int *temperature) // Transmit the measured data
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
  strcat(command, payload); // create the full command

  UARTInterface.begin(4096);
  UARTInterface.write(command);
  return receive();
}

int closestPeriod() // find the maximum allowable time between measurements that detects fruit frost
{
  int minTimeToNext = (minFunc(temperature) - criticalTemp) / maxTempSlope;
  int availablePeriods[5] = {30, 20, 15, 10, 5};
  int minPeriod = 5;
  for (size_t i = 0; i < 5; i++)
  {
    if (availablePeriods[i] < minTimeToNext)
    {
      minPeriod = availablePeriods[i];
      break;
    }
  }
  return minPeriod;
}

int nextTimeSeg(int currentTimeSegment, int measurementPeriod) // Calculate the next time segment a device should measure.
{
  int availableTimeSegments[60 / measurementPeriod];
  int setAmount = measurementPeriod / 5;
  for (size_t i = 0; i < 12; i++) //make a list of all timesegments that belong to the group in this mode. Time segments from 0-11 and groups from 0-11 as well
  {
    if (i * measurementPeriod / 5 + (groupNumber % setAmount) < 12)
    {
      availableTimeSegments[i] = i * measurementPeriod / 5 + (groupNumber % setAmount); //put timesegment in array if lower than 12
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

void setup() // configure settings, join network
{
  joined = 0;
  LowPower.enableWakeupFrom(&UARTInterface, NULL);
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  rtc.begin(24);                //Set time format to 24h time format
  analogReadResolution(12);     // Set ADC accuracy to 12 bits
  LowPower.begin();             //Initialize low power functionalities
  randomSeed(analogRead(PA_4)); // use random unconnected pin to get random value
  while (joined == 0)           //try to join as long as it has not joined yet
  {
    if (checkEnergy() > threshold_Join)
    {
      LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin
      joined = join();                                   //join the LoRa network
    }
    else
    {
      LowPower.deepSleep(5 * minute);
    }
  }
}

bool powerSavingMode() // determine whether enough power is present in the system to perform 1 tranmission
{
  int previousEnergy, currentEnergy;
  int sleepDuration;
  currentEnergy = checkEnergy();
  if (currentEnergy < threshold_PSM)
  {
    sleepDuration = 5 * minute;
    LowPower.deepSleep(sleepDuration); //sleep for 5 minutes
    previousEnergy = currentEnergy;
    currentEnergy = checkEnergy();

    while (currentEnergy < threshold_PSM)
    {
      float slope;
      slope = (currentEnergy - previousEnergy) / sleepDuration;
      sleepDuration = (threshold_PSM - currentEnergy) / slope;
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
    rtc.setAlarmTime(nextHours, nextMinutes, tsSeconds, tsMilli); // set alarm to next avaiale time slot
    LowPower.deepSleep();
    return 0;
  }
  return 1;
}

void normalMode() // adaptive temperature measurements based on current energy level and temperature
{
  int measurementPeriodEL;
  int measurementPeriodTemp;
  int measurementPeriod;
  int energyLevel = checkEnergy();
  if (energyLevel >= 144)
  {
    measurementPeriodEL = 5;
  }
  if (energyLevel < 144)
  {
    measurementPeriodEL = 10;
  }
  if (energyLevel < 72)
  {
    measurementPeriodEL = 15;
  }
  if (energyLevel < 48)
  {
    measurementPeriodEL = 20;
  }
  if (energyLevel < 36)
  {
    measurementPeriodEL = 30;
  }
  tempMeasurement();
  measurementPeriodTemp = closestPeriod(); //calculate the minimum amount of groups needed for correct temperature measurement time interval
  measurementPeriod = max(measurementPeriodEL, measurementPeriodTemp);
  ///////////////////////////////////////////////////////
  //CALCULATE THE NEXT TRANSMISSION TIME/////////////////
  ///////////////////////////////////////////////////////
  int currentHours = rtc.getHours();
  int currentMinutes = rtc.getMinutes();
  int currentTimeSegment = currentMinutes / 5;
  int nextTimeSegment = nextTimeSeg(currentTimeSegment, measurementPeriod);
  int nextHours = currentHours;
  int nextMinutes = nextTimeSegment * 5 + tsMinutes;

  int transmitted = transmit(checkEnergy(), temperature); //transmit energy level and temperature data
  if (transmitted == 0)
  {
    syncCounter += 1;                       //count number of transmissions without reply
    if (syncCounter == syncThresholdNormal) //rejoin network if too many transmissions are unanswered
    {
      int synced = 0;
      while (synced == 0)
      {
        if (checkEnergy() > threshold_Join)
        {
          LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin.
          synced = request();

          //calculate next time segment using new time slot.
          currentHours = rtc.getHours();
          currentMinutes = rtc.getMinutes();
          currentTimeSegment = currentMinutes / 5;
          nextTimeSegment = nextTimeSeg(currentTimeSegment, measurementPeriod);
          nextHours = currentHours;
          nextMinutes = nextTimeSegment * 5 + tsMinutes;
        }
        else
        {
          LowPower.deepSleep(5 * minute);
        }
      }
    }
  }
  else
  {
    syncCounter = 0; //reset counter if reply is received
  }

  rtc.setAlarmTime(nextHours, nextMinutes, tsSeconds, tsMilli);
  LowPower.deepSleep();
  return;
}

void offSeasonMode() //collect 4 measurements and transmit those in one transmission
{
  int *tempArray[5];

  for (size_t i = 0; i < offSeasonIterations; i++)
  {
    LowPower.deepSleep(offSeasonSleepDuration);
    tempMeasurement();
    tempArray[i] = temperature;
  }
  //Transmit data. If more than 5 transmissions have been done without synchronisation message from base station, rejoin the network.
  int transmitted = transmitOffSeason(checkEnergy(), tempArray);
  if (transmitted == 0)
  {
    syncCounter += 1;
    if (syncCounter == syncThresholdOffSeason)
    {
      int synced = 0;
      while (synced == 0)
      {
        if (checkEnergy() > threshold_Join)
        {
          LowPower.deepSleep(int(random(1000, 5 * minute))); //sleep for a random time before trying to rejoin.
          synced = request();

          //calculate next time segment using new time slot.
        }
        else
        {
          LowPower.deepSleep(5 * minute);
        }
      }
    }
  }
  else
  {
    syncCounter = 0;
  }
  int currentHours = rtc.getHours();
  int currentMinutes = rtc.getMinutes();
  int currentTimeSegment = currentMinutes / 5;
  int nextTimeSegment = nextTimeSeg(currentTimeSegment, 60);
  int nextHours = currentHours;
  int nextMinutes = nextTimeSegment * 5 + tsMinutes;
  rtc.setAlarmTime(nextHours, nextMinutes, tsSeconds, tsMilli);
  LowPower.deepSleep();
  return;
}

void loop() //loop though the various modes
{
  if (powerSavingMode())
  {
    if (offSeason == 1)
    {
      offSeasonMode();
    }
    else
    {
      normalMode();
    }
  }
}