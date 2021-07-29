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

#define PROGRAM "GoPro Slider Recorder"
#define VERSION "Ver 0.7 2021-07-29"

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
#include <Adafruit_BluefruitLE_SPI.h>
#include <Adafruit_BluefruitLE_UART.h>

#include "BluefruitConfig.h"

SoftwareSerial y_axisSerial(3, 2);

SoftwareSerial espSerial(5, 4);

int iRecording = false;

long lStartTimeMS = 0;

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
    {  0,258},
    {360,258},
    {720,258}
  };

#define RECORDING_TIME_5_SEC (5000L + 1000L)
#define RECORDING_TIME_3_MIN (180000L + 1000L)
#define RECORDING_TIME_5_MIN (300000L + 1000L)
#define RECORDING_TIME_5_SEC_STRING "5 Sec"
#define RECORDING_TIME_3_MIN_STRING "3 Min"
#define RECORDING_TIME_5_MIN_STRING "5 Min"
#define DEFAULT_RECORDING_TIME_MS RECORDING_TIME_5_SEC

#define GOPRO_CONNECT_TIMEOUT 30000L

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


// Bluefruit config --------------------------------------------------------
Adafruit_BluefruitLE_SPI ble(8, 7, 6); // CS, IRQ, RST pins
//Adafruit_BluefruitLE_SPI ble(8, 7, -1); // CS, IRQ, RST pins

// Stepper motor config ----------------------------------------------------
#define STEPPER_STEPS 1100 // Length of slider
#define STEPPER_RPM 20
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
// Stepper motor w/200 steps/revolution (1.8 degree) on port 2 (M3 & M4)
Adafruit_StepperMotor *motor = AFMS.getStepper(200, 2);

