/*
 * DhcpPingMonitor.ino - Library for storing large arrays of bits efficiently
 * Created by Matthew R Miller, February 14, 2021
 * 
 * This is a simple program to monitor Internet PING connectivity with an Arduino
 * Requirements:
 * - Arduino Uno (or similar)
 * - W5100 Ethernet Shield
 * - Ethernet Library 1.x (ICMP Ping is not 2.0 compatible)
 * - ICMPPing library (https://github.com/BlakeFoster/Arduino-Pingv)
 * - BoolBits Library (https://github.com/mmiller7/BoolBits)
 * - Optional LCD (suggest HiLetgo 1602 LCD Keypad Shield 1602 LCD)
 *     NOTE: Must not conflict with Ethernet Shield pins
 *     NOTE: the suggested shield, pin 10 conflicts with the Ethernet Shield, but can be removed
 * 
 * Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 *
 * HiLetgo 1602 LCD Keypad Shield 1602 LCD
 * LiquidCrystal lcd(8, 9, 4, 5, 6, 7);s
 * LCD RS pin to digital pin 8
 * LCD Enable pin to digital pin 9
 * LCD D4 pin to digital pin 4
 * LCD D5 pin to digital pin 5
 * LCD D6 pin to digital pin 6
 * LCD D7 pin to digital pin 7
 * Backlight control pin 10 (when high, may be short thru transistor on some boards?)
 * Suggested mod:
 *    Cut off or desolder pin 10, put diode from pin 10 to pin 3 (stripe towards pin 3)
 *    This eliminages the conflict with Ethernet shield and fixes the short
 *    thru the diode when set to "high" for backlight on, and is PWM.
 * Keypad Buttons pin A0
 * Idle - 1023
 * Select - 720
 * Left - 480
 * Down - 305
 * Up   - 131
 * Right - 0
 *
 * **** WARNING - pin 10 overlaps/conflicts with Ethernet, must be disconnected!
 *
 * The buttons (except Reset) are all connected through resistors to AO. The APPROXIMATE analog readings are:
 * Select: 720
 * Left: 480
 * Down: 305
 * Up: 130
 * Right: 0 (this one goes straight to ground when closed)
 *
 */

#define ENABLE_SERIAL   //Uncomment to enable serial
#define ENABLE_LCD      //Uncomment to enable LCD
// Additional print information optional (requires ENABLE_SERIAL)
//#define ETIME_CHECK     //Uncomment to print eTime in loop
//#define RAM_CHECK       //Uncomment to print free RAM in loop
//#define ENABLE_VERBOSE  //Uncomment to print extra PING stats

#define LCD_DELAY 2000 //mS to wait for info messages on LCD

#include <SPI.h>
#include <Ethernet.h>
#include <ICMPPing.h>
#include <BoolBits.h>

