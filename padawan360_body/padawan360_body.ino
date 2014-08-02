// =======================================================================================
// /////////////////////////Padawan360 Body Code v1.0 ////////////////////////////////////
// =======================================================================================
/*
by Dan Kraus
dskraus@gmail.com
Astromech: danomite4047

Heavily influenced by DanF's Padwan code which was built for Arduino+Wireless PS2
controller leveraging Bill Porter's PS2X Library. I was running into frequent disconnect
issues with 4 different controllers working in various capacities or not at all. I decided
that PS2 Controllers were going to be more difficult to come by every day, so I explored
some existing libraries out there to leverage and came across the USB Host Shield and it's
support for PS3 and Xbox 360 controllers. Bluetooth dongles were inconsistent as well
so I wanted to be able to have something with parts that other builder's could easily track
down and buy parts even at your local big box store.

Hardware:
Arduino UNO
USB Host Shield from circuits@home
Microsoft Xbox 360 Controller
Xbox 360 USB Wireless Reciver
Sabertooth Motor Controller
Syren Motor Controller
Sparkfun MP3 Trigger

This sketch supports I2C and calls events on many sound effect actions to control lights and sounds.
It is NOT set up for Dan's method of using the serial packet to transfer data up to the dome
to trigger some light effects. If you want that, you'll need to reference DanF's original
Padawan code.

Set Sabertooth 2x25/2x12 Dip Switches 1 and 2 Down, All Others Up
For SyRen Simple Serial Set Switches 1 and 2 Down, All Others Up
For SyRen Simple Serial Set Switchs 2 & 4 Down, All Others Up
Placed a 10K ohm resistor between S1 & GND on the SyRen 10 itself

*/

//************************** Set speed and turn speeds here************************************//

//set these 3 to whatever speeds work for you. 0-stop, 127-full speed.
byte drivespeed1 = 50;
//Recommend beginner: 50 to 75, experienced: 100 to 127, I like 100.
byte drivespeed2 = 100;
//Set to 0 if you only want 2 speeds.
byte drivespeed3 = 127;

byte drivespeed = drivespeed1;

// the higher this number the faster the droid will spin in place, lower - easier to control.
// Recommend beginner: 40 to 50, experienced: 50 $ up, I like 70
byte turnspeed = 70;
// If using a speed controller for the dome, sets the top speed. You'll want to vary it potenitally
// depending on your motor. My Pittman is really fast so I dial this down a ways from top speed.
// Use a number up to 127 for serial
byte domespeed = 80;

// Ramping- the lower this number the longer R2 will take to speedup or slow down,
// change this by incriments of 1
byte ramping = 5;

// Compensation is for deadband/deadzone checking. There's a little play in the neutral zone
// which gets a reading of a value of something other than 0 when you're not moving the stick.
// It may vary a bit across controllers and how broken in they are, sometimex 360 controllers
// develop a little bit of play in the stick at the center position. You can do this with the
// direct method calls against the Syren/Sabertooth library itself but it's not supported in all
// serial modes so just manage and check it in software here
// use the lowest number with no drift
// domeDeadZoneRange for the left stick, driveDeadZoneRange for the left stick
byte domeDeadZoneRange = 20;
byte driveDeadZoneRange = 20;

// Set the baude rate for the Syren motor controller
// for packetized options are: 2400, 9600, 19200 and 38400. I think you need to pick one that works
// and I think it varies across different firmware versions.
// for simple serial use 9600
int domeBaudeRate = 2400;

// Comment the SYRENSIMPLE out for packetized serial connection to Syren - Recomended.
// I've never tested Syrene Simple, it's a carry-over from DanF's library.
// Un-comment for simple serial - do not use in close contact with people
//#define SYRENSIMPLE

// I have a pin set to pull a relay high/low to trigger my upside down compressed air like R2's extinguisher
#define EXTINGUISHERPIN 3

#include <Sabertooth.h>
#include <SyRenSimplified.h>
#include <Servo.h>
#include <MP3Trigger.h>
#include <Wire.h>
#include <XBOXRECV.h>

