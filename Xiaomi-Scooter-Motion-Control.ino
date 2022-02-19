#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"
#include <PID_v1.h>

// +-============================================================================-+
// |-============================ SYSTEM VARIABLES ==============================-|
// +-============================================================================-+

unsigned long currentTime, drivingTimer, kickResetTimer, kickDelayTimer, increasmentTimer = 0;
double targetSpeed, speed, currentThrottle, brakeHandle;
int temporarySpeed, expectedSpeed, kickCount = 0;
bool kickAllowed = true;

int historyTotal = 0;
int history[historySize];
int historyIndex = 0;
int averageSpeed = 0;

//motionmodes
uint8_t State = 0;
#define READYSTATE 0
#define INCREASINGSTATE 1
#define DRIVINGSTATE 2
#define BREAKINGSTATE 3
#define DRIVEOUTSTATE 4

PID speedController(&speed, &currentThrottle, &targetSpeed, kpHigh, kiHigh, kdHigh, DIRECT);
SoftwareSerial SoftSerial(SERIAL_READ_PIN, 3);

uint8_t readBlocking() {
    while (!SoftSerial.available()) delay(1);
    return SoftSerial.read();
}

void setup() {
    pinMode(LED_PCB, OUTPUT);

    // initialize all the readings to 0:
    for (int i = 0; i < historySize; i++) history[i] = 0;

    Serial.begin(115200);
    SoftSerial.begin(115200);
    Serial.println("SYSTEM ~> Logs are now available.");

    TCCR1B = TCCR1B & 0b11111001; //Set PWM of PIN 9 & 10 to 32 khz
    throttleWrite(45);
    speedController.SetOutputLimits(45, 233);
    speedController.SetSampleTime(PIDSampleTimeHigh);
}

uint8_t buff[256];
void loop() {
    while (readBlocking() != 0x55);
    if (readBlocking() != 0xAA) return;
    uint8_t len = readBlocking();
    buff[0] = len;
    if (len > 254) return;
    uint8_t addr = readBlocking();
    buff[1] = addr;
    uint16_t sum = len + addr;
    for (int i = 0; i < len; i++) {
        uint8_t curr = readBlocking();
        buff[i + 2] = curr;
        sum += curr;
    }

    uint16_t checksum = (uint16_t) readBlocking() | ((uint16_t) readBlocking() << 8);
    if (checksum != (sum ^ 0xFFFF)) return;

    //Do brake and speed readings
    if (buff[1] == 0x20 && buff[2] == 0x65) brakeHandle = buff[6];
    if (buff[1] == 0x21 && buff[2] == 0x64 && buff[8] != 0) speed = buff[8];

    //Update the current index in the history and recalculate the average speed.
    historyTotal = historyTotal - history[historyIndex];
    history[historyIndex] = speed;
    historyTotal = historyTotal + history[historyIndex];
    historyIndex = historyIndex + 1;
    if (historyIndex >= historySize) historyIndex = 0;

    //Recalculate the average speed.
    averageSpeed = historyTotal / historySize;

    //Validate running timers.
    currentTime = millis();
    if (kickResetTimer != 0 && kickResetTimer + kickResetTime < currentTime && State == DRIVINGSTATE) resetKicks();
    if (increasmentTimer != 0 && increasmentTimer + increasmentTime < currentTime && State == INCREASINGSTATE) endIncrease();
    if (drivingTimer != 0 && drivingTimer + drivingTime < currentTime && State == DRIVINGSTATE) endDrive();
    if (kickDelayTimer == 0 || (kickDelayTimer != 0 && kickDelayTimer + kickDelay < currentTime)) {
        kickAllowed = true;
        kickDelayTimer = 0;
    } else {
        kickAllowed = false;
    }

    //Actual motion control.
    motion_control();
    computePID();
}