#ifdef ENABLE_LCD
#include <LiquidCrystal.h>
#endif

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = {  
  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
  
//Addresses to PING
IPAddress firstPingAddr(1,1,1,1); // ip address to ping
IPAddress secondPingAddr(8,8,8,8); // ip address to ping
#define PING_MAX_COUNT 300 // Number of pings for rolling average
// NOTE: 100 pings makes even 1% per ping
//       100 pings, at 1 sec timeout and 3 IPs makes 300 sec loop or 5 min
//       300 pings, at 1 sec timeout and 3 IPs makes 900 sec loop or 15 min
//       600 pings, at 1 sec timeout and 3 IPs makes 1800 sec loop or 30 min
//       1200 pings, at 1 sec timeout and 3 IPs makes 3600 sec loop or 1 hour

//Timeouts
#define ICMP_PING_TIMEOUT    950
#define NUMBER_OF_IPS        3
#define PROCESSING_LOOP_TIME 150
#define PROCESSING_LOOP_INTERVAL ((ICMP_PING_TIMEOUT * NUMBER_OF_IPS) + PROCESSING_LOOP_TIME) //The interval for pings - must be number of IPs * timeout + time for overhead processing

//Data for PING
float gatewayAvgLoss=0;
float firstAddrLoss=0;
float secondAddrLoss=0;

//Data for PING
SOCKET pingSocket = 0;
ICMPPing ping(pingSocket, 1);
#ifdef ENABLE_VERBOSE
char buffer [256];
#endif

//Data for control loop
long stime=0;
byte ethernetStatus=0;

//LCD pins
#ifdef ENABLE_LCD
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#define LCD_BACKLIGHT_PIN 3
#define LCD_BACKLIGHT_BRIGHTNESS 64
#endif

//Functiton declarations
inline void ethernetRenewMaintenance();
inline boolean doPing(IPAddress pingAddr, int x, int y);
inline int getAvgBool(byte data[]);
inline float getFloatAvgBool(byte data[]);
inline void lcdPrintOneDecimal(float value);
inline void currentPingNumInc();
inline void serialPrintIpAddr(IPAddress ipAddr);
inline void lcdPrintIpAddr(IPAddress ipAddr);
inline void lcdClockSpin(int x, int y);
inline byte getLcdButton();
inline void alarm(int timeMs);

//Structures to store ping data
BoolBits gatewayPings(PING_MAX_COUNT);
BoolBits firstAddrPings(PING_MAX_COUNT);
BoolBits secondAddrPings(PING_MAX_COUNT);
int currentPingNum=0; //to keep track of where we are in the array
boolean firstRun=true;
               
//Constants for keypad on LCD
#define BUTTON_INPUT_PIN A0
#define BUTTON_NONE 0
#define BUTTON_SELECT 1
#define BUTTON_LEFT 2
#define BUTTON_DOWN 3
#define BUTTON_UP 4
#define BUTTON_RIGHT 5



#ifdef RAM_CHECK
#ifdef ENABLE_SERIAL
// free RAM check for debugging. SRAM for ATmega328p = 2048Kb.
int availableMemory() {
    // Use 1024 with ATmega168
    int size = 2048;
    byte *buf;
    while ((buf = (byte *) malloc(--size)) == NULL);
        free(buf);
    return size;
}
#endif
#endif

inline void buildCustomChars()
{
  //Custom Characters
  byte smallCheck[8] = {
                     B00000,
                     B00000,
                     B00000,
                     B00000,
                     B00010,
                     B10100,
                     B01000
                   };
  lcd.createChar(0,smallCheck);
  #define LCD_CHAR_SMALL_CHECK byte(0)
  
  byte smallX[8] = {
                     B00000,
                     B00000,
                     B00000,
                     B00000,
                     B10100,
                     B01000,
                     B10100
                   };
  lcd.createChar(1,smallX);
  #define LCD_CHAR_SMALL_X byte(1)
  
  byte hourglass[8] = {
                     B11111,
                     B11011,
                     B01110,
                     B00100,
                     B01010,
                     B10101,
                     B11111
                   };
  lcd.createChar(2,hourglass);
  #define LCD_CHAR_HOURGLASS byte(2)
  
  /*byte clock1[8] = {
                     B00000,
                     B01110,
                     B10101,
                     B10101,
                     B10001,
                     B01110,
                     B00000
                   };*/
  byte clock1[8] = {
                     B00000,
                     B01010,
                     B11011,
                     B11011,
                     B11111,
                     B01110,
                     B00000
                   };
  lcd.createChar(3,clock1);
  #define LCD_CHAR_CLOCK1 byte(3)
  
  /*byte clock2[8] = {
                     B00000,
                     B01110,
                     B10001,
                     B10101,
                     B10011,
                     B01110,
                     B00000
                   };*/
  byte clock2[8] = {
                     B00000,
                     B01110,
                     B11111,
                     B11011,
                     B11101,
                     B01110,
                     B00000
                   };
  lcd.createChar(4,clock2);
  #define LCD_CHAR_CLOCK2 byte(4)
  
  /*byte clock3[8] = {
                     B00000,
                     B01110,
                     B10001,
                     B10101,
                     B11001,
                     B01110,
                     B00000
                   };*/
  byte clock3[8] = {
                     B00000,
                     B01110,
                     B11111,
                     B11011,
                     B10111,
                     B1110,
                     B00000
                   };
  lcd.createChar(5,clock3);
  #define LCD_CHAR_CLOCK3 byte(5)
  
  byte inversePercent[8] = {
                     B11111,
                     B10110,
                     B11101,
                     B11011,
                     B10111,
                     B01101,
                     B11111
                   };
  lcd.createChar(6,inversePercent);
  #define LCD_CHAR_INVERSE_PERCENT byte(6)
  
  byte inverseT[8] = {
                     B11111,
                     B10001,
                     B11011,
                     B11011,
                     B11011,
                     B11011,
                     B11111
                   };
  lcd.createChar(7,inverseT);
  #define LCD_CHAR_INVERSE_T byte(7)

  
  //#define DEBUG_LCD_CHARS  //Uncomment to Debug: print the characters to verify
  #ifdef DEBUG_LCD_CHARS
  lcd.setCursor(0,1);
  for(int x=0; x < 8; x++)
    lcd.write(byte(x));
  delay(30000);
  #endif//DEBUG_LCD_CHARS
}



void setup() {
  //Initialize hardware for LCD buttons
  pinMode(BUTTON_INPUT_PIN,INPUT);
  
  // Initialize serial
  #ifdef ENABLE_SERIAL
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Booting..."));
  #endif

  #ifdef RAM_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Start of setup - Free memory: "));
  Serial.println(availableMemory());
  Serial.println();
  #endif
  #endif
  
  // Initialize LCD
  #ifdef ENABLE_LCD
  lcd.begin(16,2);
  lcd.setCursor(3,0);
  lcd.print(F("Booting..."));
  // Backlight
  pinMode(LCD_BACKLIGHT_PIN,OUTPUT);
  analogWrite(LCD_BACKLIGHT_PIN,LCD_BACKLIGHT_BRIGHTNESS);
  // Build custom chars
  buildCustomChars();
  // Display hour glass in corner during boot
  lcd.setCursor(15,0);
  lcd.write(LCD_CHAR_HOURGLASS);
  #endif//ENABLE_LCD

  // Initialize Ethernet connection:
  #ifdef ENABLE_LCD
  lcd.setCursor(0,1);
  lcd.print(F("Waiting for DHCP"));
  #endif
  #ifdef ENABLE_SERIAL
  Serial.println(F("Waiting for Network DHCP"));
  #endif
  int dhcpTry=1;
  while (Ethernet.begin(mac) == 0)
  {
    dhcpTry++;
    #ifdef ENABLE_SERIAL
    Serial.print(F("Failed to configure Ethernet using DHCP, try "));
    Serial.println(dhcpTry);
    #endif
    #ifdef ENABLE_LCD
    lcd.setCursor(0,1);
    lcd.print(F("DHCP: Retry     "));
    lcd.setCursor(12,1);
    lcd.print(dhcpTry);
    #endif
    alarm(100);
  }

  
  
  #ifdef ENABLE_SERIAL
  // Print local IP address:
  Serial.print(F("DHCP IP address: "));
  serialPrintIpAddr(Ethernet.localIP());
  Serial.println();
  #endif
  #ifdef ENABLE_LCD
  // Print local IP address:
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("DHCP IP Address:"));
  lcd.setCursor(0,1);
  lcdPrintIpAddr(Ethernet.localIP());
  delay(LCD_DELAY);
  #endif

  #ifdef ENABLE_SERIAL
  // Print gateway IP address:
  Serial.print(F("DHCP Gateway IP: "));
  serialPrintIpAddr(Ethernet.gatewayIP());
  Serial.println(); 
  #endif
  #ifdef ENABLE_LCD
  // Print gateway IP address:
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("DHCP Gateway IP:"));
  lcd.setCursor(0,1);
  lcdPrintIpAddr(Ethernet.gatewayIP());
  delay(LCD_DELAY);
  #endif
  
  #ifdef ENABLE_SERIAL
  // Print first test IP address:
  Serial.print(F("1st Test IP: "));
  serialPrintIpAddr(firstPingAddr);
  Serial.println(); 
  #endif
  #ifdef ENABLE_LCD
  // Print first test IP address:
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("1st Test IP:"));
  lcd.setCursor(0,1);
  lcdPrintIpAddr(firstPingAddr);
  delay(LCD_DELAY);
  #endif
  
  #ifdef ENABLE_SERIAL
  // Print first test IP address:
  Serial.print(F("2nd Test IP: "));
  serialPrintIpAddr(secondPingAddr);
  Serial.println(); 
  #endif
  #ifdef ENABLE_LCD
  // Print first test IP address:
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("2nd Test IP:"));
  lcd.setCursor(0,1);
  lcdPrintIpAddr(secondPingAddr);
  delay(LCD_DELAY);
  #endif
  
  
  
  #ifdef ENABLE_SERIAL
  Serial.println();
  Serial.println(F("Starting tests..."));
  Serial.println();
  #endif
  
  #ifdef ENABLE_LCD
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Starting Tests  "));
  lcd.setCursor(0,1);
  //           100 100 100 %
  //           1234567890123456
  //lcd.print(F("GW  IP1 IP2 LOSS"));
  #endif

  ICMPPing::setTimeout(ICMP_PING_TIMEOUT);

  #ifdef RAM_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("End of setup - Free memory: "));
  Serial.println(availableMemory());
  Serial.println();
  #endif
  #endif
}



