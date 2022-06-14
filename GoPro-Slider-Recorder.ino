/**************************************************************************
  SmartPhone Slider Recorder

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

 **************************************************************************/

#define PROGRAM "SmartPhone Slider Recorder"
#define VERSION "Ver 0.9 2022-04-03"

#define DEBUG_OUTPUT 1

// Smartphone- or tablet-activated timelapse camera slider.
// Uses the following Adafruit parts:
//
// Arduino Uno R3 or similar (adafruit.com/product/50 or #2488)
// Motor/Stepper/Servo Shield v2 (#1438)
// NEMA-17 Stepper motor (#324)
// Miscellaneous hardware and 3D-printed parts; see guide for full list.
//
// Needs Adafruit_MotorShield libs:
// github.com/adafruit
// Use Adafruit Bluefruit LE app for iOS or Android to control dual-axis slider.

#include <SPI.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_MotorShield.h>

//SoftwareSerial y_axisSerial(3, 2);

// Veronica's
//SoftwareSerial y_axisSerial(5, 4);

// Kathi's
SoftwareSerial y_axisSerial(4, 5);

SoftwareSerial esp32Serial(6, 7);

static char esp32SerialBuffer[128] = "";


static bool bPhotoMode = false;

static long lStartTimeMS = 0;
static long lWaitTimeMS = 0;

static bool  bDoStartTime = false;
static char  sStartTime[6] = "";

static bool bWaitingForSmartPhone = false;

static char sCurrentNTP[20] = "";

static int iSteps = 0;
static int index = 0;

static int iXaxis = 0;
static int iYaxis = 0;
static int iWellplate = 1;
static int iWellplateCell = 1;

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

struct Point
{
  int x;
  int y;
  int iPoint;
};

static struct Point Points[10] =
{
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0},
  {  0,  0, 0}  
};

//#define RECORDING_TIME_5_SEC (5000L + 1000L)
#define RECORDING_TIME_5_SEC (5000L - 250L)
#define RECORDING_TIME_3_MIN (180000L + 1000L)
#define RECORDING_TIME_5_MIN (300000L + 1000L)
#define RECORDING_TIME_5_SEC_STRING "5 Sec"
#define RECORDING_TIME_3_MIN_STRING "3 Min"
#define RECORDING_TIME_5_MIN_STRING "5 Min"
#define DEFAULT_RECORDING_TIME_MS RECORDING_TIME_5_SEC

#define GOPRO_CONNECT_TIMEOUT 30000L
#define Y_AXIS_TIMEOUT        75000L

#define CHECK_NTP 30000L

long iTimeDelay = DEFAULT_RECORDING_TIME_MS;
long lCurrentTimeDelay = DEFAULT_RECORDING_TIME_MS;
char sRecordingTime[32] = RECORDING_TIME_5_SEC_STRING;

//#define DEFAULT_CELL_STEP_SIZE 116
//#define DEFAULT_GAP_STEP_SIZE (116+30)

#define DEFAULT_CELL_STEP_SIZE 110
#define DEFAULT_GAP_STEP_SIZE (110+30)

static int iShowCommand = true;

static int iSmartPhoneEnabled = true;
static bool bSmartPhoneDisabled = false;


static char sExecuteWellplateScript[128] = "";
static char sExecuteRecordingScript[128] = "";

// Stepper motor config ----------------------------------------------------
#define STEPPER_STEPS 1100 // Length of slider
#define STEPPER_RPM 20
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
// Stepper motor w/200 steps/revolution (1.8 degree) on port 2 (M3 & M4)
Adafruit_StepperMotor *motor = AFMS.getStepper(200, 2);