void motion_control() {
    if (speed < startThrottle) {
        targetSpeed = 0;
        if (speed != 0) { throttleWrite(45); currentThrottle = 45; }
        if (State != READYSTATE) Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
        State = READYSTATE;
    }

    if (brakeHandle > breakTriggered) { 
        targetSpeed = 0;
        currentThrottle = 45;                                        //close throttle directly when break is touched. 0% throttle
        throttleWrite(45); 
        speedController.SetMode(MANUAL);
        if (State != BREAKINGSTATE) {
            State = BREAKINGSTATE;
            drivingTimer = kickResetTimer = increasmentTimer = 0;
            Serial.println("BREAKING ~> Handle pulled.");
        }
    }

    switch(State) {
        case READYSTATE:
            if (speed > startThrottle) {                    //Check if speed exeeds start throttle.
                if (speed > minimumSpeed) {
                    if (averageSpeed > speed) {             
                        targetSpeed = averageSpeed;      //Average speed is higher than current speed, adjust to that.
                    } else {
                        targetSpeed = speed;
                    }
                } else {
                    targetSpeed = minimumSpeed;          //Set the expected speed to the minimum speed.
                }
                
                State = INCREASINGSTATE;
                speedController.SetMode(AUTOMATIC);
                Serial.println("INCREASING ~> The speed has exceeded the minimum throttle speed.");
            }

            break;
        case INCREASINGSTATE:
            if (speed >= temporarySpeed + calculateSpeedBump(temporarySpeed) && kickAllowed) {
                increasmentTimer = currentTime;
                increaseSpeed();
            } else if (averageSpeed >= speed && kickCount < 1 && increasmentTimer == 0) {
                increasmentTimer = currentTime;
            }

            kickCount = 0;
            break;
        case DRIVINGSTATE:
            if (kickDetected()) drivingTimer = kickResetTimer = currentTime;
            if (kickCount >= kicksBeforeIncreasment) {
                increaseSpeed();
                State = INCREASINGSTATE;
                drivingTimer = kickResetTimer = 0;
                Serial.println("INCREASING ~> The least amount of kicks before increasment has been reached.");
            }

            break;
        case BREAKINGSTATE:
        case DRIVEOUTSTATE:
            speedController.SetMode(MANUAL);
            if (brakeHandle > breakTriggered) break;
            if (State == DRIVEOUTSTATE && speed + forgetSpeed <= expectedSpeed) {
                Serial.println("DRIVEOUT ~> Speed has dropped too far under expectedSpeed. Dumping expected speed.");
                targetSpeed = 0;
            }

            if (speed >= averageSpeed + calculateSpeedBump(speed)) {
                if (State == DRIVEOUTSTATE) {
                    if (speed > averageSpeed + calculateMinimumSpeedIncreasment(speed)) {
                        targetSpeed = validateSpeed(speed);
                    } else {
                        if (speed > targetSpeed) {
                            targetSpeed = validateSpeed(averageSpeed + calculateMinimumSpeedIncreasment(speed));
                        } else {
                            targetSpeed = validateSpeed(expectedSpeed);
                        }
                    }
                } else {
                    targetSpeed = validateSpeed(speed);
                }

                kickDelayTimer = currentTime;
                State = INCREASINGSTATE;
                speedController.SetMode(AUTOMATIC);
                Serial.println("INCREASING ~> Speed increased after brake or driveout.");
            }
            
            break;
        default:
            targetSpeed = 0;
            currentThrottle = 0;
            throttleWrite(45);
            State = BREAKINGSTATE;
            drivingTimer = kickResetTimer = increasmentTimer = 0;
            Serial.println("BREAKING ~> Unknown state detected");
            speedController.SetMode(MANUAL);
    }

}

void resetKicks() {
    kickResetTimer = kickCount = 0;
}

void endIncrease() {
    State = DRIVINGSTATE;
    drivingTimer = currentTime;
    kickCount = increasmentTimer = 0; kickResetTimer = 0;
    Serial.println("DRIVING ~> The speed has been stabilized.");
}

void endDrive() {
    drivingTimer = kickResetTimer = 0;
    throttleWrite(45);
    targetSpeed = 0;
    currentThrottle = 0;
    State = DRIVEOUTSTATE;
    Serial.println("DRIVEOUT ~> Boost has expired.");
}

bool kickDetected() {
    bool result = speed >= targetSpeed + calculateSpeedBump(targetSpeed) && kickAllowed;
    if (result) { kickCount++; kickDelayTimer = currentTime; }
    return result;
}

double calculateSpeedBump(double requestedSpeed) {
    return (double) speedBump * pow(lowerSpeedBump, requestedSpeed);
}

double calculateMinimumSpeedIncreasment(double requestedSpeed) {
    return (requestedSpeed > enforceMinimumSpeedIncreasmentFrom ? (double) minimumSpeedIncreasment : 0.0);
}

double validateSpeed(double requestedSpeed) {
    return (requestedSpeed < minimumSpeed ? minimumSpeed : requestedSpeed);
}

void increaseSpeed() {
    if (speed > targetSpeed + calculateMinimumSpeedIncreasment(targetSpeed)) {
        targetSpeed = speed;
    } else {
        targetSpeed += calculateMinimumSpeedIncreasment(targetSpeed);
    }
}

int throttleWrite(int value) {
    if (value < 45) {
        analogWrite(THROTTLE_PIN, 45);
    } else if (value > 233) {
        analogWrite(THROTTLE_PIN, 233);
    } else {      
        analogWrite(THROTTLE_PIN, value);
    }
}

void computePID() {
    if (abs(targetSpeed - speed) <= calculateSpeedBump(targetSpeed) + extendLowRange) {
        speedController.SetTunings(kpLow, kiLow, kdLow);
        speedController.SetSampleTime(PIDSampleTimeLow);
    } else {
        speedController.SetTunings(kpHigh, kiHigh, kdHigh);
        speedController.SetSampleTime(PIDSampleTimeHigh);
    }

    speedController.Compute();
    if (State == INCREASINGSTATE || State == DRIVINGSTATE) throttleWrite((int) currentThrottle);
}