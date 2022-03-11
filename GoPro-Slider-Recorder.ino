/**************************************************************************
  GoPro Slider Recorder

  Original Code:  2021-05-10

  Tom Rolander, MSEE
  Mentor, Circuit Design & Software
  Miller Library, Fabrication Lab
  Hopkins Marine Station, Stanford University,
  120 Ocean View Blvd, Pacific Grove, CA 93950
  +1 831.915.9526 | rolander@stanford.edu

 **************************************************************************/

/**************************************************************************

  To Do:
  - Comment and clean up code
  - AFter 09 first 00 errors then OK ?
  - Set timer for script execution, and show time
  - Adjust time for DST

 **************************************************************************/

#define PROGRAM "GoPro Slider Recorder"
#define VERSION "Ver 0.9 2022-03-10"

#define DEBUG_OUTPUT 1

#define DO_LOCAL_BLE  0
#define DO_REMOTE_BLE 1

// Smartphone- or tablet-activated timelapse camera slider.
// Uses the following Adafruit parts:
//
// Arduino Uno R3 or similar (adafruit.com/product/50 or #2488)
// Bluefruit LE SPI Friend (#2633)
// Motor/Stepper/Servo Shield v2 (#1438)
// NEMA-17 Stepper motor (#324)
// Miscellaneous hardware and 3D-printed parts; see guide for full list.
//
// Needs Adafruit_BluefruitLE_nRF51 and Adafruit_MotorShield libs:
// github.com/adafruit
// Use Adafruit Bluefruit LE app for iOS or Android to control timing.
// Buttons 1-4 select interpolation mode (linear vs. various ease in/out).
// Up/down select speed. Left = home. Right = start slider.

#include <SPI.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_MotorShield.h>

#if DO_LOCAL_BLE
#include <Adafruit_BluefruitLE_SPI.h>
#include <Adafruit_BluefruitLE_UART.h>
#endif

#include "BluefruitConfig.h"

SoftwareSerial y_axisSerial(3, 2);

SoftwareSerial espSerialGoPro(5, 4);

SoftwareSerial espSerialPhone(7, 6);

char espSerialPhoneBuffer[128] = "";


bool bPhotoMode = false;

long lStartTimeMS = 0;
long lWaitTimeMS = 0;

bool  bDoStartTime = false;
char  sStartTime[6] = "";

char sCurrentNTP[20] = "";

int iSteps = 0;

int iXaxis = 0;
int iYaxis = 0;
int iWellplate = 1;
int iWellplateCell = 1;

struct WellplateCoord
{
  int x;
  int y;
};
static struct WellplateCoord WellplatesCoords[6] =
{
  {  0,  0},
  {360,  0},
  {720,  0},
  {  0, 258},
  {360, 258},
  {720, 258}
};

#define RECORDING_TIME_5_SEC (5000L + 1000L)
#define RECORDING_TIME_3_MIN (180000L + 1000L)
#define RECORDING_TIME_5_MIN (300000L + 1000L)
#define RECORDING_TIME_5_SEC_STRING "5 Sec"
#define RECORDING_TIME_3_MIN_STRING "3 Min"
#define RECORDING_TIME_5_MIN_STRING "5 Min"
#define DEFAULT_RECORDING_TIME_MS RECORDING_TIME_5_SEC

#define GOPRO_CONNECT_TIMEOUT 30000L

#define CHECK_NTP 30000L

long iTimeDelay = DEFAULT_RECORDING_TIME_MS;
long lCurrentTimeDelay = DEFAULT_RECORDING_TIME_MS;
char sRecordingTime[32] = RECORDING_TIME_5_SEC_STRING;

//#define DEFAULT_CELL_STEP_SIZE 116
//#define DEFAULT_GAP_STEP_SIZE (116+30)

#define DEFAULT_CELL_STEP_SIZE 110
#define DEFAULT_GAP_STEP_SIZE (110+30)

int iShowCommand = true;

int iGoProEnabled = true;


char sExecuteScript[128] = "";