// Setup function - runs once at startup -----------------------------------
void setup(void) {
  Serial.begin(115200);
  delay(1000);

  y_axisSerial.begin(1200);
  esp32Serial.begin(1200);

  esp32Serial.listen();

  // Flush serial buffer
  while (esp32Serial.available() > 0)
  {
    esp32Serial.read();
  }


  Serial.println("");
  Serial.println(F(PROGRAM));
  Serial.println(F(VERSION));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED steady on during init

  Serial.println(F("Please use Adafruit Bluefruit LE app to connect in UART mode"));
  Serial.println(F("Then Enter characters to send to Bluefruit"));
  Serial.println();

  delay(1000);

  while (esp32Serial.available() == 0)
  {
    ;
  }
  char cChar = esp32Serial.read();

#if 0
  if (cChar != 'Y')
  {
    iSmartPhoneEnabled = true;
    bSmartPhoneDisabled = false;
  }
#endif
  SendString_ble_F(F("\n\n"));
  SendString_ble_F(F(PROGRAM));
  SendString_ble_F(F("\n"));
  SendString_ble_F(F(VERSION));
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
    SendString_ble_F(F(" SmartPhone "));
    if (iSmartPhoneEnabled)
      SendString_ble_F(F("On"));
    else
      SendString_ble_F(F("Off"));
    SendString_ble_F(F("  COMMAND>"));
  }

  bCommandReceived = GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer));
  if (bCommandReceived)
  {
    char chCommand = toupper(esp32SerialBuffer[0]);

    if (isDigit(esp32SerialBuffer[0]) && isDigit(esp32SerialBuffer[1]))
    {
      iCommand = (int)(((char)esp32SerialBuffer[0] - '0') * 10) + (int)(((char)esp32SerialBuffer[1] - '0'));
      switch (iCommand)
      {
        case 0: // Start Recording cells
          if (strlen(sExecuteWellplateScript) == 0 && strlen(sExecuteRecordingScript) == 0)
          {
            SendString_ble_F(F("Enter script 'X=wrd,wrd,... or R=xx,xx,...'\n"));
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
            int iRetCode;
            if (strlen(sExecuteWellplateScript) != 0)
              iRetCode = ExecuteWellplateScript();
            else
              iRetCode = ExecuteRecordingScript();
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
          SendString_ble_F(F("Disable SmartPhone, slider only\n"));
#if DEBUG_OUTPUT
          Serial.println(F("Disable SmartPhone, slider only"));
#endif
          iSmartPhoneEnabled = false;
          break;

        case 5:
          if (bSmartPhoneDisabled)
          {
            SendString_ble_F(F("SmartPhone disabled at startup!\n"));
            break;
          }
          SendString_ble_F(F("Enable SmartPhone\n"));
#if DEBUG_OUTPUT
          Serial.println(F("Enable SmartPhone"));
#endif
          iSmartPhoneEnabled = true;
          break;

        // Video mode
        case 6:
          bPhotoMode = false;
          SendSmartPhoneCommand('V');
          break;

        // Photo mode
        case 7:
          bPhotoMode = true;
          SendSmartPhoneCommand('P');
          break;

        case 8:
          if (SmartPhoneConnect() == true)
          {
            SmartPhoneDisconnect();
            SendString_ble_F(F("Connection successful\n"));
          }
          else
          {
            SendString_ble_F(F("->SmartPhone connection **FAILED**\n  Manually reset SmartPhone!\n"));
          }
          break;

        case 9:
          {
            if (GetNTP(sCurrentNTP, true) == false)
              SendString_ble_F(F("Failed to get NTP\n"));
            break;
          }

        case 10:
          if (strlen(sExecuteWellplateScript) != 0)
          {
            SendString_ble_F(F("X="));
            SendString_ble(sExecuteWellplateScript);
            SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
            Serial.println(sExecuteWellplateScript);
#endif
          }
          else if (strlen(sExecuteRecordingScript) != 0)
          {
            SendString_ble_F(F("R="));
            SendString_ble(sExecuteRecordingScript);
            SendString_ble_F(F("\n"));
#if DEBUG_OUTPUT
            Serial.println(sExecuteRecordingScript);
#endif
          }
          else
          {
            SendString_ble_F(F("No script entered\n"));            
          }
          break;

        case 11:
          {
            bool bStartTimeOK = false;
            if (isDigit(esp32SerialBuffer[3]) &&
                isDigit(esp32SerialBuffer[4]) &&
                isDigit(esp32SerialBuffer[6]) &&
                isDigit(esp32SerialBuffer[7]) &&
                esp32SerialBuffer[5] == ':')
            {
              strncpy(sStartTime, &esp32SerialBuffer[3], 5);
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
          if (strlen(sExecuteWellplateScript) == 0)
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
          SendSmartPhoneCommand('3');
#if DEBUG_OUTPUT
          Serial.print(F("Turn ON daylight savings"));
          Serial.println(sStartTime);
#endif
          break;

        case 15:
          SendString_ble_F(F("Turn OFF daylight savings\n"));
          SendSmartPhoneCommand('4');
#if DEBUG_OUTPUT
          Serial.print(F("Turn OFF daylight savings"));
          Serial.println(sStartTime);
#endif
          break;

        case 16:
#if 0
          Serial.print(F("Joystick"));
#endif
          SendString_ble_F(F("Joystick: Push button to Finish\n"));
          DoJoystick();
          //GetJoystick();
          break;

        case 17:
#if 0
          Serial.print(F("Position:"));
#endif
          SendString_ble_F(F("Position: "));
          SendString_ble_F(F("X="));
          SendInt_ble(iXaxis);
          SendString_ble_F(F(" Y="));
          SendInt_ble(iYaxis);
          SendString_ble_F(F("\n"));
          break;

        case 88:  // Show addtional commands
#if 0
          Serial.print(F("Additional commands:\n"));
#endif
          AdditionalCommandsDisplay();
          break;

        case 99:  // Abort Recording cells
#if DEBUG_OUTPUT
          Serial.print(F("*** ABORT ***\n"));
#endif
          // handled separately below
          break;

        default:
          bUnrecognizedCommand = true;
          break;
      }

    }
    else if (chCommand == 'F' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iXaxis, iYaxis + iSteps, true);
    }
    else if (chCommand == 'B' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iXaxis, iYaxis - iSteps, true);
    }
    else if (chCommand == 'L' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iXaxis + iSteps, iYaxis, true);
    }
    else if (chCommand == 'R' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iXaxis - iSteps, iYaxis, true);
    }
    else if (chCommand == 'W' && isDigit(esp32SerialBuffer[1]))
    {
      int iNextWellplate = (esp32SerialBuffer[1] - '0');

      SmartPhoneMove(WellplatesCoords[iNextWellplate - 1].x, WellplatesCoords[iNextWellplate - 1].y, true);
      iWellplate = iNextWellplate;
    }
    else if (chCommand == 'C' && esp32SerialBuffer[2] == '-' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[3]))
    {
      int iNextWellplate = (esp32SerialBuffer[1] - '0');
      int iNextWellplateCell = (esp32SerialBuffer[3] - '0');

      if (iNextWellplate > 0 && iNextWellplate < 7 &&
          iNextWellplateCell > 0 && iNextWellplateCell < 7)
      {
        int iNextXaxis = (((iNextWellplate - 1) % 3) * 360) + (((iNextWellplateCell - 1) % 3) * 110);
        int iNextYaxis = ((iNextWellplate / 4) * 258) + ((iNextWellplateCell / 4) * 114);

        SmartPhoneMove(iNextXaxis, iNextYaxis, true);
        iWellplate = iNextWellplate;
        iWellplateCell = iNextWellplateCell;
      }
    }
    else if (chCommand == 'X' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iSteps, iYaxis, true);
    }
    else if (chCommand == 'Y' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      SmartPhoneMove(iXaxis, iSteps, true);
    }
    else if (chCommand == 'X' && esp32SerialBuffer[1] == '=')
    {
      int iRetCode = ProcessWellplateScript();
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
    else if (chCommand == 'R' && esp32SerialBuffer[1] == '=')
    {
      int iRetCode = ProcessRecordingScript();
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
            SendString_ble_F(F("Invalid point"));
            break;
          case -4:
            SendString_ble_F(F("Script too long (<64)"));
            break;
        }
      }
      SendString_ble_F(F("\n"));
    }
    else if (chCommand == 'P' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]))
    {
      int i;
      index = (esp32SerialBuffer[1] - '0') * 10;
      index += (esp32SerialBuffer[2] - '0');
      for (i=0; i<10; i++)
      {
        if (Points[i].iPoint == 0)
        {
          Points[i].x = iXaxis;
          Points[i].y = iYaxis;
          Points[i].iPoint = index;
          break;
        }
      }
      if (i >= 10)
        SendString_ble_F(F("Maximum of 10 points!\n"));
    }    
    else if (chCommand == 'G' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]))
    {
      int i;
      index = (esp32SerialBuffer[1] - '0') * 10;
      index += (esp32SerialBuffer[2] - '0');
      for (i=0; i<10; i++)
      {
        if (Points[i].iPoint == index)
        {
          SmartPhoneMove(Points[i].x, Points[i].y, true);
          break;
        }
      }
      if (i >= 10)
        SendString_ble_F(F("Point not defined!\n"));
    }    
    else if (chCommand == 'S' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]))
    {
      int i;
      index = (esp32SerialBuffer[1] - '0') * 10;
      index += (esp32SerialBuffer[2] - '0');
      for (i=0; i<10; i++)
      {
        if (Points[i].iPoint == index)
        {
          char sNumb[4] = "   ";
          sNumb[0] = '0' + (Points[i].x/100);
          sNumb[1] = '0' + ((Points[i].x/10)%10);
          sNumb[2] = '0' + (Points[i].x%10);
          SendString_ble_F(F("X="));
          SendString_ble(sNumb);
          SendString_ble_F(F(" Y="));
          sNumb[0] = '0' + (Points[i].y/100);
          sNumb[1] = '0' + ((Points[i].y/10)%10);
          sNumb[2] = '0' + (Points[i].y%10);
          SendString_ble(sNumb);
          SendString_ble_F(F("\n"));
          break;
        }
      }
      if (i >= 10)
        SendString_ble_F(F("Point not defined!\n"));
    }    
    else if (chCommand == 'D' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]))
    {
      int i;
      index = (esp32SerialBuffer[1] - '0') * 10;
      index += (esp32SerialBuffer[2] - '0');
      for (i=0; i<10; i++)
      {
        if (Points[i].iPoint == index)
        {
          Points[i].iPoint = 0;
          break;
        }
      }
      if (i >= 10)
        SendString_ble_F(F("Point not defined!\n"));
    }    
    else
    {
      bUnrecognizedCommand = true;
    }

    if (bUnrecognizedCommand)
    {
#if DEBUG_OUTPUT
      Serial.print(F("Unrecognized command: "));
      Serial.println(esp32SerialBuffer);
#endif
      SendString_ble_F(F("Unrecognized command: "));
      SendString_ble(esp32SerialBuffer);
      SendString_ble_F(F("\n"));
      HelpDisplay();
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
          int iRetCode = ExecuteWellplateScript();
          return;
        }
      }

      lWaitTimeMS = millis();
      if (GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer)))
      {
        if (esp32SerialBuffer[0] == '9' && esp32SerialBuffer[1] == '9')
        {
          esp32Serial.print(F("  Start time aborted\n"));
          bDoStartTime = false;
        }
        else
        {
          esp32Serial.print(F("  Commands ignored during start time wait\n"));
        }
      }

    }
  }
}

