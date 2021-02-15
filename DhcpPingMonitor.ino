/*
 * DhcpPingMonitor.ino - Library for storing large arrays of bits efficiently
 * Created by Matthew R Miller, February 14, 2021
 * 
 * This is a simple program to monitor Internet PING connectivity with an Arduino
 * Requirements:
 * - Arduino Uno (or similar)
 * - W5100 Ethernet Shield
 * - Ethernet Library 1.x (ICMP Ping is not 2.0 compatible)
 * - ICMP Ping library (https://github.com/BlakeFoster/Arduino-Pingv)
 * - Optional LCD (suggest HiLetgo 1602 LCD Keypad Shield 1602 LCD)
 *   NOTE: Must not conflict with Ethernet Shield pins
 *   NOTE: the suggested shield, pin 10 conflicts with the Ethernet Shield, but can be removed
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
 * Backlight control pin 10 (may be short thru transistor on some boards?)
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

#define ENABLE_SERIAL //Uncomment to enable serial
#define ENABLE_LCD    //Uncomment to enable LCD
//#define ETIME_CHECK   //Uncomment to print eTime in loop
//#define RAM_CHECK     //Uncomment to print free RAM in loop

#define LCD_DELAY 2000 //mS to wait for info messages on LCD

#include <SPI.h>
#include <Ethernet.h>
#include <ICMPPing.h>

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
#define PING_MAX_COUNT 100 // Number of pings for rolling average

#define ICMP_PING_TIMEOUT    800
#define NUMBER_OF_IPS        3
#define PROCESSING_LOOP_TIME 600
#define PROCESSING_LOOP_INTERVAL ((ICMP_PING_TIMEOUT * NUMBER_OF_IPS) + PROCESSING_LOOP_TIME) //The interval for pings - must be number of IPs * timeout + time for overhead processing

//Data for PING
int gatewayAvgLoss=0;
int firstAddrLoss=0;
int secondAddrLoss=0;

//Data for PING
SOCKET pingSocket = 0;
char buffer [256];
ICMPPing ping(pingSocket, 1);

//Data for control loop
long stime=0;
byte ethernetStatus=0;

//LCD pins
#ifdef ENABLE_LCD
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#endif

//Functiton declarations
inline boolean doPing(IPAddress pingAddr);
inline void setBool(byte data[], int pos, boolean value);
inline boolean getBool(byte data[], int pos);
inline int getAvgBool(byte data[], int startPos, int endPos);
inline int getAvgBool(byte data[]);
inline void currentPingNumInc();
inline void serialPrintIpAddr(IPAddress ipAddr);
inline void lcdPrintIpAddr(IPAddress ipAddr);
inline byte getLcdButton();

//Arrays to store ping data
#define PING_BYTE_MAX ( (int) ((PING_MAX_COUNT/8.0)+0.5) ) //"rounded up" number of bytes
byte gatewayPings[PING_BYTE_MAX];
byte firstAddrPings[PING_BYTE_MAX];
byte secondAddrPings[PING_BYTE_MAX];
int currentPingNum=0; //to keep track of where we are in the array
boolean firstRun=true;

//Bit-mask for get/set bit access functitons
static byte BIT_MASK[8] = {
                 0b00000001,
                 0b00000010,
                 0b00000100,
                 0b00001000,
                 0b00010000,
                 0b00100000,
                 0b01000000,
                 0b10000000
               };
               
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


void setup() {
  //Initialize hardware for LCD buttons
  
  
  // Initialize serial
  #ifdef ENABLE_SERIAL
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Booting..."));
  #endif
  
  // Initialize LCD
  #ifdef ENABLE_LCD
  lcd.begin(16,2);
  lcd.setCursor(3,0);
  lcd.print(F("Booting..."));
  delay(1000);
  #endif
  
  // Initialize arrays
  for(int x=0; x < PING_BYTE_MAX; x++)
  {
    gatewayPings[x]=0;
    firstAddrPings[x]=0;
    secondAddrPings[x]=0;
  }


  // Initialize Ethernet connection:
  #ifdef ENABLE_LCD
  lcd.setCursor(0,1);
  //           1234567890123456
  lcd.print(F("Waiting for DHCP"));
  #endif
  #ifdef ENABLE_SERIAL
  Serial.println(F("Waiting for Network DHCP"));
  #endif
  while (Ethernet.begin(mac) == 0)
  {
    #ifdef ENABLE_SERIAL
    Serial.println(F("Failed to configure Ethernet using DHCP, retrying..."));
    #endif
    #ifdef ENABLE_LCD
    lcd.setCursor(0,1);
    lcd.print(F("Retry: DHCP Fail"));
    #endif
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
  lcd.setCursor(0,0);
  lcd.print(F("Starting Tests  "));
  lcd.setCursor(0,1);
  //           100 100 100 %
  //           1234567890123456
  lcd.print(F("GW  IP1 IP2 LOSS"));
  #endif

  ICMPPing::setTimeout(ICMP_PING_TIMEOUT);
}


void loop() {
  //setup timekeeping variable
  stime=millis();
  
  #ifdef ETIME_CHECK
  Serial.println(F("Starting PING loop"));
  #endif
  

  //Ping and store stats
  #ifdef ENABLE_LCD
  lcd.setCursor(0,1);
  lcd.cursor();
  //lcd.blink();
  #endif
  setBool( gatewayPings,    currentPingNum, !doPing(Ethernet.gatewayIP()) );
  
  #ifdef ENABLE_LCD
  lcd.setCursor(4,1);
  #endif
  setBool( firstAddrPings,  currentPingNum, !doPing(firstPingAddr)        );
  
  #ifdef ENABLE_LCD
  lcd.setCursor(8,1);
  #endif
  setBool( secondAddrPings, currentPingNum, !doPing(secondPingAddr)       );
  
  #ifdef ENABLE_LCD
  lcd.noCursor();
  //lcd.noBlink();
  #endif
  
  //increment for next loop
  currentPingNumInc();
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Pings done in: "));
  Serial.println(millis()-stime);
  #endif
  #endif
  
  //Compute averages
  gatewayAvgLoss=getAvgBool(gatewayPings);
  firstAddrLoss=getAvgBool(firstAddrPings);
  secondAddrLoss=getAvgBool(secondAddrPings);
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Stats computed in: "));
  Serial.println(millis()-stime); 
  #endif
  #endif
  
  
  
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
  #endif
  
  #ifdef ENABLE_LCD
  lcd.setCursor(0,0);
  lcd.print(F("            %   "));
  lcd.setCursor(0,1);
  //           100 100 100 100%
  //           1234567890123456
  lcd.print(F("GW  IP1 IP2 LOSS"));
  lcd.setCursor(0,0);
  lcd.print(gatewayAvgLoss);
  lcd.setCursor(4,0);
  lcd.print(firstAddrLoss);
  lcd.setCursor(8,0);
  lcd.print(secondAddrLoss);
  #endif
  
  #ifdef ETIME_CHECK
  #ifdef ENABLE_SERIAL
  Serial.print(F("Stats printed in: "));
  Serial.println(millis()-stime); 
  #endif
  #endif
  
  
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
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Renew Failure"));
      #endif
    }
    else if(ethernetStatus == 2)
    {
      #ifdef ENABLE_LCD
      lcd.print(F("    Renew OK    "));
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Renew Success"));
      #endif
    }
    else if(ethernetStatus == 3)
    {
      #ifdef ENABLE_LCD
      lcd.print(F(" <!> Rebind Fail"));
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.println(F("Ethernet Rebind Failure"));
      #endif
    }
    else if(ethernetStatus == 4)
    {
      #ifdef ENABLE_LCD
      lcd.print(F("   Rebind OK    "));
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
      #endif
      
      #ifdef ENABLE_SERIAL
      Serial.print(F("Ethernet Maintenance: Unknown code "));
      Serial.println(ethernetStatus);
      #endif
    }
  }


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
  }
  
}


//Returns TRUE if success
inline boolean doPing(IPAddress pingAddr)
{
  #ifdef ETIME_CHECK
  Serial.println(F("Starting doPing call"));
  long eTime=millis();
  #endif

  //Prepare to save the result
  ICMPEchoReply echoReply;
  
  #ifdef ICMPPING_ASYNCH_ENABLE
  /* Begin New code */ 
  
      //Send the ping
      if (! ping.asyncStart(pingAddr, 1, echoReply))
      {
        Serial.print("Couldn't even send our ping request?? Status: ");
        Serial.println((int)echoReply.status);
        //return false;
      }
    
      //Wait for the PING
      while (! ping.asyncComplete(echoReply))
      {
        //Hurry up and wait...nothing more to do
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

    #ifdef ETIME_CHECK
    Serial.print(F("About to return true: "));
    Serial.println(millis()-eTime);
    #endif
    
    return true;
  }
  else
  {
    #ifdef ENABLE_SERIAL
    sprintf(buffer, "Echo request failed; %d", echoReply.status);
    Serial.println(buffer);
    #endif

    #ifdef ETIME_CHECK
    Serial.print(F("About to return false: "));
    Serial.println(millis()-eTime);
    #endif

    return false;
  }
}


inline void setBool(byte data[], int pos, boolean value)
{
  int byte_pos=pos/8;
  int bit_pos=pos%8;
  
  if(value)
  {
    data[byte_pos]=data[byte_pos]|BIT_MASK[bit_pos];
  }
  else
  {
    data[byte_pos]=data[byte_pos]&(~BIT_MASK[bit_pos]);
  }
}


inline boolean getBool(byte data[], int pos)
{
  int byte_pos=pos/8;
  int bit_pos=pos%8;
  
  return data[byte_pos]&BIT_MASK[bit_pos];
}

// Averages bits startPos (inclusive) thru endPos (exclusive)
inline int getAvgBool(byte data[], int startPos, int endPos)
{
  int sum=0;
  for(int x=startPos; x < endPos; x++)
  {
    if(getBool(data, x))
    {
      sum++;
    }
  }
  return (sum * 100)/(endPos-startPos);
}

inline int getAvgBool(byte data[])
{
  if(firstRun)
  {
    return getAvgBool(data, 0, currentPingNum);
  }
  else
  {
    return getAvgBool(data, 0, PING_MAX_COUNT);
  }
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