// Setup function - runs once at startup -----------------------------------
void setup(void) {
  Serial.begin(9600);
  
  y_axisSerial.begin(1200);
  espSerial.begin(1200);

  delay(5000);
  Serial.println("");
  Serial.println(F(PROGRAM));
  Serial.println(F(VERSION));
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED steady on during init
  if (!ble.begin(false))
    for (;;); // BLE init error? LED on forever
  ble.echo(false);

  Serial.println(F("Please use Adafruit Bluefruit LE app to connect in UART mode"));
  Serial.println(F("Then Enter characters to send to Bluefruit"));
  Serial.println();

  ble.verbose(false);  // debug info is a little annoying after this point!

  /* Wait for connection */
  while (! ble.isConnected()) {
      delay(500);
  }

  delay(10000);
  
  SendString_ble_F(F("GoPro Slider Recorder\\nVer "));
  SendString_ble(VERSION);
  SendString_ble_F(F("\\n"));
  HelpDisplay();
    
//  AFMS.begin(4000);
  AFMS.begin();
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
    SendString_ble_F(F("<Rec ")); 
    SendString_ble(sRecordingTime);    
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
    {
      char chCommand = toupper(ble.buffer[0]);

      if (isDigit(ble.buffer[0]) && isDigit(ble.buffer[1]))
      {
        iCommand = (int)(((char)ble.buffer[0]-'0')*10) + (int)(((char)ble.buffer[1]-'0'));
        switch(iCommand)
        {
          case 0: // Start Recording cells
            if (strlen(sExecuteScript) == 0)
            {
              SendString_ble_F(F("Enter script 'X=wrd'\\n"));
            }
            else
            {
              SendString_ble_F(F("Start recording cells\\n"));
              Serial.println(F("Start recording cells"));
              int iRetCode = ExecuteScript();        
            }
            break;
            
          case 1: 
            SendString_ble_F(F("Recording time 5 sec\\n"));
            Serial.println(F("Recording time 5 sec"));
            lCurrentTimeDelay = RECORDING_TIME_5_SEC;
            strcpy(sRecordingTime, RECORDING_TIME_5_SEC_STRING);
            break;
            
          case 2: 
            SendString_ble_F(F("Recording time 3 min\\n"));
            Serial.println(F("Recording time 3 min"));
            lCurrentTimeDelay = RECORDING_TIME_3_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_3_MIN_STRING);
            break;
            
          case 3:           
            SendString_ble_F(F("Recording time 5 min\\n"));
            Serial.println(F("Recording time 5 min"));
            lCurrentTimeDelay = RECORDING_TIME_5_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_5_MIN_STRING);
            break;
            
          case 4:
            SendString_ble_F(F("Disable GoPro, slider only\\n"));
            Serial.println(F("Disable GoPro, slider only"));
            iGoProEnabled = false;
            break;
            
          case 5:
            SendString_ble_F(F("Enable GoPro\\n"));
            Serial.println(F("Enable GoPro"));
            iGoProEnabled = true;
            break;
            
          case 99:  // Abort Recording cells
#if 0          
            if (iProcessing)
            {
              SendString_ble_F(F("Abort recording cells\\n"));
              Serial.println(F("Abort recording cells"));
              iProcessing = false;
              if (iRecording)
              {
                Serial.println(F("Record Video STOP"));
                if (iGoProEnabled)
                {
                  espSerial.print("0");
                }
                iRecording = false;
                digitalWrite(LED_BUILTIN, LOW);
              }
              iSteps = iPosition;
              iDirection = BACKWARD;
              iTimeDelay = 10000;           
              motor->step(iSteps, iDirection, SINGLE); 
              motor->release(); 
              iState = STATE_READY;
            }
            else
            {
              SendString_ble_F(F("Not recording cells\\n"));
              Serial.println(F("Not recording cells"));
            }
#endif            
            break;
            
          default:     
            bUnrecognizedCommand = true; 
            break;
        }
        
      }
      else
      if (ble.buffer[0] == '8')
      {
        ble.buffer[0] = ble.buffer[1];
        ble.buffer[1] = 0;
        espSerial.print(ble.buffer);
        Serial.print("Sending GoPro Command: ");
        Serial.println(ble.buffer);
        SendString_ble_F(F("Sending GoPro Command: "));
        SendString_ble(ble.buffer);
        SendString_ble_F(F("\\n"));
      }
      else
      if (chCommand == 'F' && isDigit(ble.buffer[1]) && isDigit(ble.buffer[2]) && isDigit(ble.buffer[3]))
      {
        iSteps = (ble.buffer[1]-'0') * 100;
        iSteps += (ble.buffer[2]-'0') * 10;
        iSteps += (ble.buffer[3]-'0');

        MoveGoPro(iXaxis, iYaxis + iSteps, true);
      }
      else
      if (chCommand == 'B' && isDigit(ble.buffer[1]) && isDigit(ble.buffer[2]) && isDigit(ble.buffer[3]))
      {
        iSteps = (ble.buffer[1]-'0') * 100;
        iSteps += (ble.buffer[2]-'0') * 10;
        iSteps += (ble.buffer[3]-'0'); 

        MoveGoPro(iXaxis, iYaxis - iSteps, true);
      }
      else
      if (chCommand == 'L' && isDigit(ble.buffer[1]) && isDigit(ble.buffer[2]) && isDigit(ble.buffer[3]))
      {
        iSteps = (ble.buffer[1]-'0') * 100;
        iSteps += (ble.buffer[2]-'0') * 10;
        iSteps += (ble.buffer[3]-'0'); 
        
        MoveGoPro(iXaxis + iSteps, iYaxis, true);
      }
      else
      if (chCommand == 'R' && isDigit(ble.buffer[1]) && isDigit(ble.buffer[2]) && isDigit(ble.buffer[3]))
      {
        iSteps = (ble.buffer[1]-'0') * 100;
        iSteps += (ble.buffer[2]-'0') * 10;
        iSteps += (ble.buffer[3]-'0'); 
        
        MoveGoPro(iXaxis - iSteps, iYaxis, true);
      }
      else
      if (chCommand == 'W' && isDigit(ble.buffer[1]))
      {
        int iNextWellplate = (ble.buffer[1]-'0'); 
        
        MoveGoPro(WellplatesCoords[iNextWellplate-1].x,WellplatesCoords[iNextWellplate-1].y, true);
        iWellplate = iNextWellplate;
      }
      else
      if (chCommand == 'C' && ble.buffer[2] == '-' && isDigit(ble.buffer[1]) && isDigit(ble.buffer[3]))
      {
        int iNextWellplate = (ble.buffer[1]-'0'); 
        int iNextWellplateCell = (ble.buffer[3]-'0'); 

        if (iNextWellplate > 0 && iNextWellplate < 7 &&
            iNextWellplateCell > 0 && iNextWellplateCell < 7)
        {       
          int iNextXaxis = (((iNextWellplate-1) % 3) * 360) + (((iNextWellplateCell-1) % 3) * 110);
          int iNextYaxis = ((iNextWellplate/4) * 258) + ((iNextWellplateCell/4) * 114);

          MoveGoPro(iNextXaxis, iNextYaxis, true);
          iWellplate = iNextWellplate;
          iWellplateCell = iNextWellplateCell;
        }
      }
      else
      if (chCommand == 'X' && ble.buffer[1] == '=')
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
        SendString_ble_F(F("\\n"));        
      }
      else
      {
        bUnrecognizedCommand = true;
      }

      if (bUnrecognizedCommand)
      {
        Serial.print(F("Unrecognized command: "));
        Serial.println(ble.buffer);
        SendString_ble_F(F("Unrecognized command: "));
        SendString_ble(ble.buffer);
        SendString_ble_F(F("\\n"));        
        HelpDisplay();
      }
    }
  }

  if (bCommandReceived)
  {
    iShowCommand = true;
  }

  delay(250);
  