void SendString_ble(char *str)
{
  esp32Serial.print(str);
}

void SendString_ble_F(const __FlashStringHelper *str)
{
  esp32Serial.print(str);

#if DEBUG_OUTPUT
  //Serial.print("Send to phone: ");
  Serial.print(str);
#endif
}

void SmartPhoneMove(int iNextXaxis, int iNextYaxis, int iWait)
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
  // do single steps in the forward or backward directions
  
////  motor->step(iVal, iDirection, SINGLE);
  motor->step(iVal, iDirection, MICROSTEP);
  motor->release();
#else
  for (int i = 0; i < iVal; i++)
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

    // enable listening for y-axis controller
    // disables listening for esp32
    y_axisSerial.listen();

#if 0
    Serial.print("Wait for completion = ");
#endif

    if (WaitFor_y_axisSerial() == false)
    {
      // disables listening for y-axis controller
      // edables listening for esp32
      esp32Serial.listen();
      return;
    }

    char cYaxisByte = y_axisSerial.read();
#if 0
    Serial.println(cYaxisByte);
    Serial.println("DELAY");
#endif

    // disables listening for y-axis controller
    // edables listening for esp32
    esp32Serial.listen();
  }

  iXaxis = iNextXaxis;
  iYaxis = iNextYaxis;
}