#if DO_LOCAL_BLE
// Bluefruit config --------------------------------------------------------
Adafruit_BluefruitLE_SPI ble(8, 7, 6); // CS, IRQ, RST pins
//Adafruit_BluefruitLE_SPI ble(8, 7, -1); // CS, IRQ, RST pins
#endif

// Stepper motor config ----------------------------------------------------
#define STEPPER_STEPS 1100 // Length of slider
#define STEPPER_RPM 20
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
// Stepper motor w/200 steps/revolution (1.8 degree) on port 2 (M3 & M4)
Adafruit_StepperMotor *motor = AFMS.getStepper(200, 2);

// Setup function - runs once at startup -----------------------------------
void setup(void) {
  Serial.begin(115200);
  delay(5000);

  y_axisSerial.begin(1200);
  espSerialGoPro.begin(1200);
  espSerialPhone.begin(1200);

  delay(5000);
  Serial.println("");
  Serial.println(F(PROGRAM));
  Serial.println(F(VERSION));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED steady on during init

#if DO_LOCAL_BLE  
  if (!ble.begin(false))
    for (;;); // BLE init error? LED on forever
  ble.echo(false);
#endif

  Serial.println(F("Please use Adafruit Bluefruit LE app to connect in UART mode"));
  Serial.println(F("Then Enter characters to send to Bluefruit"));
  Serial.println();

#if DO_LOCAL_BLE  
  ble.verbose(false);  // debug info is a little annoying after this point!

  /* Wait for connection */
  while (! ble.isConnected()) {
    delay(500);
  }
#endif

  delay(10000);

  SendString_ble_F(F("GoPro Slider Recorder\nVer "));
  SendString_ble(VERSION);
  SendString_ble_F(F("\n"));
  HelpDisplay();

  AFMS.begin(1600);
//  AFMS.begin();
  motor->setSpeed(STEPPER_RPM);
  motor->release(); // Allow manual positioning at start
  digitalWrite(LED_BUILTIN, LOW); // LED off = successful init
}