//  Serial.println("LOOP");
}

boolean checkCRC(uint8_t sum, uint8_t CRCindex) {
  for (uint8_t i = 2; i < CRCindex; i++) sum -= (uint8_t)ble.buffer[i];
  return ((uint8_t)ble.buffer[CRCindex] == sum);
}

void SendString_ble(char *str)
{
  ble.print(F("AT+BLEUARTTX="));
  ble.println(str);
  // check response stastus
  if (! ble.waitForOK() ) {
    Serial.println(F("Failed to send?"));
  }
}

void SendString_ble_F(const __FlashStringHelper *str)
{
  ble.print(F("AT+BLEUARTTX="));
  ble.println(str);
  // check response stastus
  if (! ble.waitForOK() ) {
    Serial.println(F("Failed to send?"));
  }
}

void HelpDisplay()
{
  SendString_ble_F(F("\\nCommands:\\n"));
  SendString_ble_F(F("  00 Start recording cells\\n"));
  SendString_ble_F(F("  01 Recording time 5 sec\\n"));
  SendString_ble_F(F("  02 Recording time 3 min\\n"));
  SendString_ble_F(F("  03 Recording time 5 min\\n"));
  SendString_ble_F(F("  04 Disable GoPro, slider only\\n"));
  SendString_ble_F(F("  05 Enable GoPro\\n"));
  SendString_ble_F(F("  99 Abort recording cells\\n"));    
  SendString_ble_F(F("  X= Script ('wrd')\\n"));    
}

void MoveGoPro(int iNextXaxis, int iNextYaxis, int iWait)
{
  int iVal = 0;

  if (iWait != 0)
    SendString_ble_F(F("->Moving... please wait\\n"));    

  iVal = 0;
  if (iNextXaxis > iXaxis)
  {
    iVal = iNextXaxis-iXaxis;
    motor->step(iVal, FORWARD, SINGLE); 
  }
  else
  if (iNextXaxis < iXaxis)
  {
    iVal = iXaxis-iNextXaxis;
    motor->step(iVal, BACKWARD, SINGLE); 
  }
  motor->release();
  
  char sParam[5] = "X000";
  iVal = 0;
  if (iNextYaxis > iYaxis)
  {
    sParam [0] = 'F';
    iVal = iNextYaxis-iYaxis;
  }
  else
  if (iNextYaxis < iYaxis)
  {
    sParam [0] = 'B';
    iVal = iYaxis-iNextYaxis;
  }
  sParam[1] = (iVal/100) + '0';
  sParam[2] = ((iVal/10) % 10) + '0';
  sParam[3] = (iVal % 10) + '0';
  if (iVal != 0)
  {
    y_axisSerial.print(sParam);

    //Serial.println("Wait for completion.");
    //iState = STATE_WAITING_FOR_COMPLETION;

    unsigned long ulDelayMS = (((unsigned long)iVal)/5L) * 1000L;
    delay(ulDelayMS);
  }          

  iXaxis = iNextXaxis;
  iYaxis = iNextYaxis;
}

int ProcessScript()
{
  strcpy(sExecuteScript, &ble.buffer[2]);
  strupr(sExecuteScript);
  Serial.println(sExecuteScript);
  int iLen = strlen(sExecuteScript);

  if (iLen >= 64)
    return (-4);

  for (int i=0; i<iLen; i=i+4)
  {
    int iNextWellplate = (sExecuteScript[i]-'0'); 
    int iNextWellplateCell; 

    if (sExecuteScript[i] < '1' || sExecuteScript[i] > '6')
      return (-1);
    if (sExecuteScript[i+1] != '1' && sExecuteScript[i+1] != '2')
      return (-2);
    if (sExecuteScript[i+2] != 'F' && sExecuteScript[i+2] != 'R')
      return (-3);
  }  
  return (0);
}