bool SendSmartPhoneCommand(char cCommand)
{
  bool bRetCode = true;
  bWaitingForSmartPhone = true;
  esp32Serial.print('\x1B');
  delay(500);
  esp32Serial.print(cCommand);

  if (WaitForesp32Serial() == false)
    bRetCode = false;
  else
  {
    char cesp32SerialByte = esp32Serial.read();
    if (cesp32SerialByte != '1')
      bRetCode = false;
  }

#if DEBUG_OUTPUT
  Serial.print("GOPRO Escape [");
  Serial.print(cCommand);
  Serial.print("] ");
  if (bRetCode)
    Serial.println("Success");
  else
    Serial.println("Failure");
#endif

  bWaitingForSmartPhone = false;
  return (bRetCode);
}

bool SmartPhoneConnect()
{
  bool bRetCode;
#if DEBUG_OUTPUT
  Serial.println("CONNECT");
#endif

  if (bPhotoMode)
    bRetCode = SendSmartPhoneCommand('P');
  else
    bRetCode = SendSmartPhoneCommand('V');

  if (bRetCode)
    bRetCode = SendSmartPhoneCommand('1');
  return (bRetCode);
}

bool SmartPhoneDisconnect()
{
  bool bRetCode;
#if DEBUG_OUTPUT
  Serial.println("DISCONNECT");
#endif

  bRetCode = SendSmartPhoneCommand('0');
  return (bRetCode);
}

