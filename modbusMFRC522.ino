#include <SPI.h>
#include <MFRC522.h>
#include <SimpleModbusSlave.h>

/*Daniel Perron 30 Sept 2018
 * Version 1.1
 * modified with MFRC522 library
 * modbus register 5 hold the number of serial ID byte
 * modbus register 6..10  contains the serial ID
 * modbus register 1..3 still hold the four bytes serial ID + parity 
 *                      for back compatibility purpose
 * status word, register 0, bit 15 is set to 1  for this version
 * modbus register 15  display software  version = 0x0101 => 1.1
 */



/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/


#define SS_PIN 10
#define RST_PIN A1

MFRC522 rfid(SS_PIN,RST_PIN);
MFRC522::MIFARE_Key key;

// set version to 1.0  MAJOR.MINOR
#define VERSION 0x0101

#define SLAVE_ID 1

#define TX_ENABLE_PIN A0

void(* resetFunct) (void) = 0; //declare reset function @ address 0

/*   Arduino MINI PIN

   2 - INPUT      reed swicth door close detection
   3 - INPUT      door bell switch
   4 - INPUT      damper 
   5 - OUTPUT     relay   unlock activation
   6 - OUTPUT     relay   illumination light
  
  // led indicator 
   7 - OUTPUT     led yellow   Card read and waiting for reply
   8 - OUTPUT     led green    Card Accpeted
   9 - OUTPUT     led red      Card refused
  
  10 -  SS_PIN   Select PIN
  11 -  SPI MOSI
  12 -  SPI MISO
  13 -  SPI SCK
  
 // ANALOG PIN  unused (could be digital).
  A0 - OUTPUT     TX_ENABLE PIN 
  A1 - OUTPUT     RST_PIN
  A2 -
     A4 -
  A3 -
     A5 -
  
  
  A6 -
  A7 - 




*/



unsigned long currentTime;
unsigned char gotNewCard;
unsigned char gotCard;
unsigned char rfidEnable;


int serNum[11];
unsigned char serSize;

#define CMD_REFUSE   1
#define CMD_ACCEPT   2
#define CMD_DAY_MODE 4
#define CMD_NIGHT_MODE 8
#define CMD_LAMP_OFF  16
#define CMD_LAMP_ON   32
#define CMD_DISABLE   64
#define CMD_ENABLE    128
#define CMD_ACK_BELL  256
#define CMD_ACK_DAMPER 512
#define CMD_RELOAD    1024
#define CMD_RESET     0xF000

#define RESET_MAGIC   0xA000

#define STATUS_NEW_CARD  1
#define STATUS_CARD_IN   2
#define STATUS_DAMPER_SW 4
#define STATUS_REED_SW   8
#define STATUS_BELL_SW   16
#define STATUS_LAMP_RELAY   32
#define STATUS_UNLOCK_RELAY 64
#define STATUS_ENABLE       128
#define STATUS_BELL_LATCH   256
#define STATUS_DAMPER_LATCH 512



uint8_t BellFlag;
uint8_t DamperFlag;

#define HOLDING_REGS_SIZE  16

#define IN_REED_SW 2
#define IN_BELL_SW 3
#define IN_DAMPER_SW  4
#define OUT_UNLOCK 5
#define OUT_LIGHT  6

// led indicator 
#define OUT_LED_BLUE 7
#define OUT_LED_GREEN  8
#define OUT_LED_RED    9

#define unlockDoor() digitalWrite(OUT_UNLOCK,HIGH)
#define lockDoor()   digitalWrite(OUT_UNLOCK,LOW)



// HOLDING REGISTER
/* WORD 0   STATUS  
    bit0 : Got A new valid Card
    bit1 : Valid card in te reader
    bit2 : Door Status
    bit3 : bell switch status
    bit4 : relay Light Status
    bit5 : relay Door status
    bit7 : Reader Enable
    bit8..14: 0
    bit 15: specify Version with 4,7 and 10 BYTES serial ID
*/  

// WORD 1..3  RFID BYTE INFO (4 bytes ONLY) (keep for back compatibility)
/* WORD 4 server control byte  
    bit 0 : Refuse Card (bit 0 has highest priority)
    bit 1 : Accept Card
    bit 2 : SET DAY MODE
    bit 3 : SET NIGHT MODE
    bit 4 : Turn LIGHT OFF
    bit 5 : Turn LIGHT ON
    bit 6 : Disable Reader
    bit 7 : Enable  Reader
    bit 8.. 15 not used
 */
// WORD 5  CARD SIZE
// WORD 6..10 RFID CARD INFO FOR (4,7 and 10 BYTES ID)
// WORD 12..14  FUTUR. USER RAM FOR NOW 
// WORD 15    VERSION NUMBER


