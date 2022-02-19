#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"

// +-============================================================================-+
// |-============================ SYSTEM VARIABLES ==============================-|
// +-============================================================================-+

unsigned long currentTime = 0;
unsigned long drivingTimer = 0;
unsigned long kickResetTimer = 0;
unsigned long kickDelayTimer = 0;
unsigned long increasmentTimer = 0;
bool kickAllowed = true;

int BrakeHandle;
int Speed; //current speed
int temporarySpeed = 0;
int expectedSpeed = 0;
int kickCount = 0;

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
    Serial.println("Starting Logging data...");

    TCCR1B = TCCR1B & 0b11111001; //Set PWM of PIN 9 & 10 to 32 khz
    ThrottleWrite(45);
}

uint8_t buff[256];
void loop() {
    int w = 0;
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
    if (buff[1] == 0x20 && buff[2] == 0x65) BrakeHandle = buff[6];
    if (buff[1] == 0x21 && buff[2] == 0x64 && buff[8] != 0) Speed = buff[8];

    //Update the current index in the history and recalculate the average speed.
    historyTotal = historyTotal - history[historyIndex];
    history[historyIndex] = Speed;
    historyTotal = historyTotal + history[historyIndex];
    historyIndex = historyIndex + 1;
    if (historyIndex >= historySize) historyIndex = 0;

    //Recalculate the average speed.
    averageSpeed = historyTotal / historySize;

    //Actual motion control.
    motion_control();

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
}

void motion_control() {
    if ((Speed != 0) && (Speed < startThrottle)) ThrottleWrite(45);             // If speed is under 5 km/h, stop throttle
    if (BrakeHandle > breakTriggered) {                                         //close throttle directly when break is touched. 0% throttle
        ThrottleWrite(45); 
        digitalWrite(LED_PCB, HIGH);
        if (State != BREAKINGSTATE) {
            State = BREAKINGSTATE;
            drivingTimer = 0; kickResetTimer = 0; increasmentTimer = 0;
            Serial.println("BREAKING ~> Handle pulled.");
        }
    } else {
        digitalWrite(LED_PCB, LOW);
    }

    switch(State) {
        case READYSTATE:
            if (Speed > startThrottle) {                    //Check if speed exeeds start throttle.
                if (Speed > minimumSpeed) {
                    if (averageSpeed > Speed) {             
                        temporarySpeed = averageSpeed;      //Average speed is higher than current speed, adjust to that.
                    } else {
                        temporarySpeed = Speed;
                    }
                } else {
                    temporarySpeed = minimumSpeed;          //Set the expected speed to the minimum speed.
                }
                
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> The speed has exceeded the minimum throttle speed.");
            }

            break;
        case INCREASINGSTATE:
            if (Speed >= temporarySpeed + calculateSpeedBump(temporarySpeed) && kickAllowed) {
                kickCount++;
                increasmentTimer = 0;
                kickDelayTimer = currentTime;
            } else if (averageSpeed >= Speed && kickCount < 1 && increasmentTimer == 0) {
                increasmentTimer = currentTime;
            }

            if (kickCount >= 1) {
                if (Speed > temporarySpeed + calculateMinimumSpeedIncreasment(Speed)) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(temporarySpeed + calculateMinimumSpeedIncreasment(Speed));
                }

                ThrottleSpeed(temporarySpeed);
            }

            kickCount = 0;
            break;
        case DRIVINGSTATE:
            if (Speed >= expectedSpeed + calculateSpeedBump(expectedSpeed) && kickAllowed) {
                kickCount++;
                drivingTimer = currentTime + increasmentTime;
                kickResetTimer = currentTime;
                kickDelayTimer = currentTime;
            }

            if (kickCount >= kicksBeforeIncreasment) {
                if (Speed > expectedSpeed + calculateMinimumSpeedIncreasment(Speed)) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(expectedSpeed + calculateMinimumSpeedIncreasment(Speed));
                }

                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                drivingTimer = 0; kickResetTimer = 0;
                Serial.println("INCREASING ~> The least amount of kicks before increasment has been reached.");
            }

            break;
        case BREAKINGSTATE:
        case DRIVEOUTSTATE:
            if (BrakeHandle > breakTriggered) break;
            if (State == DRIVEOUTSTATE && Speed + forgetSpeed <= expectedSpeed) {
                Serial.println("DRIVEOUT ~> Speed has dropped too far under expectedSpeed. Dumping expected speed.");
                expectedSpeed = 0;
            }


            if (Speed < startThrottle) {
                State = READYSTATE;
                Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
            } else if (Speed >= averageSpeed + calculateSpeedBump(Speed)) {
                if (State == DRIVEOUTSTATE) {
                    if (Speed > averageSpeed + calculateMinimumSpeedIncreasment(Speed)) {
                        temporarySpeed = ValidateSpeed(Speed);
                    } else {
                        if (Speed > expectedSpeed) {
                            temporarySpeed = ValidateSpeed(averageSpeed + calculateMinimumSpeedIncreasment(Speed));
                        } else {
                            temporarySpeed = ValidateSpeed(expectedSpeed);
                        }
                    }
                } else {
                    temporarySpeed = ValidateSpeed(Speed);
                }

                kickDelayTimer = currentTime;
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> Break released and speed increased.");
            }
            
            break;
        default:
            ThrottleWrite(45);
            digitalWrite(LED_PCB, HIGH);
            State = BREAKINGSTATE;
            drivingTimer = 0; kickResetTimer = 0; increasmentTimer = 0;
            Serial.println("BREAKING ~> Unknown state detected");
    }

}

int resetKicks() {
    kickResetTimer = 0;
    kickCount = 0;
}

int endIncrease() {
    expectedSpeed = temporarySpeed;
    kickCount = 0;
    State = DRIVINGSTATE;
    drivingTimer = currentTime;
    increasmentTimer = 0; kickResetTimer = 0;
    Serial.println("DRIVING ~> The speed has been stabilized.");
}

int endDrive() {
    drivingTimer = 0; kickResetTimer = 0;
    ThrottleWrite(45);
    State = DRIVEOUTSTATE;
    Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
}

int calculateSpeedBump(int requestedSpeed) {
  return round(speedBump * pow(lowerSpeedBump, requestedSpeed));
}

int calculateMinimumSpeedIncreasment(int requestedSpeed) {
    return (requestedSpeed > enforceMinimumSpeedIncreasmentFrom ? minimumSpeedIncreasment : 0);
}

int ValidateSpeed(int requestedSpeed) {
    if (requestedSpeed < minimumSpeed) {
        return minimumSpeed;
    } else if (requestedSpeed > maximumSpeed) {
        return maximumSpeed;
    } else {
        return requestedSpeed;
    }
}

int ThrottleSpeed(int requestedSpeed) {
    if (requestedSpeed <= 0) {
        ThrottleWrite(45);
    } else if (requestedSpeed >= maximumSpeed) {
        ThrottleWrite(233);
    } else {
        int throttleRange = 233 - 45;
        float calculatedThrottle = baseThrottle + ((requestedSpeed + additionalSpeed) / (float) maximumSpeed * throttleRange);
        ThrottleWrite((int) calculatedThrottle);
    }
}

int ThrottleWrite(int value) {
    if (value < 45) {
        analogWrite(THROTTLE_PIN, 45);
    } else if (value > 233) {
        analogWrite(THROTTLE_PIN, 233);
    } else {      
        analogWrite(THROTTLE_PIN, value);
    }
}