int ProcessWellplateScript()
{

  strcpy(sExecuteWellplateScript, &esp32SerialBuffer[2]);
  strupr(sExecuteWellplateScript);
#if DEBUG_OUTPUT
  Serial.println(sExecuteWellplateScript);
#endif
  int iLen = strlen(sExecuteWellplateScript);

  if (iLen >= 64)
    return (-4);

  for (int i = 0; i < iLen; i = i + 4)
  {
    int iNextWellplate = (sExecuteWellplateScript[i] - '0');
    int iNextWellplateCell;

    if (sExecuteWellplateScript[i] < '1' || sExecuteWellplateScript[i] > '6')
      return (-1);
    if (sExecuteWellplateScript[i + 1] != '1' && sExecuteWellplateScript[i + 1] != '2')
      return (-2);
    if (sExecuteWellplateScript[i + 2] != 'F' && sExecuteWellplateScript[i + 2] != 'R')
      return (-3);
  }

  return (0);
}

int ProcessRecordingScript()
{


      int j;
      for (j=0; j<10; j=j+1)
      {
        if (Points[j].iPoint != 0)
        {
          Serial.print(Points[j].iPoint);
          Serial.print(" ");
          Serial.print(Points[j].x);
          Serial.print(" ");
          Serial.println(Points[j].y);
        }
      }
  

  strcpy(sExecuteRecordingScript, &esp32SerialBuffer[2]);
  strupr(sExecuteRecordingScript);
#if DEBUG_OUTPUT
  Serial.println(sExecuteRecordingScript);
#endif
  int iLen = strlen(sExecuteRecordingScript);

  if (iLen >= 64)
    return (-4);

  for (int i = 0; i < iLen; i = i + 3)
  {
    if (isDigit(sExecuteRecordingScript[i]) && isDigit(sExecuteRecordingScript[i+1]))
    {
      int j;
      index = (sExecuteRecordingScript[i] - '0') * 10;
      index += (sExecuteRecordingScript[i+1] - '0');
      for (j=0; j<10; j=j+1)
      {
        if (Points[j].iPoint == index)
          break;
      }
      if (j >= 10)
        return (-1);      
    }
    else
      return(-1);
  }
  return (0);
}

int ExecuteWellplateScript()
{
  int iLen = strlen(sExecuteWellplateScript);

  char cesp32SerialByte = '1';

  if (iSmartPhoneEnabled)
  {
    if (SmartPhoneConnect() == false)
    {
      SendString_ble_F(F("->SmartPhone connection **FAILED**\n  Manually reset SmartPhone!\n"));
#if DEBUG_OUTPUT
      Serial.println(F(" FAILED"));
#endif
      return (0);
    }
  }

  for (int i = 0; i < iLen; i = i + 4)
  {
    int iNextWellplate = (sExecuteWellplateScript[i] - '0');
    int iNextWellplateCell;

    if (sExecuteWellplateScript[i + 1] == '1')
    {
      if (sExecuteWellplateScript[i + 2] == 'F')
        iNextWellplateCell = 1;
      else
        iNextWellplateCell = 3;
    }
    else
    {
      if (sExecuteWellplateScript[i + 2] == 'F')
        iNextWellplateCell = 4;
      else
        iNextWellplateCell = 6;
    }

    for (int iCell = 0; iCell < 3; iCell++)
    {
      int iNextXaxis = (((iNextWellplate - 1) % 3) * 360) + (((iNextWellplateCell - 1) % 3) * 110);
      int iNextYaxis = ((iNextWellplate / 4) * 258) + ((iNextWellplateCell / 4) * 114);

      SmartPhoneMove(iNextXaxis, iNextYaxis, false);
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

      if (iSmartPhoneEnabled)
      {
        bool bRetCode = SendSmartPhoneCommand('A');
      }
      if (bPhotoMode)
      {
#if DEBUG_OUTPUT
        Serial.println(F(" PHOTO"));
#endif
        //      delay(2500);

        if (GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer)))
        {
          if (esp32SerialBuffer[0] == '9' && esp32SerialBuffer[1] == '9')
          {
            esp32Serial.print(F("  Photos aborted\n"));
            SmartPhoneMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
            iWellplate = 1;
            iWellplateCell = 1;
            return (0);
          }
          else
          {
            esp32Serial.print(F("  Commands ignored during photos\n"));
          }

        }
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
          if (GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer)))
          {
            if (esp32SerialBuffer[0] == '9' && esp32SerialBuffer[1] == '9')
            {
              esp32Serial.print(F("  Recording aborted\n"));
              if (iSmartPhoneEnabled)
              {
                bool bRetCode;
                bRetCode = SendSmartPhoneCommand('S');
                bRetCode = SendSmartPhoneCommand('0');
              }
              SmartPhoneMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
              iWellplate = 1;
              iWellplateCell = 1;
              return (0);
            }
            else
            {
              esp32Serial.print(F("  Commands ignored during recording\n"));
            }
          }
        }