// Loop function - repeats forever -----------------------------------------
void loop(void)
{
  int bCommandReceived = false;
  int bUnrecognizedCommand = false;
  int iCommand = 0;

  if (iShowCommand)
  {
    iShowCommand = false;
    if (bPhotoMode)
      SendString_ble_F(F("<Photo "));
    else
    {
      SendString_ble_F(F("<Rec "));
      SendString_ble(sRecordingTime);
    }
    SendString_ble_F(F(" GoPro "));
    if (iGoProEnabled)
      SendString_ble_F(F("On"));
    else
      SendString_ble_F(F("Off"));
    SendString_ble_F(F("  COMMAND>"));
  }

  bCommandReceived = GetCommand();
  if (bCommandReceived)
  {
    espSerialPhoneBuffer[0] = '\0';
    int iNext = 0;
    while (espSerialPhone.available() > 0)
    {
      espSerialPhoneBuffer[iNext++] = espSerialPhone.read();
    }

#if DEBUG_OUTPUT
Serial.print(espSerialPhoneBuffer);
#endif
    
    {
      char chCommand = toupper(espSerialPhoneBuffer[0]);

      if (isDigit(espSerialPhoneBuffer[0]) && isDigit(espSerialPhoneBuffer[1]))
      {
        iCommand = (int)(((char)espSerialPhoneBuffer[0] - '0') * 10) + (int)(((char)espSerialPhoneBuffer[1] - '0'));
        switch (iCommand)
        {
          case 0: // Start Recording cells
            if (strlen(sExecuteScript) == 0)
            {
              SendString_ble_F(F("Enter script 'X=wrd'\n"));
            }
            else
            {
              if (bPhotoMode)
                SendString_ble_F(F("Start photos of cells\n"));
              else
                SendString_ble_F(F("Start recording cells\n"));
#if DEBUG_OUTPUT
              Serial.println(F("Start recording cells"));
#endif
              int iRetCode = ExecuteScript();
            }
            break;

          case 1:
            SendString_ble_F(F("Recording time 5 sec\n"));
#if DEBUG_OUTPUT
            Serial.println(F("Recording time 5 sec"));
#endif
            lCurrentTimeDelay = RECORDING_TIME_5_SEC;
            strcpy(sRecordingTime, RECORDING_TIME_5_SEC_STRING);
            break;

          case 2:
            SendString_ble_F(F("Recording time 3 min\n"));
#if DEBUG_OUTPUT
            Serial.println(F("Recording time 3 min"));
#endif
            lCurrentTimeDelay = RECORDING_TIME_3_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_3_MIN_STRING);
            break;

          case 3:
            SendString_ble_F(F("Recording time 5 min\n"));
#if DEBUG_OUTPUT
            Serial.println(F("Recording time 5 min"));
#endif
            lCurrentTimeDelay = RECORDING_TIME_5_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_5_MIN_STRING);
            break;

          case 4:
            SendString_ble_F(F("Disable GoPro, slider only\n"));
#if DEBUG_OUTPUT
            Serial.println(F("Disable GoPro, slider only"));
#endif
            iGoProEnabled = false;
            break;

          case 5:
            SendString_ble_F(F("Enable GoPro\n"));
#if DEBUG_OUTPUT
            Serial.println(F("Enable GoPro"));
#endif
            iGoProEnabled = true;
            break;

          case 6:
            bPhotoMode = false;
            break;
            
          case 7:
            bPhotoMode = true;
            break;
            
          case 8:
            if (GoProConnect() == true)
            {
              GoProDisconnect();
              SendString_ble_F(F("Connection successful\n"));
            }
            else
            {
              SendString_ble_F(F("->GoPro connection **FAILED**\n  Manually reset GoPro!\n"));      
            }
            break;

          case 9:
          {
            if (GetNTP(sCurrentNTP, true) == false)
              SendString_ble_F(F("Failed to get NTP\n"));
            break;
          }

          case 10:
            SendString_ble(sExecuteScript);
            SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
            Serial.println(sExecuteScript);
#endif
            break;
            
          case 11:
          {
            bool bStartTimeOK = false;
            if (isDigit(espSerialPhoneBuffer[3]) &&
                isDigit(espSerialPhoneBuffer[4]) &&
                isDigit(espSerialPhoneBuffer[6]) &&
                isDigit(espSerialPhoneBuffer[7]) &&
                espSerialPhoneBuffer[5] == ':')
            {
              strncpy(sStartTime, &espSerialPhoneBuffer[3], 5);
              sStartTime[5] = '\0';
              bStartTimeOK = true;
            }

            if (bStartTimeOK == false)
              SendString_ble_F(F("Bad start time, expected HH:MM\n"));
            else
              SendString_ble_F(F("Set start time = "));
              SendString_ble(sStartTime);
              SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
            Serial.print("Set start time = ");
            Serial.println(sStartTime);
#endif
            break;
          }
                      
          case 12:
              SendString_ble_F(F("Start time = "));
              SendString_ble(sStartTime);
              SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
              Serial.print("Set start time = ");
              Serial.println(sStartTime);
#endif
              break;
            
          case 13:
            if (strlen(sExecuteScript) == 0)
            {
              SendString_ble_F(F("Script required!\n"));
              break;
            }
            if (strlen(sStartTime) == 0)
            {
              SendString_ble_F(F("Start time required!\n"));
              break;
            }
            bDoStartTime = true;
            lWaitTimeMS = millis();
            SendString_ble_F(F("Recording at start time = "));
            SendString_ble(sStartTime);
            SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
            Serial.print("Recording at start time = ");
            Serial.println(sStartTime);
#endif
            break;
            
          case 14:
            SendString_ble_F(F("Turn ON daylight savings\n"));
            espSerialGoPro.print("3");
#if DEBUG_OUTPUT
            Serial.print(F("Turn ON daylight savings"));
            Serial.println(sStartTime);
#endif
            break;
            
          case 15:
            SendString_ble_F(F("Turn OFF daylight savings\n"));
            espSerialGoPro.print("4");
#if DEBUG_OUTPUT
            Serial.print(F("Turn OFF daylight savings"));
            Serial.println(sStartTime);
#endif
            break;
            
          case 99:  // Abort Recording cells
            // handled separately below
            break;

          default:
            bUnrecognizedCommand = true;
            break;
        }

      }
      else if (espSerialPhoneBuffer[0] == '8')
      {
        espSerialPhoneBuffer[0] = espSerialPhoneBuffer[1];
        espSerialPhoneBuffer[1] = 0;
        espSerialGoPro.print(espSerialPhoneBuffer);
#if DEBUG_OUTPUT
        Serial.print("Sending GoPro Command: ");
        Serial.println(espSerialPhoneBuffer);
#endif
        SendString_ble_F(F("Sending GoPro Command: "));
        SendString_ble(espSerialPhoneBuffer);
        SendString_ble_F(F("\n"));
      }
      else if (chCommand == 'F' && isDigit(espSerialPhoneBuffer[1]) && isDigit(espSerialPhoneBuffer[2]) && isDigit(espSerialPhoneBuffer[3]))
      {
        iSteps = (espSerialPhoneBuffer[1] - '0') * 100;
        iSteps += (espSerialPhoneBuffer[2] - '0') * 10;
        iSteps += (espSerialPhoneBuffer[3] - '0');

        GoProMove(iXaxis, iYaxis + iSteps, true);
      }
      else if (chCommand == 'B' && isDigit(espSerialPhoneBuffer[1]) && isDigit(espSerialPhoneBuffer[2]) && isDigit(espSerialPhoneBuffer[3]))
      {
        iSteps = (espSerialPhoneBuffer[1] - '0') * 100;
        iSteps += (espSerialPhoneBuffer[2] - '0') * 10;
        iSteps += (espSerialPhoneBuffer[3] - '0');

        GoProMove(iXaxis, iYaxis - iSteps, true);
      }
      else if (chCommand == 'L' && isDigit(espSerialPhoneBuffer[1]) && isDigit(espSerialPhoneBuffer[2]) && isDigit(espSerialPhoneBuffer[3]))
      {
        iSteps = (espSerialPhoneBuffer[1] - '0') * 100;
        iSteps += (espSerialPhoneBuffer[2] - '0') * 10;
        iSteps += (espSerialPhoneBuffer[3] - '0');

        GoProMove(iXaxis + iSteps, iYaxis, true);
      }
      else if (chCommand == 'R' && isDigit(espSerialPhoneBuffer[1]) && isDigit(espSerialPhoneBuffer[2]) && isDigit(espSerialPhoneBuffer[3]))
      {
        iSteps = (espSerialPhoneBuffer[1] - '0') * 100;
        iSteps += (espSerialPhoneBuffer[2] - '0') * 10;
        iSteps += (espSerialPhoneBuffer[3] - '0');

        GoProMove(iXaxis - iSteps, iYaxis, true);
      }
      else if (chCommand == 'W' && isDigit(espSerialPhoneBuffer[1]))
      {
        int iNextWellplate = (espSerialPhoneBuffer[1] - '0');

        GoProMove(WellplatesCoords[iNextWellplate - 1].x, WellplatesCoords[iNextWellplate - 1].y, true);
        iWellplate = iNextWellplate;
      }
      else if (chCommand == 'C' && espSerialPhoneBuffer[2] == '-' && isDigit(espSerialPhoneBuffer[1]) && isDigit(espSerialPhoneBuffer[3]))
      {
        int iNextWellplate = (espSerialPhoneBuffer[1] - '0');
        int iNextWellplateCell = (espSerialPhoneBuffer[3] - '0');

        if (iNextWellplate > 0 && iNextWellplate < 7 &&
            iNextWellplateCell > 0 && iNextWellplateCell < 7)
        {
          int iNextXaxis = (((iNextWellplate - 1) % 3) * 360) + (((iNextWellplateCell - 1) % 3) * 110);
          int iNextYaxis = ((iNextWellplate / 4) * 258) + ((iNextWellplateCell / 4) * 114);

          GoProMove(iNextXaxis, iNextYaxis, true);
          iWellplate = iNextWellplate;
          iWellplateCell = iNextWellplateCell;
        }
      }
      else if (chCommand == 'X' && espSerialPhoneBuffer[1] == '=')
      {
        int iRetCode = ProcessScript();
        if (iRetCode == 0)
        {
          SendString_ble_F(F("Script processed OK"));
        }
        else
        {
          SendString_ble_F(F("Script ERROR: "));
          switch (iRetCode)
          {
            case -1:
              SendString_ble_F(F("Invalid well plate # (1-6)"));
              break;
            case -2:
              SendString_ble_F(F("Invalid row # (1-2)"));
              break;
            case -3:
              SendString_ble_F(F("Invalid direction (F,R)"));
              break;
            case -4:
              SendString_ble_F(F("Script too long (<64)"));
              break;
          }
        }
        SendString_ble_F(F("\n"));
      }
      else
      {
        bUnrecognizedCommand = true;
      }

      if (bUnrecognizedCommand)
      {
#if DEBUG_OUTPUT
        Serial.print(F("Unrecognized command: "));
        Serial.println(espSerialPhoneBuffer);
#endif
        SendString_ble_F(F("Unrecognized command: "));
        SendString_ble(espSerialPhoneBuffer);
        SendString_ble_F(F("\n"));
        HelpDisplay();
      }
    }
  }

  if (bCommandReceived)
  {
    iShowCommand = true;
  }

  delay(250);

  if (bDoStartTime)
  {
    if (millis() > (lWaitTimeMS + CHECK_NTP))
    {      
      // Get NTP time and compare to Start Time
      if (GetNTP(sCurrentNTP, false))
      {
#if DEBUG_OUTPUT
        Serial.println(F("COMPARE to start time:"));
        Serial.println(sStartTime);
        Serial.println(&sCurrentNTP[11]);
#endif
        
        if (strncmp(sStartTime, &sCurrentNTP[11], 5) == 0)
        {
          bDoStartTime = false;
          if (bPhotoMode)
            SendString_ble_F(F("Start photos of cells\n"));
          else
            SendString_ble_F(F("Start recording cells\n"));
#if DEBUG_OUTPUT
          Serial.println(F("Start recording cells"));
#endif
          int iRetCode = ExecuteScript();
          return;
        }
      }

      lWaitTimeMS = millis();
      if (GetCommand())
      {

#if DO_LOCAL_BLE  
        if (espSerialPhoneBuffer[0] == '9' && espSerialPhoneBuffer[1] == '9')
        {
          SendString_ble_F(F("  Start time aborted\n"));
          bDoStartTime = false;
        }
        else
        {
          SendString_ble_F(F("  Commands ignored during start time wait\n"));
        }
#endif
        
      }
    
    }
  }

  //  Serial.println("LOOP");
}