void loop() {
  // Setup timekeeping variable
  stime=millis();
  
  #ifdef ETIME_CHECK
  Serial.println(F("Starting PING loop"));
  #endif

  // Ping and store stats
  lcd.setCursor(0,1);
  lcd.print(F("GW  IP1 IP2 LOSS"));
  gatewayPings.setBool( currentPingNum, !doPing(Ethernet.gatewayIP(), 2,1) );
  firstAddrPings.setBool( currentPingNum, !doPing(firstPingAddr, 7,1) );
  secondAddrPings.setBool( currentPingNum, !doPing(secondPingAddr, 11,1) );
  
  // Increment for next loop
  currentPingNumInc();
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Pings done in: "));
  Serial.println(millis()-stime);
  #endif
  #endif
  
  // Compute averages
  gatewayAvgLoss=getFloatAvgBool(gatewayPings);
  firstAddrLoss=getFloatAvgBool(firstAddrPings);
  secondAddrLoss=getFloatAvgBool(secondAddrPings);
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Stats computed in: "));
  Serial.println(millis()-stime); 
  #endif
  #endif
  
  
  // Print Stats
  #ifdef ENABLE_SERIAL
  Serial.print(F("Avg Loss Gateway "));
  serialPrintIpAddr(Ethernet.gatewayIP());
  Serial.print(F(": "));
  Serial.print(gatewayAvgLoss);
  Serial.println(F("%"));
  
  Serial.print(F("Avg Loss IP "));
  serialPrintIpAddr(firstPingAddr);
  Serial.print(F(": "));
  Serial.print(firstAddrLoss);
  Serial.println(F("%"));
  
  Serial.print(F("Avg Loss IP "));
  serialPrintIpAddr(secondPingAddr);
  Serial.print(F(": "));
  Serial.print(secondAddrLoss);
  Serial.println(F("%"));

  // Make it look neater with blank line to separate loops
  Serial.println();
  #endif
  
  #ifdef ENABLE_LCD
  lcd.setCursor(0,0);
  lcd.print(F("            %   "));
  /*lcd.setCursor(0,1);
  //           100 100 100 100%
  //           1234567890123456
  lcd.print(F("GW  IP1 IP2 LOSS"));*/
  lcd.setCursor(0,0);
  if(gatewayAvgLoss < 10)
  {
    lcdPrintOneDecimal(gatewayAvgLoss);
  }
  else
  {
    lcd.print(round(gatewayAvgLoss));
  }
  lcd.setCursor(4,0);
  if(firstAddrLoss < 10)
  {
    lcdPrintOneDecimal(firstAddrLoss);
  }
  else
  {
    lcd.print(round(firstAddrLoss));
  }
  lcd.setCursor(8,0);
  if(secondAddrLoss < 10)
  {
    lcdPrintOneDecimal(secondAddrLoss);
  }
  else
  {
    lcd.print(round(secondAddrLoss));
  }
  #endif
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Stats printed in: "));
  Serial.println(millis()-stime); 
  #endif
  #endif
  
  ethernetRenewMaintenance();

  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Loop done in: "));
  Serial.println(millis()-stime);
  #endif
  #endif
  
  #ifdef RAM_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Free memory: "));
  Serial.println(availableMemory());
  Serial.println();
  #endif
  #endif

  
  //wait the remainder of seconds
  //When pings fail, about 1.55 Seconds per ping
  //100 pings at 6 seconds per is 10 minute average
  while(millis()-stime < PROCESSING_LOOP_INTERVAL)
  {
    //Hurry up and wait...nothing more to do

    //So we know it isn't dead, do something
    #ifdef ENABLE_LCD
    lcdClockSpin(15,0);
    /*
    //Bounce cursor
    if((millis()/500)%2 == 0)
    {
      lcd.setCursor(15,0);
    }
    else
    {
      lcd.setCursor(15,1);
    }
    lcd.cursor();
    //lcd.noBlink();
    */
    #endif
  }
  #ifdef ENABLE_LCD
  // Blank over the clock
  lcd.setCursor(15,0);
  lcd.print(F(" "));
  /*
  //Turn off the cursor
  lcd.noCursor();
  //lcd.noBlink();
  */
  #endif 
}