#if DEBUG_OUTPUT
        Serial.println(F("Record Video STOP"));
#endif

        if (iSmartPhoneEnabled)
        {
          bool bRetCode = SendSmartPhoneCommand('S');
        }
#if DEBUG_OUTPUT
        Serial.println("RECORD End");
#endif
      }
      if (sExecuteWellplateScript[i + 2] == 'F')
        iNextWellplateCell++;
      else
        iNextWellplateCell--;
    }
  }

  if (iSmartPhoneEnabled)
  {
    SmartPhoneDisconnect();
  }

  SmartPhoneMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
  iWellplate = 1;
  iWellplateCell = 1;

  return (1);
}

int ExecuteRecordingScript()
{
  int iLen = strlen(sExecuteRecordingScript);

  char cesp32SerialByte = '1';

  if (iSmartPhoneEnabled)
  {
    if (SmartPhoneConnect() == false)
    {
      SendString_ble_F(F("->SmartPhone connection **FAILED**\n  Manually reset SmartPhone!\n"));
#if DEBUG_OUTPUT
      Serial.println(F(" FAILED"));
#endif
      return (0);
    }
  }

  for (int i = 0; i < iLen; i = i + 3)
  {
    index = (sExecuteRecordingScript[i] - '0') * 10;
    index += (sExecuteRecordingScript[i+1] - '0');          

    {
      int j;
      int iPointIndex;
      for (j=0; j<10; j=j+1)
      {
        if (Points[j].iPoint == index)
        {
          iPointIndex = j;
          break;
        }
      }
      
      int iNextXaxis = Points[iPointIndex].x;
      int iNextYaxis = Points[iPointIndex].y;

      SmartPhoneMove(iNextXaxis, iNextYaxis, false);

#if DEBUG_OUTPUT
      Serial.print("Point ");
      Serial.println(iPointIndex);
      Serial.println("RECORD Beg");
#endif
      char sNumb[3] = "  ";
      if (bPhotoMode)
        SendString_ble_F(F(" Photo "));
      else
        SendString_ble_F(F(" Recording "));
      sNumb[0] = '0' + (Points[iPointIndex].iPoint/10);
      sNumb[1] = '0' + (Points[iPointIndex].iPoint%10);
      SendString_ble(sNumb);
      SendString_ble_F(F("\n"));

      if (iSmartPhoneEnabled)
      {
        bool bRetCode = SendSmartPhoneCommand('A');
      }
      if (bPhotoMode)
      {
#if DEBUG_OUTPUT
        Serial.println(F(" PHOTO"));
#endif
        //      delay(2500);

        if (GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer)))
        {
          if (esp32SerialBuffer[0] == '9' && esp32SerialBuffer[1] == '9')
          {
            esp32Serial.print(F("  Photos aborted\n"));
            SmartPhoneMove(0, 0, true);
            return (0);
          }
          else
          {
            esp32Serial.print(F("  Commands ignored during photos\n"));
          }

        }
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
          if (GetCommand(esp32SerialBuffer, sizeof(esp32SerialBuffer)))
          {
            if (esp32SerialBuffer[0] == '9' && esp32SerialBuffer[1] == '9')
            {
              esp32Serial.print(F("  Recording aborted\n"));
              if (iSmartPhoneEnabled)
              {
                bool bRetCode;
                bRetCode = SendSmartPhoneCommand('S');
                bRetCode = SendSmartPhoneCommand('0');
              }
              SmartPhoneMove(0, 0, true);
              return (0);
            }
            else
            {
              esp32Serial.print(F("  Commands ignored during recording\n"));
            }
          }
        }
#if DEBUG_OUTPUT
        Serial.println(F("Record Video STOP"));
#endif

        if (iSmartPhoneEnabled)
        {
          bool bRetCode = SendSmartPhoneCommand('S');
        }
#if DEBUG_OUTPUT
        Serial.println("RECORD End");
#endif
      }
    }
  }

  if (iSmartPhoneEnabled)
  {
    SmartPhoneDisconnect();
  }

  SmartPhoneMove(0, 0, true);

  return (1);
}

