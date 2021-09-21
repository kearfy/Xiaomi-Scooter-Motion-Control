#include <Arduino.h>
#include <SoftwareSerial.h>
#include <PID_v1.h>
#include "config.h"

double targetSpeed, speed, currentThrottle, brakeHandle;
PID speedController(&speed, &currentThrottle, &targetSpeed, kpHigh, kiHigh, kdHigh, DIRECT);
SoftwareSerial SoftSerial(SERIAL_READ_PIN, 3);

unsigned long currentTime = 0;
unsigned long drivingTimer = 0;
unsigned long kickResetTimer = 0;
unsigned long kickDelayTimer = 0;
unsigned long increasmentTimer = 0;
bool kickAllowed = true;

int kickCount = 0;
int historyTotal = 0;
int history[historySize];
int historyIndex = 0;
int averageSpeed = 0;

uint8_t State = 0;
#define READYSTATE 0
#define INCREASINGSTATE 1
#define DRIVINGSTATE 2
#define BREAKINGSTATE 3
#define DRIVEOUTSTATE 4

uint8_t readSerial() {
    while (!SoftSerial.available()) delay(1);
    return SoftSerial.read();
}

void setup() {
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
    while (readSerial() != 0x55);
    if (readSerial() != 0xAA) return;
    uint8_t len = readSerial();
    buff[0] = len;
    if (len > 254) return;
    uint8_t addr = readSerial();
    buff[1] = addr;
    uint16_t sum = len + addr;
    for (int i = 0; i < len; i++) {
        uint8_t curr = readSerial();
        buff[i + 2] = curr;
        sum += curr;
    }

    uint16_t checksum = (uint16_t) readSerial() | ((uint16_t) readSerial() << 8);
    if (checksum != (sum ^ 0xFFFF)) return;
    if (buff[1] == 0x20 && buff[2] == 0x65) brakeHandle = buff[6];
    if (buff[1] == 0x21 && buff[2] == 0x64 && buff[8] != 0) speed = buff[8];

    historyTotal = historyTotal - history[historyIndex];
    history[historyIndex] = speed;
    historyTotal = historyTotal + history[historyIndex];
    historyIndex = historyIndex + 1;
    if (historyIndex >= historySize) historyIndex = 0;
    averageSpeed = historyTotal / historySize;

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

    motionControl();
    computePID();
}

void motionControl() {
    Serial.println("SPEED");
    Serial.println(speed);
    Serial.println("TARGET");
    Serial.println(targetSpeed);
    Serial.println("THROTTLE");
    Serial.println(currentThrottle);
  
    if (speed < startThrottle) {
        targetSpeed = 0;
        State = READYSTATE;
        if (speed != 0) { throttleWrite(45); currentThrottle = 0; }
        if (State != READYSTATE) Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
    } 
    
    if (brakeHandle > breakTriggered) {
        targetSpeed = 0;
        currentThrottle = 45;
        throttleWrite(45);
        if (State != BREAKINGSTATE) {
            State = BREAKINGSTATE;
            drivingTimer = kickResetTimer = increasmentTimer = 0;
            Serial.println("BREAKING ~> Handle pulled.");
        }
    }

    switch(State) {
        case READYSTATE:
            if (speed > startThrottle) {
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> The speed has exceeded the minimum throttle speed.");
                if (speed > minimumSpeed) {
                    if (averageSpeed > speed) {
                        throttleSpeed(averageSpeed, false);
                    } else {
                        throttleSpeed(speed, false);
                    }
                } else {
                    throttleSpeed(minimumSpeed, false);
                }
            }

            speedController.SetMode(MANUAL);
            break;
        case INCREASINGSTATE:
            if (kickDetected()) {
                increasmentTimer = currentTime;
                increaseSpeed();
            } else if (averageSpeed >= speed && kickCount < 1 && increasmentTimer == 0) {
                increasmentTimer = currentTime;
            }

            kickCount = 0;
            speedController.SetMode(AUTOMATIC);
            break;
        case DRIVINGSTATE:
            if (kickDetected()) drivingTimer = kickResetTimer = currentTime;
            if (kickCount >= kicksBeforeIncreasment) {
                increaseSpeed();
                State = INCREASINGSTATE;
                drivingTimer = 0; kickResetTimer = 0;
                Serial.println("INCREASING ~> The least amount of kicks before increasment has been reached.");
            }

            speedController.SetMode(AUTOMATIC);
            break;
        case BREAKINGSTATE:
        case DRIVEOUTSTATE:
            if (brakeHandle > breakTriggered) break;
            if (speed >= averageSpeed + calculateSpeedBump(speed)) {
                kickDelayTimer = currentTime;
                if (State == DRIVEOUTSTATE) {
                    throttleSpeed(targetSpeed, true);
                    State = DRIVINGSTATE;
                    Serial.println("DRIVING ~> kick detected after boost expired.");
                } else {
                    if (speed > averageSpeed + calculateMinimumSpeedIncreasment(speed)) {
                        throttleSpeed(speed, false);
                    } else {
                        throttleSpeed(averageSpeed + calculateMinimumSpeedIncreasment(speed), true);
                    }

                    State = INCREASINGSTATE;
                    Serial.println("INCREASING ~> Break released and speed increased.");
                }
            }
            
            speedController.SetMode(MANUAL);
            break;
        default:
            targetSpeed = 0;
            currentThrottle = 0;
            throttleWrite(45);
            State = BREAKINGSTATE;
            drivingTimer = 0; kickResetTimer = 0; increasmentTimer = 0;
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
    increasmentTimer = kickResetTimer = kickCount = 0;
    Serial.println("DRIVING ~> The speed has been stabilized.");
}

void endDrive() {
    drivingTimer = 0; kickResetTimer = 0;
    throttleWrite(45);
    targetSpeed = 0;
    currentThrottle = 0;
    State = DRIVEOUTSTATE;
    Serial.println("DRIVEOUT ~> Boost has expired.");
}

int kickDetected() {
    bool result = speed >= targetSpeed + calculateSpeedBump(targetSpeed) && kickAllowed;
    if (result) { kickCount++; kickDelayTimer = currentTime; }
    return result;
}

int calculateSpeedBump(int requestedSpeed) {
    return round(speedBump * pow(lowerSpeedBump, requestedSpeed));
}

int calculateMinimumSpeedIncreasment(int requestedSpeed) {
    return (requestedSpeed > enforceMinimumSpeedIncreasmentFrom ? minimumSpeedIncreasment : 0);
}

void increaseSpeed() {
    if (speed > targetSpeed + calculateMinimumSpeedIncreasment(targetSpeed)) {
        throttleSpeed(speed, false);
    } else {
        throttleSpeed(targetSpeed + calculateMinimumSpeedIncreasment(targetSpeed), true);
    }
}

void throttleSpeed(int requestedSpeed, bool forceEstimation) {
    if (requestedSpeed < 5) {
        throttleWrite(45);
        targetSpeed = 0;
        currentThrottle = 0;
    } else {
        targetSpeed = (requestedSpeed < minimumSpeed ? minimumSpeed : requestedSpeed);
        if (currentThrottle <= 45 || true) {
            int throttleRange = 233 - 45;
            float calculatedThrottle = (baseThrottle + ((targetSpeed + additionalSpeed) / (float) maximumSpeed * throttleRange)) * initialThrottleLimiter;
            currentThrottle = ((int) calculatedThrottle < 45 ? 45 : (int) calculatedThrottle);
            throttleWrite(currentThrottle);
        }
    }
}

void throttleWrite(int value) {
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
