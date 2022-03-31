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

 **************************************************************************/

#define PROGRAM "GoPro Slider Recorder"
#define VERSION "Ver 0.9 2022-03-30"

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

SoftwareSerial y_axisSerial(3, 2);

SoftwareSerial esp32Serial(6, 7);

static char esp32SerialBuffer[128] = "";


static bool bPhotoMode = false;

long lStartTimeMS = 0;
long lWaitTimeMS = 0;

bool  bDoStartTime = false;
char  sStartTime[6] = "";

static bool bWaitingForGoPro = false;

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

int iShowCommand = true;

int iGoProEnabled = true;


char sExecuteScript[128] = "";

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
    SendString_ble_F(F(" GoPro "));
    if (iGoProEnabled)
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

        // Video mode
        case 6:
          bPhotoMode = false;
          SendGoProCommand('V');
          break;

        // Photo mode
        case 7:
          bPhotoMode = true;
          SendGoProCommand('P');
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
          SendGoProCommand('3');
#if DEBUG_OUTPUT
          Serial.print(F("Turn ON daylight savings"));
          Serial.println(sStartTime);
#endif
          break;

        case 15:
          SendString_ble_F(F("Turn OFF daylight savings\n"));
          SendGoProCommand('4');
#if DEBUG_OUTPUT
          Serial.print(F("Turn OFF daylight savings"));
          Serial.println(sStartTime);
#endif
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

      GoProMove(iXaxis, iYaxis + iSteps, true);
    }
    else if (chCommand == 'B' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      GoProMove(iXaxis, iYaxis - iSteps, true);
    }
    else if (chCommand == 'L' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      GoProMove(iXaxis + iSteps, iYaxis, true);
    }
    else if (chCommand == 'R' && isDigit(esp32SerialBuffer[1]) && isDigit(esp32SerialBuffer[2]) && isDigit(esp32SerialBuffer[3]))
    {
      iSteps = (esp32SerialBuffer[1] - '0') * 100;
      iSteps += (esp32SerialBuffer[2] - '0') * 10;
      iSteps += (esp32SerialBuffer[3] - '0');

      GoProMove(iXaxis - iSteps, iYaxis, true);
    }
    else if (chCommand == 'W' && isDigit(esp32SerialBuffer[1]))
    {
      int iNextWellplate = (esp32SerialBuffer[1] - '0');

      GoProMove(WellplatesCoords[iNextWellplate - 1].x, WellplatesCoords[iNextWellplate - 1].y, true);
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

        GoProMove(iNextXaxis, iNextYaxis, true);
        iWellplate = iNextWellplate;
        iWellplateCell = iNextWellplateCell;
      }
    }
    else if (chCommand == 'X' && esp32SerialBuffer[1] == '=')
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
          int iRetCode = ExecuteScript();
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
  // do single steps in the forward or backward directions
  motor->step(iVal, iDirection, SINGLE);
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

#if DEBUG_OUTPUT
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
#if DEBUG_OUTPUT
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

bool SendGoProCommand(char cCommand)
{
  bool bRetCode = true;
  bWaitingForGoPro = true;
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

  bWaitingForGoPro = false;
  return (bRetCode);
}

bool GoProConnect()
{
  bool bRetCode;
#if DEBUG_OUTPUT
  Serial.println("CONNECT");
#endif

  if (bPhotoMode)
    bRetCode = SendGoProCommand('P');
  else
    bRetCode = SendGoProCommand('V');

  if (bRetCode)
    bRetCode = SendGoProCommand('1');
  return (bRetCode);
}

bool GoProDisconnect()
{
  bool bRetCode;
#if DEBUG_OUTPUT
  Serial.println("DISCONNECT");
#endif

  bRetCode = SendGoProCommand('0');
  return (bRetCode);
}

int ProcessScript()
{

  strcpy(sExecuteScript, &esp32SerialBuffer[2]);
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

  char cesp32SerialByte = '1';

  if (iGoProEnabled)
  {
    if (GoProConnect() == false)
    {
      SendString_ble_F(F("->GoPro connection **FAILED**\n  Manually reset GoPro!\n"));
#if DEBUG_OUTPUT
      Serial.println(F(" FAILED"));
#endif
      return (0);
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
      {
        bool bRetCode = SendGoProCommand('A');
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
            GoProMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
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
              if (iGoProEnabled)
              {
                bool bRetCode;
                bRetCode = SendGoProCommand('S');
                bRetCode = SendGoProCommand('0');
              }
              GoProMove(WellplatesCoords[0].x, WellplatesCoords[0].y, true);
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

        if (iGoProEnabled)
        {
          bool bRetCode = SendGoProCommand('S');
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

  return (1);
}

bool GetCommand(char *pesp32SerialBuffer, int esp32SerialBufferLen)
{
#if 1
  if (bWaitingForGoPro)
  {
#if DEBUG_OUTPUT
      Serial.println("IGNORE GetCommand() whilst waiting for GoPro");
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

void HelpDisplay()
{
  if (GetNTP(sCurrentNTP, true) == false)
    SendString_ble_F(F("Failed to get NTP\n"));

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