inline void ethernetRenewMaintenance()
{
  
  //handle ethernet renewing
  ethernetStatus=Ethernet.maintain();
  /*
  Returns byte:
  0: nothing happened
  1: renew failed
  2: renew success
  3: rebind fail
  4: rebind success
  */
  if(ethernetStatus > 0)
  {
    #ifdef ENABLE_LCD
    lcd.setCursor(0,0);
    lcd.print(F("Ethernet Maint: "));
    lcd.setCursor(0,1);
    #endif

    if(ethernetStatus == 1)
    {
      #ifdef ENABLE_LCD
      lcd.print(F(" <!> Renew Fail "));
      alarm(10000);
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Renew Failure"));
      #endif
    }
    else if(ethernetStatus == 2)
    {
      #ifdef ENABLE_LCD
      lcd.print(F("    Renew OK    "));
      alarm(1000);
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Renew Success"));
      #endif
    }
    else if(ethernetStatus == 3)
    {
      #ifdef ENABLE_LCD
      lcd.print(F(" <!> Rebind Fail"));
      alarm(10000);
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Rebind Failure"));
      #endif
    }
    else if(ethernetStatus == 4)
    {
      #ifdef ENABLE_LCD
      lcd.print(F("   Rebind OK    "));
      alarm(1000);
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Rebind Success"));
      #endif
    }
    else
    {
      #ifdef ENABLE_LCD
      lcd.print(F("Unknown Code    "));
      lcd.setCursor(1,14);
      lcd.print(ethernetStatus);
      alarm(10000);
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.print(F("Ethernet Maintenance: Unknown code "));
      Serial.println(ethernetStatus);
      #endif
    }
  }
}