bool GetCommand(char *pesp32SerialBuffer, int esp32SerialBufferLen)
{
#if 1
  if (bWaitingForSmartPhone)
  {
#if DEBUG_OUTPUT
      Serial.println("IGNORE GetCommand() whilst waiting for SmartPhone");
#endif
    return (false);
  }
#endif
  
  if (esp32Serial.available() > 0)
  {
#if DEBUG_OUTPUT
    Serial.println("GetCommand() true");
#endif

    pesp32SerialBuffer[0] = '\0';
    int iNext = esp32Serial.readBytes(pesp32SerialBuffer, esp32SerialBufferLen);
    pesp32SerialBuffer[iNext] = '\0';

#if DEBUG_OUTPUT
    Serial.print("esp32SerialBuffer = [");
    Serial.print(pesp32SerialBuffer);
    Serial.println("]");
#endif

    return (true);
  }
  return (false);
}

bool WaitForesp32Serial()
{
  lStartTimeMS = millis();
  while (esp32Serial.available() == 0)
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
    if (millis() > (lStartTimeMS + Y_AXIS_TIMEOUT))
    {
#if DEBUG_OUTPUT
      Serial.println("Y-Axis move TIMEOUT");
#endif
      SendString_ble_F(F("Y-Axis move TIMEOUT\n"));
      return (false);
    }
  }
  return (true);
}

