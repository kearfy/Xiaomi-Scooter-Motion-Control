#include <Arduino.h>
#include <arduino-timer.h>
#include <SoftwareSerial.h>

// WARNING: Always try to use the latest version available.
//          Check here: https://github.com/kearfy/Xiaomi-Scooter-Motion-Control

// +-============================================================================-+
// |-================================ SETTINGS ==================================-|
// +-============================================================================-+

//Defines how much km/u is detected as a kick.
const int speedBump = 3;

//Defines what the minimal increasement in km/u every kick should make.
const int minimumSpeedIncreasment = 7;

//Defines how much percent the speedBump should go down per km in speed. (it's harder to speed up when driving 20km/u.
const float lowerSpeedBump = 0.988;

//Recommended lowerSpeedBump values for different speedBumps.
// 1: 1.0;
// 2: 0.986;
// 3: 0.988;
// 4: 0.993;
// 5: 0.9935;

//Minimum and maximum speed of your scooter. (You can currently use this firmware in just one mode)
const int minimumSpeed = 7;
const int maximumSpeed = 27;

//Minimum speed before throtteling
const int startThrottle = 5;

//Amount of kicks it takes to switch over the INCREASING state.
const int kicksBeforeIncreasment = 2;

//Don't touch unless you know what you are doing.
const int breakTriggered = 47;

//Defines how long one kick should take.
const int drivingTime = 5000;

//Defines how much time the amount of kicks before increasment can take up.
const int kickResetTime = 1500;

//Defines the amount of time the INCREASING state will wait for a new kick.
const int increasmentTime = 2000;

//used to calculate the average speed.
const int historySize = 20;

// Arduino pin where throttle is connected to (only pin 9 & 10 is ok to use)
const int THROTTLE_PIN = 10;
int LED_PCB = 13;

//TX & RX pin
SoftwareSerial SoftSerial(2, 3); // RX, TX

//END OF SETTINGS

// +-============================================================================-+
// |-=============================== VARIABLES ==================================-|
// +-============================================================================-+
// |-========================== !!! DO NOT EDIT !!! =============================-|
// +-============================================================================-+

//All the different timers.
auto drivingTimer = timer_create_default();
auto kickResetTimer = timer_create_default();
auto increasmentTimer = timer_create_default();

int BrakeHandle;          //Status of the brake handle.
int Speed;                //current speed
int temporarySpeed = 0;   //Expected speed in INCREASINGSTATE.
int expectedSpeed = 0;    //Expected speed in DRIVINGSTATE.
int kickCount = 0;        //To track the amount of kicks.

int historyTotal = 0;     //Sum of the history.
int history[historySize]; //History of speeds.
int historyIndex = 0;     //Current index in the history.
int averageSpeed = 0;     //Average speed based on the history.

//Speedrange. Used for throttlewrite calculations.
const int speedRange = maximumSpeed - minimumSpeed;


//motionmodes
uint8_t State = 0;
#define READYSTATE 0
#define INCREASINGSTATE 1
#define DRIVINGSTATE 2
#define BREAKINGSTATE 3

void logByteInHex(uint8_t val) {
    //  if(val < 16)
    //    Serial.print('0');
    //
    //  Serial.print(val, 16);
    //  Serial.print(' ');
}

uint8_t readBlocking() {
    while (!SoftSerial.available()) {
        delay(1);
    }

    return SoftSerial.read();
}

void setup() {
    pinMode(LED_PCB, OUTPUT);

    // initialize all the readings to 0:
    for (int i = 0; i < historySize; i++) {
        history[i] = 0;
    }

    Serial.begin(115200);
    SoftSerial.begin(115200);

    Serial.println("Starting Logging data...");

    TCCR1B = TCCR1B & 0b11111001; //Set PWM of PIN 9 & 10 to 32 khz

    ThrottleWrite(45);
}