//Returns TRUE if success
//Takes x, y for LCD status of ping
inline boolean doPing(IPAddress pingAddr, int x, int y)
{
  #ifdef ETIME_CHECK
  Serial.println(F("Starting doPing call"));
  long eTime=millis();
  #endif

  #ifdef ENABLE_LCD
  lcd.setCursor(x,y);
  lcd.write(LCD_CHAR_HOURGLASS);
  #endif

  //Prepare to save the result
  ICMPEchoReply echoReply;
  
  #ifdef ICMPPING_ASYNCH_ENABLE
  /* Begin New code */ 
  
      //Send the ping
      if (! ping.asyncStart(pingAddr, 1, echoReply))
      {
        #ifdef ENABLE_SERIAL
        #ifdef ENABLE_VERBOSE
        Serial.print("Couldn't even send our ping request?? Status: ");
        Serial.println((int)echoReply.status);
        #endif
        #endif

        //If we can't send the ping, clearly it is lost
        #ifdef ENABLE_LCD
        lcd.setCursor(x,y);
        lcd.write(LCD_CHAR_SMALL_X);
        #endif
    
        return false;
      }
    
      //Wait for the PING
      while (! ping.asyncComplete(echoReply))
      {
        //Hurry up and wait...nothing more to do

        //Spin the LCD clock icon
        #ifdef ENABLE_LCD
        lcdClockSpin(x,y);
        #endif
      }

  /* End New code */ 
  #else // Not set ICMPPING_ASYNCH_ENABLE
  /* Begin Old code */ 

      // Ping the address, retry 1 time
      ping(pingAddr, 1, echoReply);

  /* End Old code */ 
  #endif //End check ICMPPING_ASYNCH_ENABLE


  #ifdef ETIME_CHECK
  Serial.print(F("returned from ping call: "));
  Serial.println(millis()-eTime);
  #endif
  
  if (echoReply.status == SUCCESS)
  {
    #ifdef ENABLE_SERIAL
    #ifdef ENABLE_VERBOSE
    sprintf(buffer,
            "Reply[%d] from: %d.%d.%d.%d: bytes=%d time=%ldms TTL=%d",
            echoReply.data.seq,
            echoReply.addr[0],
            echoReply.addr[1],
            echoReply.addr[2],
            echoReply.addr[3],
            REQ_DATASIZE,
            millis() - echoReply.data.time,
            echoReply.ttl);

    Serial.println(buffer);
    #endif
    #endif

    #ifdef ETIME_CHECK
    Serial.print(F("About to return true: "));
    Serial.println(millis()-eTime);
    #endif

    #ifdef ENABLE_LCD
    lcd.setCursor(x,y);
    lcd.write(LCD_CHAR_SMALL_CHECK);
    #endif
    
    return true;
  }
  else
  {
    #ifdef ENABLE_SERIAL
    #ifdef ENABLE_VERBOSE
    sprintf(buffer, "Echo request failed; %d", echoReply.status);
    Serial.println(buffer);
    #endif
    #endif

    #ifdef ETIME_CHECK
    Serial.print(F("About to return false: "));
    Serial.println(millis()-eTime);
    #endif
        
    #ifdef ENABLE_LCD
    lcd.setCursor(x,y);
    lcd.write(LCD_CHAR_SMALL_X);
    #endif

    return false;
  }
}