union wordByte{
 uint8_t  Byte[32];
 uint16_t Word[16];
};


union wordByte holdingRegs;

// light and unlock temporisation
unsigned char lightDelay;
unsigned char unlockDelay;
unsigned char ledDelay;
unsigned char forceLightON;

unsigned char nightMode;
unsigned long timemilli;

#define DEFAULT_LIGHT_DELAY 30
#define DEFAULT_UNLOCK_DELAY 5
#define DEFAULT_LED_DELAY     2
#define DEFAULT_LEDBLUE_DELAY  30


#define LED_NONE    0
#define LED_GREEN   1
#define LED_RED     2
#define LED_BLUE    4
#define LED_ALL     7




void lightON(void)
{
  lightDelay=0;
  if(nightMode)
   {
    digitalWrite(OUT_LIGHT,HIGH);
    if(forceLightON==0)
       lightDelay=DEFAULT_LIGHT_DELAY;
   }
   
}
 
 
void lightOFF(void)
{
  lightDelay=0;
  digitalWrite(OUT_LIGHT,LOW);
}


void setLed(uint8_t Led)
{
  ledDelay=0;
  
 if(Led & LED_GREEN)
   {
    digitalWrite(OUT_LED_GREEN, LOW);
    ledDelay= DEFAULT_LED_DELAY;
   }
   else  
    digitalWrite(OUT_LED_GREEN, HIGH);

 if(Led & LED_RED)
   {
    digitalWrite(OUT_LED_RED, LOW);
    ledDelay= DEFAULT_LED_DELAY;
   }
   else  
    digitalWrite(OUT_LED_RED, HIGH);

 if(Led & LED_BLUE)
   {
    digitalWrite(OUT_LED_BLUE, LOW);
    ledDelay= DEFAULT_LEDBLUE_DELAY;
   }
   else  
    digitalWrite(OUT_LED_BLUE, HIGH);
  
}


void updateSecondTimer(void)
{
  
  unsigned long _temp = millis();
  if((_temp - timemilli) < 1000) return;

  timemilli = _temp;

// external light
  
  if(nightMode)
    {
      if(forceLightON)
        lightDelay=0;
      else
      {
        if(lightDelay>0)
         {
            lightDelay--;
            if(lightDelay==0)
              lightOFF();
         } 
      }
    }
   else
    {
      forceLightON=false;
      if(digitalRead(OUT_LIGHT)==HIGH)
         lightOFF();    
      lightDelay=0;
    
    }  
    
// LOCK

  if(unlockDelay > 0)
    {
      unlockDelay--;       
    }

  if(unlockDelay == 0)
    lockDoor();  
// LED 

   if(ledDelay >0)
     {
      ledDelay--;
       if(ledDelay==0)
       {
        setLed(LED_NONE);       
       } 
     }
  
  
}



void setup(){
int loop;
    for(loop=0;loop<HOLDING_REGS_SIZE;loop++)
      holdingRegs.Word[loop]=0;
    holdingRegs.Word[15]=VERSION;
// SET INPUT reed and door bell buttons

   pinMode(IN_REED_SW,INPUT_PULLUP);
   pinMode(IN_BELL_SW,INPUT_PULLUP);
   
// SET OUTPUT MODE;
   pinMode(OUT_UNLOCK,OUTPUT);
   digitalWrite(OUT_UNLOCK,LOW);

   pinMode(OUT_LIGHT,OUTPUT);
   digitalWrite(OUT_LIGHT,LOW);

// LED Will be all on for 1 second
   pinMode(OUT_LED_BLUE,OUTPUT);
   pinMode(OUT_LED_GREEN,OUTPUT);
   pinMode(OUT_LED_RED,OUTPUT);
   setLed(LED_ALL);
   
   currentTime=millis();
   
   modbus_configure(&Serial, 9600, SERIAL_8N1, SLAVE_ID, TX_ENABLE_PIN, HOLDING_REGS_SIZE, holdingRegs.Word); 
   SPI.begin();
   rfid.PCD_Init();
   for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
   }

   // wait 1 second
   delay(1000);
   
   // Turn all led OFf
   setLed(LED_NONE);

   lightDelay=0;
   unlockDelay=0;
   forceLightON=0;
   gotCard=0;
   gotNewCard=0;
   rfidEnable=1;
   BellFlag=0;
   DamperFlag=0;
}