uint8_t buff[256];
void loop() {
    int w = 0;
    while (readBlocking() != 0x55);

    if (readBlocking() != 0xAA) {
        return;
    }

    uint8_t len = readBlocking();
    buff[0] = len;

    if (len > 254) {
        return;
    }

    uint8_t addr = readBlocking();
    buff[1] = addr;

    uint16_t sum = len + addr;
    for (int i = 0; i < len; i++) {
        uint8_t curr = readBlocking();
        buff[i + 2] = curr;
        sum += curr;
    }

    uint16_t checksum = (uint16_t) readBlocking() | ((uint16_t) readBlocking() << 8);
    if (checksum != (sum ^ 0xFFFF)) {
        return;
    }

    for (int i = 0; i < len + 2; i++) {
        logByteInHex(buff[i]);
    }

    //  Serial.print("check ");
    //  Serial.print(checksum, 16);
    //
    //  Serial.println();
    switch (buff[1]) {
        case 0x20:
            switch (buff[2]) {
                case 0x65:
                    BrakeHandle = buff[6];
            }
        case 0x21:
            switch (buff[2]) {
                case 0x64:
                    if (buff[8] != 0) {
                        Speed = buff[8];
                    }
            }
    }

    //Update the current index in the history and recalculate the average speed.

    historyTotal = historyTotal - history[historyIndex];
    history[historyIndex] = Speed;
    historyTotal = historyTotal + history[historyIndex];
    historyIndex = historyIndex + 1;

    if (historyIndex >= historySize) {
        historyIndex = 0;
    }

    //Recalculate the average speed.
    averageSpeed = historyTotal / historySize;

    //Trigger motion control, tick all timers.
    motion_control();
    drivingTimer.tick();
    kickResetTimer.tick();
    increasmentTimer.tick();
}

void motion_control() {
    if ((Speed != 0) && (Speed < startThrottle)) {
        // If speed is under 5 km/h, stop throttle
        ThrottleWrite(45); //  0% throttle
    }

    if (BrakeHandle > breakTriggered) {
        ThrottleWrite(45); //close throttle directly when break is touched. 0% throttle
        digitalWrite(LED_PCB, HIGH);

        //If the script isn't in the BREAKINGSTATE yet, switch over and cancel all timers.
        if (State != BREAKINGSTATE) {
            State = BREAKINGSTATE;
            drivingTimer.cancel();
            kickResetTimer.cancel();
            increasmentTimer.cancel();
            Serial.println("BREAKING ~> Handle pulled.");
        }
    } else {
        digitalWrite(LED_PCB, LOW);
    }

    Serial.println("SPEED");
    Serial.println(Speed);
    Serial.println("AVERAGE SPEED");
    Serial.println(Speed);

    if (State == INCREASINGSTATE) {
        Serial.println("TEMPORARY SPEED");
        Serial.println(temporarySpeed);
    } else {
        Serial.println("EXPECTED SPEED");
        Serial.println(expectedSpeed);
    }

    Serial.println(' ');
    Serial.println(' ');

    //Action based on the current state.
    switch(State) {
        case READYSTATE:
            //If the current speed exceeds the minimum speed required to throttle, start throttle.
            if (Speed > startThrottle) {
                //If vehicle is already moving faster than the minimum speed, Go above minimum speed.
                if (Speed > minimumSpeed) {
                    //If the average speed is higher than the actual speed the vehicle probably just recovered from a break. Go with the average speed.
                    if (averageSpeed > Speed) {
                        temporarySpeed = averageSpeed;
                    } else {
                        //Else, just go with the current speed.
                        temporarySpeed = Speed;
                    }
                } else {
                    temporarySpeed = minimumSpeed;
                }

                //Start throttle, move over to increasing state to give the driver room to speed up.
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> The speed has exceeded the minimum throttle speed.");
            }

            break;
        case INCREASINGSTATE:
            //If the Speed is lower than the expected temporary speed + calculated speedbump required to speed up, add kick and disable the increasementTimer.
            if (Speed >= temporarySpeed + calculateSpeedBump(temporarySpeed)) {
                kickCount++;
                increasmentTimer.cancel();
            } else if (averageSpeed >= temporarySpeed && kickCount < 1) {
                //If the speed has stabilized, start the increasementTimer to eventually switch over to DRIVINGSTATE.
                increasmentTimer.in(increasmentTime, endIncrease);
            }

            //If one or more kicks have been made, increase the (also the expected) temporary speed.
            if (kickCount >= 1) {
                //If the current speed exceeds the expected temporary speed AND the minimum speed increasment, go with the current speed.
                if (Speed > temporarySpeed + minimumSpeedIncreasment) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(temporarySpeed + minimumSpeedIncreasment);
                }

                //Start new throttle.
                ThrottleSpeed(temporarySpeed);
            }

            //Reset the kickcount every loop.
            kickCount = 0;
            break;
        case DRIVINGSTATE:
            //If the speed exceeds the expected speed + calculated speedbump, add kick and reset kickResetTimer.
            if (Speed >= expectedSpeed + calculateSpeedBump(expectedSpeed)) {
                kickCount++;
                kickResetTimer.cancel();
                kickResetTimer.in(kickResetTime, resetKicks);
            }

            //If kickCount exceeds or is equal to kicksBeforeIncreasement, increase speed and bump over to INCREASINGSTATE.
            if (kickCount >= kicksBeforeIncreasment) {
                //If the current speed exceeds the expected speed AND the minimum speed increasment, go with the current speed.
                if (Speed > expectedSpeed + minimumSpeedIncreasment) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(expectedSpeed + minimumSpeedIncreasment);
                }

                //Start new throttle, update State and cancel timers.
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                drivingTimer.cancel();
                kickResetTimer.cancel();
                Serial.println("INCREASING ~> The least amount of kicks before increasment has been reached.");
            }

            break;
        case BREAKINGSTATE:
            //If BrakeHandle is still being triggerd, move on.
            if (BrakeHandle > breakTriggered) break;

            //If speed has dropped, move back to READYSTATE.
            if (Speed < startThrottle) {
                State = READYSTATE;
                Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");

            //If speed has increased over the averageSpeed + calculated speedBump, start throttling again.
            } else if (Speed >= averageSpeed + calculateSpeedBump(averageSpeed)) {

                //Don't apply minimumSpeadIncreasment here. Handle was most likely pulled to lower the speed.
                //Instead, move over to INCREASINGSTATE to let the driver increase speed if wanted.
                temporarySpeed = ValidateSpeed(Speed);
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> Break released and speed increased.");
            }
            
            break;
        default:
            //Unknown state! Activate breaks and move over to BREAKINGSTATE to automatically resolve the issue.
            ThrottleWrite(45);
            digitalWrite(LED_PCB, HIGH);
            State = BREAKINGSTATE;
            drivingTimer.cancel();
            kickResetTimer.cancel();
            increasmentTimer.cancel();
            Serial.println("BREAKING ~> Unknown state detected");
    }

}