#if DO_LOCAL_BLE  
boolean checkCRC(uint8_t sum, uint8_t CRCindex) {
  for (uint8_t i = 2; i < CRCindex; i++) sum -= (uint8_t)espSerialPhoneBuffer[i];
  return ((uint8_t)espSerialPhoneBuffer[CRCindex] == sum);
}
#endif

void SendString_ble(char *str)
{

#if DO_LOCAL_BLE    
  ble.print(F("AT+BLEUARTTX="));
  ble.println(str);
  // check response stastus
  if (! ble.waitForOK() ) {
    Serial.println(F("Failed to send?"));
  }
#endif

  espSerialPhone.print(str);
  
}

void SendString_ble_F(const __FlashStringHelper *str)
{

#if DO_LOCAL_BLE  
  ble.print(F("AT+BLEUARTTX="));
  ble.println(str);
  // check response stastus
  if (! ble.waitForOK() ) {
    Serial.println(F("Failed to send?"));
  }
#endif

  espSerialPhone.print(str);

//Serial.print("Send to phone: ");  
Serial.print(str);  
}

void GoProMove(int iNextXaxis, int iNextYaxis, int iWait)
{
  int iVal = 0;
  int iDirection;
//  int iStyle = MICROSTEP;
  int iStyle = INTERLEAVE;

  if (iWait != 0)
    SendString_ble_F(F("->Moving... please wait\n"));

  iVal = 0;
  if (iNextXaxis > iXaxis)
  {
    iVal = iNextXaxis - iXaxis;
    iDirection = FORWARD;
    //motor->step(iVal, FORWARD, SINGLE);
  }
  else if (iNextXaxis < iXaxis)
  {
    iVal = iXaxis - iNextXaxis;
    iDirection = BACKWARD;
    //motor->step(iVal, BACKWARD, SINGLE);
  }
#if 1 
  motor->step(iVal, iDirection, SINGLE);
  motor->release();
#else
  for (int i=0; i<iVal; i++)
  {
    motor->step(1, iDirection, iStyle); 
//    motor->release();
  } 
  motor->release();
#endif

  char sParam[5] = "X000";
  iVal = 0;
  if (iNextYaxis > iYaxis)
  {
    sParam [0] = 'F';
    iVal = iNextYaxis - iYaxis;
  }
  else if (iNextYaxis < iYaxis)
  {
    sParam [0] = 'B';
    iVal = iYaxis - iNextYaxis;
  }
  sParam[1] = (iVal / 100) + '0';
  sParam[2] = ((iVal / 10) % 10) + '0';
  sParam[3] = (iVal % 10) + '0';
  if (iVal != 0)
  {
    y_axisSerial.print(sParam);

//Serial.print("Wait for completion = ");
    //iState = STATE_WAITING_FOR_COMPLETION;

//    if (WaitFor_y_axisSerial() == false)
//      return;
//    char cYaxisByte = y_axisSerial.read();  
//Serial.println(cYaxisByte); 
//Serial.println("DELAY");

    unsigned long ulDelayMS = (((unsigned long)iVal) / 5L) * 1000L;
    delay(ulDelayMS);
  }

  iXaxis = iNextXaxis;
  iYaxis = iNextYaxis;
}