#include <SoftwareSerial.h>
// These are the pins for the Sabertooth and Syren
SoftwareSerial STSerial(NOT_A_PIN, 4);
SoftwareSerial SyRSerial(2, 5);

/////////////////////////////////////////////////////////////////
Sabertooth ST(128, STSerial);
#if defined(SYRENSIMPLE)
SyRenSimplified SyR(SyRSerial); // Use SWSerial as the serial port.
#else
Sabertooth SyR(128, SyRSerial);
#endif

// Satisfy IDE, which only needs to see the include statment in the ino.
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif

// Set some defaults for start up
// 0 = full volume, 255 off
byte vol = 20;
// 0 = drive motors off ( right stick disabled ) at start
byte isDriveEnabled = 0;

// Automated function variables
// Used as a boolean to turn on/off automated functions like periodic random sounds and periodic dome turns
byte isInAutomationMode = 0;
unsigned long automateMillis = 0;
byte automateDelay = random(5,20);// set this to min and max seconds between sounds
//How much the dome may turn during automation.
int turnDirection = 20;
// Action number used to randomly choose a sound effect or a dome turn
byte automateAction = 0;
unsigned long DriveMillis = 0;
int driveThrottle = 0;
int sticknum = 0;
int domeThrottle = 0;
int turnThrottle = 0;

int firstLoadOnConnect = 0;

// this is legacy right now. The rest of the sketch isn't set to send any of this
// data to another arduino like the original Padawan sketch does
// right now just using it to track whether or not the HP light is on so we can
// fire the correct I2C event to turn on/off the HP light.
struct SEND_DATA_STRUCTURE{
  //put your variable definitions here for the data you want to send
  //THIS MUST BE EXACTLY THE SAME ON THE OTHER ARDUINO
  int hpx; // hp movement
  int hpy; // hp movement
  int hpl; // hp light
  int hpa; // hp automation
  int dsp; // 0 = random, 1 = alarm, 5 = leia, 11 = alarm2, 100 = no change
};

SEND_DATA_STRUCTURE domeData;//give a name to the group of data

MP3Trigger mp3Trigger;
USB Usb;
XBOXRECV Xbox(&Usb);

void setup(){
  SyRSerial.begin(domeBaudeRate);
  #if defined(SYRENSIMPLE)
    SyR.motor(0);
  #else
    SyR.autobaud();
  #endif

  // 9600 is the default baud rate for Sabertooth packet serial.
  STSerial.begin(9600);
  // Send the autobaud command to the Sabertooth controller(s).
  ST.autobaud();
  /* NOTE: *Not all* Sabertooth controllers need this command.
  It doesn't hurt anything, but V2 controllers use an
  EEPROM setting (changeable with the function setBaudRate) to set
  the baud rate instead of detecting with autobaud.
  If you have a 2x12, 2x25 V2, 2x60 or SyRen 50, you can remove
  the autobaud line and save yourself two seconds of startup delay.
  */

  ST.setTimeout(950);
  #if !defined(SYRENSIMPLE)
    SyR.setTimeout(950);
  #endif

  #if !defined(SYRENSIMPLE)
  SyR.setTimeout(950);
  #endif

  // The Sabertooth won't act on mixed mode packet serial commands until
  // it has received power levels for BOTH throttle and turning, since it
  // mixes the two together to get diff-drive power levels for both motors.
  ST.drive(0);
  ST.turn(0);

  pinMode(EXTINGUISHERPIN, OUTPUT);
  digitalWrite(EXTINGUISHERPIN, HIGH);

  mp3Trigger.setup();
  mp3Trigger.setVolume(vol);


  // Start I2C Bus. The body is the master.
  Wire.begin();

   //Serial.begin(115200);
  // Wait for serial port to connect - used on Leonardo, Teensy and other boards with built-in USB CDC serial connection
  while (!Serial);
    if (Usb.Init() == -1) {
      //Serial.print(F("\r\nOSC did not start"));
      while (1); //halt
    }
  //Serial.print(F("\r\nXbox Wireless Receiver Library Started"));
}