int calculateSpeedBump(int requestedSpeed) {
    float calculatedSpeedBump = (float) speedBump;
    for (int i = 0; i < requestedSpeed; i++) {
        calculatedSpeedBump = calculatedSpeedBump * (float) lowerSpeedBump;
    }
    
    return round(calculatedSpeedBump);
}

//Reset the kicks. Used as function for the resetKicksTimer.
int resetKicks() {
    kickCount = 0;
}

//Stop increasing speed of the vehicle, move over to DRIVINGSTATE.
//Used for the increasmentTimer.
int endIncrease() {
    expectedSpeed = temporarySpeed;
    kickCount = 0;
    State = DRIVINGSTATE;
    drivingTimer.in(drivingTime, endDrive);
    Serial.println("DRIVING ~> The speed has been stabilized.");
}

//Stop throttling, cancel timers and move over to READYSTATE.
//Used for the drivingTimer.
int endDrive() {
    drivingTimer.cancel();
    kickResetTimer.cancel();
    ThrottleWrite(45);
    State = READYSTATE;
    Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
}

//Validate that the speed is between the minimum and maximum speed.
int ValidateSpeed(int requestedSpeed) {
    if (requestedSpeed < minimumSpeed) {
        return minimumSpeed;
    } else if (requestedSpeed > maximumSpeed) {
        return maximumSpeed;
    } else {
        return requestedSpeed;
    }
}

//Calculate the throttle for the ThrottleWrite function. Based on the requestedSpeed, maximum speed and the throttleRange (maxSpeed - minSpeed).
int ThrottleSpeed(int requestedSpeed) {
    if (requestedSpeed == 0) {
        ThrottleWrite(45);
    } else if (requestedSpeed == maximumSpeed) {
        ThrottleWrite(233);
    } else {
        int throttleRange = 233 - 45;
        float calculatedThrottle = (requestedSpeed + 4) / (float) maximumSpeed * throttleRange;
        ThrottleWrite((int) calculatedThrottle);
    }
}

//Instruct the vehicle at which speed it should drive.
int ThrottleWrite(int value) {
    if (value != 0) {
        analogWrite(THROTTLE_PIN, value);
    } else {
        analogWrite(THROTTLE_PIN, 45);
    }
}
