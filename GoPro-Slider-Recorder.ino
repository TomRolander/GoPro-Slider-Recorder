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
#define VERSION "Ver 0.5 2021-07-12"

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
int iESP8266Byte = 0;
int iRecording = false;

int iPosition = 0;
int iCell = 0;

long lStartTimeMS = 0;

int iSteps = 0;
int iDirection = 0;

int iMaxCells = 9;  // 3 well plates at 3 cells per well plate
int iWellPlates = 3;

#define RECORDING_TIME_5_SEC 5000
#define RECORDING_TIME_3_MIN 180000
#define RECORDING_TIME_5_MIN 300000
#define RECORDING_TIME_5_SEC_STRING "5 Sec"
#define RECORDING_TIME_3_MIN_STRING "3 Min"
#define RECORDING_TIME_5_MIN_STRING "5 Min"
#define DEFAULT_RECORDING_TIME_MS RECORDING_TIME_5_SEC

long iTimeDelay = DEFAULT_RECORDING_TIME_MS;
long iCurrentTimeDelay = DEFAULT_RECORDING_TIME_MS;
char sRecordingTime[32] = RECORDING_TIME_5_SEC_STRING;

#define DEFAULT_CELL_STEP_SIZE 116
#define DEFAULT_GAP_STEP_SIZE (116+30)

#define STATE_READY             0
#define STATE_RECORDING_CELL_1  1
#define STATE_MOVING_TO_CELL_2  2
#define STATE_RECORDING_CELL_2  3
#define STATE_MOVING_TO_CELL_3  4
#define STATE_RECORDING_CELL_3  5
#define STATE_MOVING_TO_CELL_1  6

int iState = STATE_READY;

int iProcessing = false;

int iShowCommand = true;