int ExecuteScript()
{
  int iLen = strlen(sExecuteScript);

  char cESP8266Byte = '1';
  
  if (iGoProEnabled)
  {
    // Clear out any left over characters
    while (espSerial.available() != 0)
    {
      cESP8266Byte = espSerial.read();
    }
    
    SendString_ble_F(F("->Connecting to GoPro... please wait\\n"));    
    espSerial.print("1");
    
    lStartTimeMS = millis();
    while (espSerial.available() == 0)
    {
      delay(100);
      if (millis() > (lStartTimeMS + GOPRO_CONNECT_TIMEOUT))
      {
        SendString_ble_F(F("->GoPro connection **FAILED**\\n  Manually reset GoPro!\\n"));    
        return (-1);
      }
    }
    cESP8266Byte = espSerial.read();
  }
  else
  {
    cESP8266Byte = '1';
  }

  if (cESP8266Byte != '1')
  {
    SendString_ble_F(F("\\n*** Record Video FAILED ***\\n"));
    Serial.println(F(" FAILED"));
    return (-1);
  }

  
  for (int i=0; i<iLen; i=i+4)
  {
    int iNextWellplate = (sExecuteScript[i]-'0'); 
    int iNextWellplateCell; 

    if (sExecuteScript[i+1] == '1')
    {
      if (sExecuteScript[i+2] == 'F')
        iNextWellplateCell = 1;
      else
        iNextWellplateCell = 3;
    }
    else
    {
      if (sExecuteScript[i+2] == 'F')
        iNextWellplateCell = 4;
      else
        iNextWellplateCell = 6;
    }
    
    for (int iCell=0; iCell<3; iCell++)
    {
      int iNextXaxis = (((iNextWellplate-1) % 3) * 360) + (((iNextWellplateCell-1) % 3) * 110);
      int iNextYaxis = ((iNextWellplate/4) * 258) + ((iNextWellplateCell/4) * 114);

      MoveGoPro(iNextXaxis, iNextYaxis, false);
      iWellplate = iNextWellplate;
      iWellplateCell = iNextWellplateCell;

Serial.print(iWellplate);
Serial.print("-");
Serial.println(iWellplateCell);
Serial.println("RECORD Beg");

      {
        char sNumb[2] = " ";
        SendString_ble_F(F(" Recording "));
        sNumb[0] = '0' + iWellplate;
        SendString_ble(sNumb);
        SendString_ble_F(F("-"));
        sNumb[0] = '0' + iWellplateCell;
        SendString_ble(sNumb);
        SendString_ble_F(F("\\n"));
        
        if (iGoProEnabled)
          espSerial.print("A");
          
        Serial.println(F(" START"));
        iRecording = true;
        digitalWrite(LED_BUILTIN, HIGH);

        lStartTimeMS = millis();
        while ((millis() - lStartTimeMS) < lCurrentTimeDelay)
        {
          delay(1000);
          if (GetCommand())
          {
            if (ble.buffer[0] == '9' && ble.buffer[1] == '9')
            {
              SendString_ble_F(F("  Recording aborted\\n"));
              if (iGoProEnabled)
              {
                espSerial.print("S");
                espSerial.print("0");
              }
              MoveGoPro(WellplatesCoords[0].x,WellplatesCoords[0].y, true);
              iWellplate = 1;
              iWellplateCell = 1;
              return (1);
            }
            else
            {
              SendString_ble_F(F("  Commands ignored during recording\\n"));
            }
          }
        }
//      delay(lCurrentTimeDelay);
        
        Serial.println(F("Record Video STOP"));
        
        if (iGoProEnabled)
          espSerial.print("S");
      }
      
Serial.println("RECORD End");

      if (sExecuteScript[i+2] == 'F')
        iNextWellplateCell++;
      else            
        iNextWellplateCell--;
    }
  }

  if (iGoProEnabled)
  {
    espSerial.print("0");
  }

  MoveGoPro(WellplatesCoords[0].x,WellplatesCoords[0].y, true);
  iWellplate = 1;
  iWellplateCell = 1;
  
  return (0);  
}

bool GetCommand()
{
  if (ble.isConnected())
  {
    ble.println(F("AT+BLEUARTRX")); // Request string from BLE module
    ble.readline(); // Read outcome

    if (strcmp(ble.buffer, "OK") != 0)
    {
      // Some data was found, its in the buffer
      Serial.print(F("[Recv] ")); 
      Serial.println(ble.buffer);
      ble.waitForOK();
      return (true);
    }
  }
  return (false);
}