inline int getAvgBool(BoolBits& data)
{
  if(firstRun)
  {
    return data.getAvgBool( 0, currentPingNum);
  }
  else
  {
    return data.getAvgBool( 0, PING_MAX_COUNT);
  }
}

inline float getFloatAvgBool(BoolBits& data)
{
  if(firstRun)
  {
    return data.getFloatAvgBool( 0, currentPingNum);
  }
  else
  {
    return data.getFloatAvgBool( 0, PING_MAX_COUNT);
  }
}

inline void lcdPrintOneDecimal(float value)
{    
  int firstPart=(int)value;
  int secondPart=round((value-firstPart)*10);
  
  //handle case where rounding bumps 0.9 to 1.0
  if(secondPart > 9)
  {
    firstPart++;
    secondPart=0;
  }

  //Print it
  lcd.print(firstPart);
  lcd.print(F("."));
  lcd.print(secondPart);
}

inline void currentPingNumInc()
{
  currentPingNum++;
  
  if(currentPingNum == PING_MAX_COUNT)
  {
    currentPingNum=0;
    firstRun=false;
  }
}


inline void serialPrintIpAddr(IPAddress ipAddr)
{
  #ifdef ENABLE_SERIAL
  for (byte octet = 0; octet < 4; octet++)
  {
    // print the value of each byte of the IP address:
    Serial.print(ipAddr[octet], DEC);
    if(octet < 3)
    {
      Serial.print(F("."));
    }
  }
  #endif
}
  
inline void lcdPrintIpAddr(IPAddress ipAddr)
{
  #ifdef ENABLE_LCD
  for(byte octet=0; octet < 4; octet++)
  {
    lcd.print(ipAddr[octet]);
    if(octet < 3)
    {
      lcd.print(F("."));
    }
  }
  #endif
}

inline void lcdClockSpin(int x, int y)
{
  #define SPIN_RATE 250
  
  if(millis()%SPIN_RATE == 0)
  {
    lcd.setCursor(x,y);
    switch((millis()/SPIN_RATE)%3)
    {
      case 0:
              lcd.write(LCD_CHAR_CLOCK1);
              break;
      case 1:
              lcd.write(LCD_CHAR_CLOCK2);
              break;
      case 2:
              lcd.write(LCD_CHAR_CLOCK3);
              break;
    }
  }
}

inline byte getLcdButton()
{
  int buttonValue = analogRead(BUTTON_INPUT_PIN);
  
  if(buttonValue > 871)
  {
    return BUTTON_NONE;
  }
  else if(buttonValue > 600)
  {
    return BUTTON_SELECT;
  }
  else if(buttonValue > 392)
  {
    return BUTTON_LEFT;
  }
  else if(buttonValue > 218)
  {
    return BUTTON_DOWN;
  }
  else if(buttonValue > 66)
  {
    return BUTTON_UP;
  }
  else
  {
    return BUTTON_RIGHT;
  }
}

inline void alarm(int timeMs)
{
  long etime=millis();

  analogWrite(LCD_BACKLIGHT_PIN,0);
  delay(50);
  analogWrite(LCD_BACKLIGHT_PIN,255);
  delay(50);

  while(millis()-etime < timeMs)
  {
    if(millis() % 100 == 0)
    {
      if(millis()/500 % 2 == 0)
      {
        analogWrite(LCD_BACKLIGHT_PIN,0);
      }
      else
      {
        analogWrite(LCD_BACKLIGHT_PIN,255);
      }
    }
  }
  
  analogWrite(LCD_BACKLIGHT_PIN,LCD_BACKLIGHT_BRIGHTNESS);
}