bool GoProConnect()
{
  int iTryCount = 0;
  
  char cEspSerialGoProByte = '\0';

  while (iTryCount++ < 3)
  {
    // Clear out any left over characters
    while (espSerialGoPro.available() != 0)
    {
      cEspSerialGoProByte = espSerialGoPro.read();
    }
    
    SendString_ble_F(F("->Connecting to GoPro... please wait\n"));
    espSerialGoPro.print("1");
    
    lStartTimeMS = millis();
    while (espSerialGoPro.available() == 0)
    {
      delay(100);
      if (millis() > (lStartTimeMS + GOPRO_CONNECT_TIMEOUT))
      {
        continue;
      }
    }
    cEspSerialGoProByte = espSerialGoPro.read();
    if (cEspSerialGoProByte != '1')
      continue;
  
    if (bPhotoMode)
    {
      espSerialGoPro.print("P");
    }
    else
    {
      espSerialGoPro.print("V");
    }
    if (WaitForEspSerialGoPro() == false)
      continue;
        
    cEspSerialGoProByte = espSerialGoPro.read();
    if (cEspSerialGoProByte != '1')
      continue;
      
    return (true);
  }

  return (false);
}

bool GoProDisconnect()
{  
  espSerialGoPro.print("0");
  
  if (WaitForEspSerialGoPro() == false)
    return (false);
  char cEspSerialGoProByte = espSerialGoPro.read();  
  if (cEspSerialGoProByte != '1')
    return (false);
    
  return (true);
}