bool GetNTP(char *buffer, bool bDisplay)
{
  // clear buffer
  while (esp32Serial.available() != 0)
  {
    char cesp32SerialByte = esp32Serial.read();
  }


  for (int i = 0; i < 5; i++)
  {
    char tmpbuffer[50];

    esp32Serial.print('\x1B');
    delay(500);
    esp32Serial.print("2");
    while (esp32Serial.available() == 0)
    {
      ;
    }
    int iNmbBytes = esp32Serial.readBytes(tmpbuffer, sizeof(tmpbuffer));
    tmpbuffer[iNmbBytes] = '\0';
    tmpbuffer[19] = '\0';
#if DEBUG_OUTPUT
    Serial.print("NTP string size = ");
    Serial.println(iNmbBytes);
#endif
    if (iNmbBytes != 19)
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

void DoJoystick()
{
  int iSW;
  int iVX;
  int iVY;

  while (true)
  {
    GetJoystick(&iSW, &iVX, &iVY);

    if (iSW == 0)
      break;

    int iXaxisNext = iXaxis;
    int iYaxisNext = iYaxis;
    
    if (iVY >= 3000)
    {
      if (iXaxis < 720)
      {
        iXaxisNext = iXaxis + 1;
      }
    }
    if (iVY <= 1000)
    {
      if (iXaxis > 0)
        iXaxisNext = iXaxis - 1;
    }

    if (iVX >= 3000)
    {
      if (iYaxis < 720)
      {
        iYaxisNext = iYaxis + 1;
      }
    }
    if (iVX <= 1000)
    {
      if (iYaxis > 0)
        iYaxisNext = iYaxis - 1;
    }
    
    
    SmartPhoneMove(iXaxisNext, iYaxisNext, false);
  
  }  
  return;
  
#if 0
  while (true)
  {
//    GetJoystick(tmpbuffer, true);
    return;
    
    if (strstr(tmpbuffer, "SW=0") != 0)
      break;
  }
#endif  
}

//bool GetJoystick(/*char *tmpbuffer, bool bDisplay*/)
void GetJoystick(int *iSW, int *iVX, int *iVY)
{
  
  // clear buffer
  while (esp32Serial.available() != 0)
  {
    char cesp32SerialByte = esp32Serial.read();
  }

  char tmpbuffer[50];
  tmpbuffer[0] = '\0';

  esp32Serial.print('\x1B');
  //delay(500);
  esp32Serial.print("J");
  while (esp32Serial.available() == 0)
  {
    ;
  }
  int iNmbBytes = esp32Serial.readBytes(tmpbuffer, sizeof(tmpbuffer));
  tmpbuffer[iNmbBytes] = '\0';
  //tmpbuffer[19] = '\0';
#if 0
  Serial.print("Joystick string size = ");
  Serial.println(iNmbBytes);
  Serial.print("[");
  Serial.print(tmpbuffer);
  Serial.println("]");
#endif

  if (false /*bDisplay*/)
  {
    SendString_ble(tmpbuffer);
    SendString_ble_F(F("\n"));
  }

    *iSW = 0;
    *iVX = 0;
    *iVY = 0;

    char *ptr, *ptr1;
    
    ptr = strstr(tmpbuffer, "SW=");
    ptr1 = 0;
    if (ptr != 0)
      ptr1 = strstr(ptr, ",");    
    if (ptr != 0 &&
        ptr1 != 0)
    {
      *ptr1 = '\0';
      *iSW = atoi(&ptr[3]);
      *ptr1 = ',';
    }
    ptr = strstr(tmpbuffer, "VX=");
    ptr1 = 0;
    if (ptr != 0)
      ptr1 = strstr(ptr, ",");    
    if (ptr != 0 &&
        ptr1 != 0)
    {
      *ptr1 = '\0';
      *iVX = atoi(&ptr[3]);
      *ptr1 = ',';
    }
    ptr = strstr(tmpbuffer, "VY=");
    if (ptr != 0)
    {
      *iVY = atoi(&ptr[3]);
    }

  return (true);
}

void SendInt_ble(int ival)
{
  char sVal[4] = "000";
  sVal[0] += (ival/100);  
  sVal[1] += ((ival/10) % 10);  
  sVal[2] += (ival % 10);
  SendString_ble(sVal); 

#if DEBUG_OUTPUT
  //Serial.print("Send to phone: ");
  Serial.print(sVal);
#endif
   
}

void HelpDisplay()
{
  if (GetNTP(sCurrentNTP, true) == false)
    SendString_ble_F(F("Failed to get NTP\n"));

  SendString_ble_F(F("\nCommands:\n"));
  SendString_ble_F(F("  00 Start recording cells\n"));
  SendString_ble_F(F("  01 Recording time 5 sec\n"));
  SendString_ble_F(F("  02 Recording time 3 min\n"));
  SendString_ble_F(F("  03 Recording time 5 min\n"));
  SendString_ble_F(F("  04 Disable SmartPhone, slider only\n"));
  SendString_ble_F(F("  05 Enable SmartPhone\n"));
  SendString_ble_F(F("  06 Video Mode SmartPhone\n"));
  SendString_ble_F(F("  07 Photo Mode SmartPhone\n"));
  SendString_ble_F(F("  08 Test SmartPhone Connect\Disconnect\n"));
  SendString_ble_F(F("  09 Get NTP current time\n"));
  SendString_ble_F(F("  10 Script display\n"));
  SendString_ble_F(F("  11 Set start time HH:MM\n"));
  SendString_ble_F(F("  12 Display start time\n"));
  SendString_ble_F(F("  13 Begin recording at start time\n"));
  SendString_ble_F(F("  14 Turn ON daylight time\n"));
  SendString_ble_F(F("  15 Turn OFF daylight time\n"));
  SendString_ble_F(F("  16 Joystick\n"));
  SendString_ble_F(F("  17 Show position\n"));
  SendString_ble_F(F("  88 Show additional commands\n"));
  SendString_ble_F(F("  99 Abort recording cells\n"));
  SendString_ble_F(F("  X= Script ('wrd')\n"));
}

void AdditionalCommandsDisplay()
{
  SendString_ble_F(F("\nAdditional Commands:\n"));
  SendString_ble_F(F("  Fxxx Move forward xxx steps in Y\n"));
  SendString_ble_F(F("  Bxxx Move backward xxx steps in Y\n"));
  SendString_ble_F(F("  Lxxx Move forward xxx steps in X\n"));
  SendString_ble_F(F("  Rxxx Move backward xxx steps in X\n"));
  SendString_ble_F(F("  Xxxx Move to X position\n"));
  SendString_ble_F(F("  Yxxx Move to Y position\n"));
  SendString_ble_F(F("  Wx Move to wellplate x\n"));
  SendString_ble_F(F("  Cx-x Move to wellplate x and cell x\n"));
  SendString_ble_F(F("  Pxx Record position as point xx\n"));
  SendString_ble_F(F("  Dxx Delete point xx\n"));
  SendString_ble_F(F("  Gxx Move to point xx\n"));
  SendString_ble_F(F("  Sxx Show position of point xx\n"));
  SendString_ble_F(F("  R=xx,xx,xx Record at point xx,xx,...\n"));
}