int iGoProEnabled = true;

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
  
  SendString_ble("GoPro Slider Recorder\\nVer ");
  SendString_ble(VERSION);
  SendString_ble("\\n");
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
    char sCells[2] = " ";
    iShowCommand = false;
    SendString_ble("<"); 
    sCells[0] = '0' + (char)iMaxCells;   
    SendString_ble(sCells);    
    SendString_ble(" cells ");    
    SendString_ble(sRecordingTime);    
    SendString_ble(" GoPro "); 
    if (iGoProEnabled)   
      SendString_ble("On");  
    else  
      SendString_ble("Off");  
    SendString_ble("  COMMAND>");    
  }

  if (ble.isConnected())
  {
    ble.println(F("AT+BLEUARTRX")); // Request string from BLE module
    ble.readline(); // Read outcome

    if (strcmp(ble.buffer, "OK") != 0)
    {
      // Some data was found, its in the buffer
      Serial.print(F("[Recv] ")); Serial.println(ble.buffer);
      ble.waitForOK();
      bCommandReceived = true;

      if (isDigit(ble.buffer[0]) && isDigit(ble.buffer[1]))
      {
        iCommand = (int)(((char)ble.buffer[0]-'0')*10) + (int)(((char)ble.buffer[1]-'0'));
        switch(iCommand)
        {
          case 0: // Start Recording cells
            SendString_ble("Start recording cells\\n");
            Serial.println(F("Start recording cells"));
            break;
            
          case 1: 
            SendString_ble("1 Well Plate with 3 cells\\n");
            Serial.println(F("1 Well Plate with 3 cells"));
            iWellPlates = 1;
            iMaxCells = 3;
            break;
          case 2: 
            SendString_ble("2 Well Plates with 6 cells\\n");
            Serial.println(F("2 Well Plates with 6 cells"));
            iWellPlates = 2;
            iMaxCells = 6;
            break;
          case 3: 
            SendString_ble("3 Well Plates with 9 cells\\n");
            Serial.println(F("3 Well Plates with 9 cells"));
            iWellPlates = 3;
            iMaxCells = 9;
            break;
          case 4: 
            SendString_ble("Recording time 5 sec\\n");
            Serial.println(F("Recording time 5 sec"));
            iCurrentTimeDelay = RECORDING_TIME_5_SEC;
            strcpy(sRecordingTime, RECORDING_TIME_5_SEC_STRING);
            break;
          case 5: 
            SendString_ble("Recording time 3 min\\n");
            Serial.println(F("Recording time 3 min"));
            iCurrentTimeDelay = RECORDING_TIME_3_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_3_MIN_STRING);
            break;
          case 6: 
            SendString_ble("Recording time 5 min\\n");
            Serial.println(F("Recording time 5 min"));
            iCurrentTimeDelay = RECORDING_TIME_5_MIN;
            strcpy(sRecordingTime, RECORDING_TIME_5_MIN_STRING);
            break;
          case 7:
            SendString_ble("Disable GoPro, slider only\\n");
            Serial.println(F("Disable GoPro, slider only"));
            iGoProEnabled = false;
            break;
          case 8:
            SendString_ble("Enable GoPro\\n");
            Serial.println(F("Enable GoPro"));
            iGoProEnabled = true;
            break;
          case 9:
            SendString_ble("Forward cellplate row\\n");
            Serial.println(F("Forward cellplate row"));

            Serial.println("Sending 'F' command");
            y_axisSerial.print("F");

            delay(10000);

#if 0            
            Serial.println("Wait for completion.");
            while (y_axisSerial.available() == 0)
              ;
            char cForword = y_axisSerial.read();    
            Serial.print("RET = ");
            Serial.println(cForword);
#endif            
            break;
          case 10:
            SendString_ble("Backward cellplate row\\n");
            Serial.println(F("Backward cellplate row"));
            y_axisSerial.print('B');

            delay(10000);
#if 0            
            while (y_axisSerial.available() == 0)
              ;
            char cBackword = y_axisSerial.read();    
            Serial.print("RET = ");
            Serial.println(cBackword);
#endif            
            break;
            
          case 99:  // Abort Recording cells
            if (iProcessing)
            {
              SendString_ble("Abort recording cells\\n");
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
              SendString_ble("Not recording cells\\n");
              Serial.println(F("Not recording cells"));
            }
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
        SendString_ble("Sending GoPro Command: ");
        SendString_ble(ble.buffer);
        SendString_ble("\\n");
      }
      else
      {
        bUnrecognizedCommand = true;
      }

      if (bUnrecognizedCommand)
      {
        Serial.print(F("Unrecognized command: "));
        Serial.println(ble.buffer);
        SendString_ble("Unrecognized command: ");
        SendString_ble(ble.buffer);
        SendString_ble("\\n");        
        HelpDisplay();
      }
    }
  }

  if (bCommandReceived)
  {
    iShowCommand = true;
  }
  
  switch(iState)
  {
    case STATE_READY:    
    // Process any pending Bluetooth input
    if (bCommandReceived)
    {
      if (strcmp(ble.buffer, "00") == 0)
      {
        if (iRecording == false)
        {
            iProcessing = true;
            iState = STATE_RECORDING_CELL_1;
            iTimeDelay = iCurrentTimeDelay;
            iPosition = 0;
            iCell = 0;           
            //ble.end();
        }            
      }

    }
    break;

  case STATE_RECORDING_CELL_1:   
  case STATE_RECORDING_CELL_2:   
  case STATE_RECORDING_CELL_3:   
    if (iRecording == false)
    {
      Serial.println(F("Record Video"));
      if (iGoProEnabled)
      {
        // Clear out any left over characters
        while (espSerial.available() != 0)
        {
          iESP8266Byte = espSerial.read();
        }
        
        espSerial.print("1");
        int iCnt = 0;
        while (espSerial.available() == 0)
        {
          delay(100);
          //if (((iCnt++) % 100) == 0)
            //Serial.print("*");
        }
        iESP8266Byte = espSerial.read();
#if 0
        Serial.println("#########");
        Serial.print("#### ");
        Serial.print(iESP8266Byte,HEX);
        Serial.println("#### ");
        Serial.println("#########");
        iESP8266Byte = '1';
#endif
      }
      else
      {
        iESP8266Byte = '1';
      }
      if (iESP8266Byte == '1')
      {
        Serial.println(F(" START"));
        iRecording = true;
        digitalWrite(LED_BUILTIN, HIGH);
        lStartTimeMS = millis();
        iCell++;
      }
      else
      {
        SendString_ble("\\n*** Record Video FAILED ***\\n");
        Serial.println(F(" FAILED"));
      }      
    }
    else
    {
      if (millis() > (lStartTimeMS+iTimeDelay))
      {
        Serial.println(F("Record Video STOP"));
        if (iGoProEnabled)
        {
          espSerial.print("0");
        }
        iRecording = false;
        digitalWrite(LED_BUILTIN, LOW);
        if (iState == STATE_RECORDING_CELL_1)
        {
          iState = STATE_MOVING_TO_CELL_2;
          iSteps = DEFAULT_CELL_STEP_SIZE;
          iDirection = FORWARD; 
          iTimeDelay = 3000;           
          iPosition += iSteps;
        }
        else
        if (iState == STATE_RECORDING_CELL_2)
        {
          iState = STATE_MOVING_TO_CELL_3;
          iSteps = DEFAULT_CELL_STEP_SIZE;
          iDirection = FORWARD;            
          iTimeDelay = 3000;           
          iPosition += iSteps;
        }
        else
        if (iState == STATE_RECORDING_CELL_3)
        {
          iState = STATE_MOVING_TO_CELL_1;
          if (iCell < iMaxCells)
          {
            iSteps = DEFAULT_GAP_STEP_SIZE;
            iDirection = FORWARD;
            iPosition += iSteps;
          }
          else
          {
            iSteps = iPosition;
            iDirection = BACKWARD;
            iTimeDelay = 10000;           
          }                     
        }
        motor->step(iSteps, iDirection, SINGLE); 
        motor->release(); 
        lStartTimeMS = millis();
        Serial.print(F("Move Start Time MS = "));
        Serial.println(lStartTimeMS);
      }
    }
    
    break;

  case STATE_MOVING_TO_CELL_1:
  case STATE_MOVING_TO_CELL_2:
  case STATE_MOVING_TO_CELL_3:
    if (millis() > (lStartTimeMS+iTimeDelay))
    {
      if (iState == STATE_MOVING_TO_CELL_1)
      {
        if (iCell < iMaxCells)
          iState = STATE_RECORDING_CELL_1;
        else
          iState = STATE_READY;
        iTimeDelay = iCurrentTimeDelay;           
        //if (!ble.begin(false))
        //  for (;;); // BLE init error? LED on forever
        //ble.echo(false);
      }
      else
      if (iState == STATE_MOVING_TO_CELL_2)
      {
        iState = STATE_RECORDING_CELL_2;
        iTimeDelay = iCurrentTimeDelay;           
      }
      else
      if (iState == STATE_MOVING_TO_CELL_3)
      {
        iState = STATE_RECORDING_CELL_3;
        iTimeDelay = iCurrentTimeDelay;           
      }
      lStartTimeMS = millis();
//      Serial.print("Recording Start Time MS = ");
//      Serial.println(lStartTimeMS);
    }
    break;
    
  }
  delay(250);
  //Serial.println(iState);
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

void HelpDisplay()
{
  SendString_ble("\\nCommands:\\n");
  SendString_ble("  00 Start recording cells\\n");
  SendString_ble("  01 Well plate 3 cells\\n");
  SendString_ble("  02 Well plates 6 cells\\n");
  SendString_ble("  03 Well plates 9 cells\\n");
  SendString_ble("  04 Recording time 5 sec\\n");
  SendString_ble("  05 Recording time 3 min\\n");
  SendString_ble("  06 Recording time 5 min\\n");
  SendString_ble("  07 Disable GoPro, slider only\\n");
  SendString_ble("  08 Enable GoPro\\n");
  SendString_ble("  09 Forward cellplate row\\n");
  SendString_ble("  10 Backward cellplate row\\n");
  SendString_ble("  8x Send 'x' GoPro Command\\n");
  SendString_ble("  99 Abort recording cells\\n");    
}