int ProcessScript()
{

  strcpy(sExecuteScript, &espSerialPhoneBuffer[2]);
  strupr(sExecuteScript);
#if DEBUG_OUTPUT
  Serial.println(sExecuteScript);
#endif
  int iLen = strlen(sExecuteScript);

  if (iLen >= 64)
    return (-4);

  for (int i = 0; i < iLen; i = i + 4)
  {
    int iNextWellplate = (sExecuteScript[i] - '0');
    int iNextWellplateCell;

    if (sExecuteScript[i] < '1' || sExecuteScript[i] > '6')
      return (-1);
    if (sExecuteScript[i + 1] != '1' && sExecuteScript[i + 1] != '2')
      return (-2);
    if (sExecuteScript[i + 2] != 'F' && sExecuteScript[i + 2] != 'R')
      return (-3);
  }
  
  return (0);
}

int ExecuteScript()
{
  int iLen = strlen(sExecuteScript);

  char cEspSerialGoProByte = '1';

  if (iGoProEnabled)
  {
    if (GoProConnect() == false)    
    {
      SendString_ble_F(F("->GoPro connection **FAILED**\n  Manually reset GoPro!\n"));      
#if DEBUG_OUTPUT
      Serial.println(F(" FAILED"));
#endif
      return (-1);
    }
  }

  for (int i = 0; i < iLen; i = i + 4)
  {
    int iNextWellplate = (sExecuteScript[i] - '0');
    int iNextWellplateCell;

    if (sExecuteScript[i + 1] == '1')
    {
      if (sExecuteScript[i + 2] == 'F')
        iNextWellplateCell = 1;
      else
        iNextWellplateCell = 3;
    }
    else
    {
      if (sExecuteScript[i + 2] == 'F')
        iNextWellplateCell = 4;
      else
        iNextWellplateCell = 6;
    }

    for (int iCell = 0; iCell < 3; iCell++)
    {
      int iNextXaxis = (((iNextWellplate - 1) % 3) * 360) + (((iNextWellplateCell - 1) % 3) * 110);
      int iNextYaxis = ((iNextWellplate / 4) * 258) + ((iNextWellplateCell / 4) * 114);

      GoProMove(iNextXaxis, iNextYaxis, false);
      iWellplate = iNextWellplate;
      iWellplateCell = iNextWellplateCell;

#if DEBUG_OUTPUT
      Serial.print(iWellplate);
      Serial.print("-");
      Serial.println(iWellplateCell);
      Serial.println("RECORD Beg");
#endif
      char sNumb[2] = " ";
      if (bPhotoMode)
        SendString_ble_F(F(" Photo "));
      else
        SendString_ble_F(F(" Recording "));
      sNumb[0] = '0' + iWellplate;
      SendString_ble(sNumb);
      SendString_ble_F(F("-"));
      sNumb[0] = '0' + iWellplateCell;
      SendString_ble(sNumb);
      SendString_ble_F(F("\n"));

      if (iGoProEnabled)
        espSerialGoPro.print("A");

      if (bPhotoMode)
      {
#if DEBUG_OUTPUT
        Serial.println(F(" PHOTO"));
#endif
        delay(2500);
///////
        if (GetCommand())
        {

#if DO_LOCAL_BLE  
          if (espSerialPhoneBuffer[0] == '9' && espSerialPhoneBuffer[1] == '9')
          {
            SendString_ble_F(F("  Photos aborted\n"));
            GoProMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
            iWellplate = 1;
            iWellplateCell = 1;
            return (1);
          }
          else
          {
            SendString_ble_F(F("  Commands ignored during photos\n"));
          }
#endif
          
        }
///////
                
      }
      else
      {
#if DEBUG_OUTPUT
        Serial.println(F(" START VIDEO RECORDING"));
#endif
  
        lStartTimeMS = millis();
        while ((millis() - lStartTimeMS) < lCurrentTimeDelay)
        {
          delay(1000);
          if (GetCommand())
          {

#if DO_LOCAL_BLE              
            if (espSerialPhoneBuffer[0] == '9' && espSerialPhoneBuffer[1] == '9')
            {
              SendString_ble_F(F("  Recording aborted\n"));
              if (iGoProEnabled)
              {
                espSerialGoPro.print("S");
                espSerialGoPro.print("0");
              }
              GoProMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
              iWellplate = 1;
              iWellplateCell = 1;
              return (1);
            }
            else
            {
              SendString_ble_F(F("  Commands ignored during recording\n"));
            }
#endif
            
          }
        }
#if DEBUG_OUTPUT
        Serial.println(F("Record Video STOP"));
#endif
  
        if (iGoProEnabled)
        {
          espSerialGoPro.print("S");
  
          //Delay while GoPro writes out recording to SDCard
          delay(5000);
        }
#if DEBUG_OUTPUT
        Serial.println("RECORD End");
#endif
      }
      if (sExecuteScript[i + 2] == 'F')
        iNextWellplateCell++;
      else
        iNextWellplateCell--;
    }
  }

  if (iGoProEnabled)
  {
    GoProDisconnect();
  }

  GoProMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
  iWellplate = 1;
  iWellplateCell = 1;

  return (0);
}