void writeStatus(void)
{
 uint16_t _Status=0x8000; // specify 4,7, and 10 BYTES SERIAL ID
 
 if(gotNewCard >0)
      _Status |= STATUS_NEW_CARD;
      
 if(gotCard > 0)
      _Status |= STATUS_CARD_IN;

 if(digitalRead(IN_DAMPER_SW) ==  LOW)
      _Status |= STATUS_DAMPER_SW;
      
 if(digitalRead(IN_REED_SW) == LOW)
    {
      _Status |= STATUS_REED_SW;
      // ok door open! Stop the unlock
      unlockDelay=0;
      digitalWrite(OUT_UNLOCK,LOW);
    }
    
 if(digitalRead(IN_BELL_SW) == LOW )
      _Status |= STATUS_BELL_SW;
   
 if(digitalRead(OUT_LIGHT) == HIGH)
      _Status |= STATUS_LAMP_RELAY;

 if(digitalRead(OUT_UNLOCK) == HIGH)
      _Status |= STATUS_UNLOCK_RELAY;

  if(rfidEnable)
      _Status |= STATUS_ENABLE;
      
  if(BellFlag )
     _Status |= STATUS_BELL_LATCH;
     
  if(DamperFlag)
     _Status |= STATUS_DAMPER_LATCH;

 holdingRegs.Word[0]=_Status;

    
}



void readCommand(void)
{
  uint16_t Command;
  
  if(holdingRegs.Word[4]==0) return;

    // let's check holdingRegs.WORD[7]
    
    // OK do we have an accept
    // This mean open the door no matter what
    Command =  holdingRegs.Word[4];
    
    
    // Check For Reset
    
    if((Command & CMD_RESET) == RESET_MAGIC)
      resetFunct();
    

    // De we need to reload card
    
    if(Command & CMD_RELOAD)
      {
        gotNewCard=0;
        gotCard=0;
      }

     // card refused
    if(Command & CMD_REFUSE)
     {
        setLed(LED_RED);
        gotNewCard=0;
        unlockDelay=0;
     }
    // card accepted
    else
    if(Command & CMD_ACCEPT)
      {
       setLed(LED_GREEN);
       gotNewCard=0;
       unlockDelay=DEFAULT_UNLOCK_DELAY;
       unlockDoor();     
      }

    if(Command & CMD_DAY_MODE)
     {
      lightOFF();
      forceLightON=0;
      nightMode=0;
     } 

    if(Command & CMD_NIGHT_MODE)
     {
       nightMode=1;
     }

    if(Command & CMD_LAMP_OFF)
     {
        lightOFF();
     }
     
    if(Command & CMD_LAMP_ON)
     {
       lightON();
     }

    if(Command & CMD_DISABLE)
      rfidEnable=0;
    else
     if(Command & CMD_ENABLE)
       rfidEnable=1;
    
    // Is somebody press the bell
    if(Command & CMD_ACK_BELL)
        BellFlag=0;
    
    if(Command & CMD_ACK_DAMPER)
        DamperFlag=0;

    holdingRegs.Word[4]=0;
}    






void loop(){

  
  // check Bell
  
  if(digitalRead(IN_BELL_SW) == LOW)
   {
     BellFlag=1;
     // if bell door turn light
     lightON(); 
   }

  if(digitalRead(IN_DAMPER_SW) == LOW)
     DamperFlag=1;

  
  
  updateSecondTimer();
  modbus_update();
 
  readCommand();
    
  if(rfidEnable)    
    if(rfid.PICC_IsNewCardPresent()){
        currentTime=millis();
        if(rfid.PICC_ReadCardSerial()){
          if(!gotNewCard)
          if(!gotCard)
          {
            lightON();
            holdingRegs.Word[5]=rfid.uid.size;

            for (byte i=0 ;i<10;i++)
              if(i>rfid.uid.size)
                holdingRegs.Byte[12+i]=0;
               else
                holdingRegs.Byte[12+i]=rfid.uid.uidByte[i];
            // set register WORD 1,2,3 for back compatibility
            unsigned char parity=0;
            for (byte i=0 ;i<4;i++)
              {
               holdingRegs.Byte[2+i]= rfid.uid.uidByte[i];
               parity ^= rfid.uid.uidByte[i];
              }
            // add fitfh byte (PARITY BYTE) for compatibility
            holdingRegs.Byte[6]=parity;
            holdingRegs.Byte[7]= 0;
            setLed(LED_BLUE);
            gotNewCard=1;
            gotCard=1;
        }
      }
    }
   
     if(gotNewCard)
       currentTime=millis();
    
     if((millis()- currentTime) > 200)
        gotCard=0; 
        
   writeStatus();     
        
   rfid.PICC_HaltA();
   rfid.PCD_StopCrypto1();
}