void loop(){
  Usb.Task();
  // if we're not connected, return so we don't bother doing anything else.
  // set all movement to 0 so if we lose connection we don't have a runaway droid!
  // a restraining bolt and jawa droid caller won't save us here!
  if(!Xbox.XboxReceiverConnected || !Xbox.Xbox360Connected[0]){
    ST.drive(0);
    ST.turn(0);
    SyR.motor(1,0);
    firstLoadOnConnect = 0;
    return;
  }

  // After the controller connects, Blink all the LEDs so we know drives are disengaged at start
  if(firstLoadOnConnect == 0){
    firstLoadOnConnect = 1;
    mp3Trigger.play(21);
    Xbox.setLedMode(ROTATING, 0);
  }

  // enable / disable right stick (droid movement) & play a sound to signal motor state
  if(Xbox.getButtonClick(START, 0)) {
    if (isDriveEnabled < 1){
      isDriveEnabled = 1;
      mp3Trigger.play(52);
      // //When the drive is enabled, set our LED accordingly to indicate speed
      if(drivespeed == drivespeed1){
        Xbox.setLedOn(LED1, 0);
      } else if(drivespeed == drivespeed2 && (drivespeed3!=0)){
        Xbox.setLedOn(LED2, 0);
      } else {
        Xbox.setLedOn(LED3, 0);
      }
    } else {
      isDriveEnabled = 0;
      Xbox.setLedMode(ROTATING, 0);
      mp3Trigger.play(53);
    }
  }

  //Toggle automation mode with the BACK button
  if(Xbox.getButtonClick(BACK, 0)) {
    if (isInAutomationMode < 1){
      isInAutomationMode = 1;
      mp3Trigger.play(52);
    } else {
      isInAutomationMode = 0;
      automateAction = 0;
      mp3Trigger.play(53);
    }
  }

  // Plays random sounds or dome movements for automations when in automation mode
  if(isInAutomationMode == 1){
    unsigned long currentMillis = millis();

    if(currentMillis - automateMillis > (automateDelay*1000)){
      automateMillis = millis();
      automateAction = random(1,5);

      if(automateAction > 1){
        mp3Trigger.play(random(32,52));
      }
      if(automateAction < 4){
        #if defined(SYRENSIMPLE)
          SyR.motor(turnDirection);
        #else
          SyR.motor(1,turnDirection);
        #endif

        delay(750);

        #if defined(SYRENSIMPLE)
          SyR.motor(0);
        #else
          SyR.motor(1,0);
        #endif

        if(turnDirection > 0){
          turnDirection = -45;
        } else {
          turnDirection = 45;
        }
      }

      // sets the mix, max seconds between automation actions - sounds and dome movement
      automateDelay = random(5,20);
    }
  }

  // Volume Control of MP3 Trigger
  // Hold R1 and Press Up/down on D-pad to increase/decrease volume
  if(Xbox.getButtonClick(UP, 0)){
    // volume up
    if(Xbox.getButtonPress(R1, 0)){
      if (vol > 0){
        vol--;
        mp3Trigger.setVolume(vol);
      }
    }
  }
  if(Xbox.getButtonClick(DOWN, 0)){
    //volume down
    if(Xbox.getButtonPress(R1, 0)){
      if (vol < 255){
        vol++;
        mp3Trigger.setVolume(vol);
      }
    }
  }

  // Logic display brightness.
  // Hold L1 and press up/down on dpad to increase/decrease brightness
  if(Xbox.getButtonClick(UP, 0)){
    if(Xbox.getButtonPress(L1, 0)){
      triggerI2C(10, 24);
    }
  }
  if(Xbox.getButtonClick(DOWN, 0)){
    if(Xbox.getButtonPress(L1, 0)){
      triggerI2C(10, 25);
    }
  }


  //FIRE EXTINGUISHER
  // When holding L2-UP, extinguisher is spraying. WHen released, stop spraying

  // TODO: ADD SERVO DOOR OPEN FIRST. ONLY ALLOW EXTINGUISHER ONCE IT'S SET TO 'OPENED'
  // THEN CLOSE THE SERVO DOOR
  if(Xbox.getButtonPress(L1, 0)){
    if(Xbox.getButtonPress(UP, 0)){
      digitalWrite(EXTINGUISHERPIN, LOW);
    } else {
      digitalWrite(EXTINGUISHERPIN, HIGH);
    }
  }


  // GENERAL SOUND PLAYBACK AND DISPLAY CHANGING

  // Y Button and Y combo buttons
  if(Xbox.getButtonClick(Y, 0)){
    if(Xbox.getButtonPress(L1, 0)){
      mp3Trigger.play(8);
      //logic lights, random
      triggerI2C(10, 0);
    } else if(Xbox.getButtonPress(L2, 0)){
      mp3Trigger.play(2);
      //logic lights, random
      triggerI2C(10, 0);
    } else if(Xbox.getButtonPress(R1, 0)){
      mp3Trigger.play(9);
      //logic lights, random
      triggerI2C(10, 0);
    } else {
      mp3Trigger.play(random(13,17));
      //logic lights, random
      triggerI2C(10, 0);
    }
  }

  // A Button and A combo Buttons
  if(Xbox.getButtonClick(A, 0)){
    if(Xbox.getButtonPress(L1, 0)){
      mp3Trigger.play(6);
      //logic lights
      triggerI2C(10, 6);
      // HPEvent 11 - SystemFailure - I2C
      triggerI2C(25, 11);
      triggerI2C(26, 11);
      triggerI2C(27, 11);
    } else if(Xbox.getButtonPress(L2, 0)){
      mp3Trigger.play(1);
      //logic lights, alarm
      triggerI2C(10, 1);
      //  HPEvent 3 - alarm - I2C
      triggerI2C(25, 3);
      triggerI2C(26, 3);
      triggerI2C(27, 3);
    } else if(Xbox.getButtonPress(R1, 0)){
      mp3Trigger.play(11);
      //logic lights, alarm2Display
      triggerI2C(10, 11);
    } else {
      mp3Trigger.play(random(17,25));
      //logic lights, random
      triggerI2C(10, 0);
    }
  }

  // B Button and B combo Buttons
  if(Xbox.getButtonClick(B, 0)){
    if(Xbox.getButtonPress(L1, 0)){
      mp3Trigger.play(7);
      //logic lights, random
      triggerI2C(10, 0);
    } else if(Xbox.getButtonPress(L2, 0)){
      mp3Trigger.play(3);
      //logic lights, random
      triggerI2C(10, 0);
    } else if(Xbox.getButtonPress(R1, 0)){
      mp3Trigger.play(10);
      //logic lights, random
      triggerI2C(10, 0);
      // HPEvent 1 - Disco - I2C
      triggerI2C(25, 10);
      triggerI2C(26, 10);
      triggerI2C(27, 10);
    } else {
      mp3Trigger.play(random(32,52));
      //logic lights, random
      triggerI2C(10, 0);
    }
  }

  // X Button and X combo Buttons
  if(Xbox.getButtonClick(X, 0)){
    // leia message L1+X
    if(Xbox.getButtonPress(L1, 0)){
      mp3Trigger.play(5);
      //logic lights, leia message
      triggerI2C(10, 5);
      // Front HPEvent 1 - HoloMessage - I2C -leia message
      triggerI2C(25, 9);
    } else if(Xbox.getButtonPress(L2, 0)){
      mp3Trigger.play(4);
      //logic lights
      triggerI2C(10, 4);
    } else if(Xbox.getButtonPress(R1, 0)){
      mp3Trigger.play(12);
      //logic lights, random
      triggerI2C(10, 0);
    } else {
      mp3Trigger.play(random(25,32));
      //logic lights, random
      triggerI2C(10, 0);
    }
  }

  // turn hp light on & off with Left Analog Stick Press (L3)
  if(Xbox.getButtonClick(L3, 0))  {
    // if hp light is on, turn it off
    if(domeData.hpl == 1){
      domeData.hpl = 0;
      // turn hp light off
      // Front HPEvent 2 - ledOFF - I2C
      triggerI2C(25, 2);
    } else {
      domeData.hpl = 1;
      // turn hp light on
      // Front HPEvent 4 - whiteOn - I2C
      triggerI2C(25, 1);
    }
  }


  // Change drivespeed if drive is eabled
  // Press Right Analog Stick (R3)
  // Set LEDs for speed - 1 LED, Low. 2 LED - Med. 3 LED High
  if(Xbox.getButtonClick(R3, 0) && isDriveEnabled == 1) {
    //if in lowest speed
    if(drivespeed == drivespeed1){
      //change to medium speed and play sound 3-tone
      drivespeed = drivespeed2;
      Xbox.setLedOn(LED2, 0);
      mp3Trigger.play(53);
      triggerI2C(10, 22);
    } else if(drivespeed == drivespeed2 && (drivespeed3!=0)){
      //change to high speed and play sound scream
      drivespeed = drivespeed3;
      Xbox.setLedOn(LED3, 0);
      mp3Trigger.play(1);
      triggerI2C(10, 23);
    } else {
      //we must be in high speed
      //change to low speed and play sound 2-tone
      drivespeed = drivespeed1;
      Xbox.setLedOn(LED1, 0);
      mp3Trigger.play(52);
      triggerI2C(10, 21);
    }
  }


  // FOOT DRIVES
  // Xbox 360 analog stick values are signed 16 bit integer value
  // Sabertooth runs at 8 bit signed. -127 to 127 for speed (full speed reverse and  full speed forward)
  // Map the 360 stick values to our min/max current drive speed
  sticknum = (map(Xbox.getAnalogHat(RightHatY, 0), -32768, 32767, -drivespeed, drivespeed));
  if(sticknum > -driveDeadZoneRange && sticknum < driveDeadZoneRange){
    // stick is in dead zone - don't drive
    driveThrottle = 0;
  } else {
    if(driveThrottle < sticknum){
      if(sticknum - driveThrottle < (ramping+1) ){
        driveThrottle+=ramping;
      } else {
        driveThrottle = sticknum;
      }
    } else if(driveThrottle > sticknum){
      if(driveThrottle - sticknum < (ramping+1) ){
        driveThrottle-=ramping;
      } else {
        driveThrottle = sticknum;
      }
    }
  }

  turnThrottle = map(Xbox.getAnalogHat(RightHatX, 0), -32768, 32767, 0, 255);
  if(turnThrottle <= 200 && turnThrottle >= 54)
    turnThrottle = map(turnThrottle, 54, 200, -(turnspeed/3), (turnspeed/3));
  else if(turnThrottle > 200)
    turnThrottle = map(turnThrottle, 201, 255, turnspeed/3, turnspeed);
  else if(turnThrottle < 54)
    turnThrottle = map(turnThrottle, 0, 53, -turnspeed, -(turnspeed/3));



  // DRIVE!
  // right stick (drive)
  if (isDriveEnabled == 1){
    // Only do deadzone check for turning here. Our Drive throttle speed has some math applied
    // for ramping and stuff, so just keep it separate here
    if(turnThrottle > -driveDeadZoneRange && turnThrottle < driveDeadZoneRange){
      // stick is in dead zone - don't turn
      turnThrottle = 0;
    }
    ST.turn(-turnThrottle);
    ST.drive(driveThrottle);
  }

  // DOME DRIVE!
  domeThrottle = (map(Xbox.getAnalogHat(LeftHatX, 0), -32768, 32767, -domespeed, domespeed));
  if (domeThrottle > -domeDeadZoneRange && domeThrottle < domeDeadZoneRange){
    //stick in dead zone - don't spin dome
    domeThrottle = 0;
  }

  //domeThrottle = 0;

  #if defined(SYRENSIMPLE)
    SyR.motor(domeThrottle);
  #else
    SyR.motor(1,domeThrottle);
  #endif
} // END loop()

void triggerI2C(int deviceID, int eventID){
  Wire.beginTransmission(deviceID);
  Wire.write(eventID);
  Wire.endTransmission();
}