bool GetCommand()
{
  if (espSerialPhone.available() > 0)
    return (true);
  return (false);
}

bool WaitForEspSerialGoPro()
{
  lStartTimeMS = millis();
  while (espSerialGoPro.available() == 0)
  {
    delay(100);
    if (millis() > (lStartTimeMS + GOPRO_CONNECT_TIMEOUT))
    {
      return (false);
    }
  }  
  return (true);
}

bool WaitFor_y_axisSerial()
{
  lStartTimeMS = millis();
  while (y_axisSerial.available() == 0)
  {
    delay(100);
    if (millis() > (lStartTimeMS + GOPRO_CONNECT_TIMEOUT))
    {
      return (false);
    }
  }  
  return (true);
}

bool GetNTP(char *buffer, bool bDisplay)
{
  while (espSerialGoPro.available() != 0)
  {
    char cEspSerialGoProByte = espSerialGoPro.read();
  }
  for (int i=0; i<10; i++)
  {
    char tmpbuffer[20];
    espSerialGoPro.print("2");
    while (espSerialGoPro.available() == 0)
    {
      ;
    }
    int iNmbBytes = espSerialGoPro.readBytes(tmpbuffer, sizeof(tmpbuffer));
    tmpbuffer[19] = '\0';
#if DEBUG_OUTPUT
    Serial.print("NTP string size = ");
    Serial.println(iNmbBytes);
#endif
    if (iNmbBytes != 20)
      continue;
#if DEBUG_OUTPUT
    Serial.print("NTP = ");
    Serial.println(tmpbuffer);
#endif

    if (bDisplay)
    {
      SendString_ble(tmpbuffer);
      SendString_ble_F(F("\n"));
    }
    if (tmpbuffer[ 4] == '-' &&
        tmpbuffer[ 7] == '-' &&
        tmpbuffer[13] == ':' &&
        tmpbuffer[16] == ':')
    {
        strcpy(sCurrentNTP, tmpbuffer);
        return (true);
    }
    delay(2000);
  }
  return (false);
}

void HelpDisplay()
{
  SendString_ble_F(F("\nCommands:\n"));
  SendString_ble_F(F("  00 Start recording cells\n"));
  SendString_ble_F(F("  01 Recording time 5 sec\n"));
  SendString_ble_F(F("  02 Recording time 3 min\n"));
  SendString_ble_F(F("  03 Recording time 5 min\n"));
  SendString_ble_F(F("  04 Disable GoPro, slider only\n"));
  SendString_ble_F(F("  05 Enable GoPro\n"));
  SendString_ble_F(F("  06 Video Mode GoPro\n"));
  SendString_ble_F(F("  07 Photo Mode GoPro\n"));
  SendString_ble_F(F("  08 Test GoPro Connect\Disconnect\n"));
  SendString_ble_F(F("  09 Get NTP current time\n"));
  SendString_ble_F(F("  10 Script display\n"));
  SendString_ble_F(F("  11 Set start time HH:MM\n"));
  SendString_ble_F(F("  12 Display start time\n"));
  SendString_ble_F(F("  13 Begin recording at start time\n"));
  SendString_ble_F(F("  14 Turn ON daylight time\n"));
  SendString_ble_F(F("  15 Turn OFF daylight time\n"));
  SendString_ble_F(F("  99 Abort recording cells\n"));
  SendString_ble_F(F("  X= Script ('wrd')\n"));
}
