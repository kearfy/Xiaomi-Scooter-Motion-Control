#include <Arduino.h>

#include <arduino-timer.h>

#include <SoftwareSerial.h>

// +-============================================================================-+
// |-================================ SETTINGS ==================================-|
// +-============================================================================-+

const int speedBump = 1;
const int minimumSpeedIncreasement = 5;

const int minimumSpeed = 7;
const int maximumSpeed = 27;

const int startThrottle = 5;
const int kicksBeforeIncreasment = 2;
const int breakTriggered = 47;

const int drivingTime = 8000;
const int kickResetTime = 2000;
const int increasmentTime = 2000;
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


auto drivingTimer = timer_create_default();
auto kickResetTimer = timer_create_default();
auto increasmentTimer = timer_create_default();

int BrakeHandle;
int Speed; //current speed
int temporarySpeed = 0;
int expectedSpeed = 0;
int kickCount = 0;

int historyTotal = 0;
int history[historySize];
int historyIndex = 0;
int averageSpeed = 0;

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

    switch(State) {
        case READYSTATE:
            if (Speed > startThrottle) {
                if (Speed > minimumSpeed) {
                    if (averageSpeed > Speed) {
                        temporarySpeed = averageSpeed;
                    } else {
                        temporarySpeed = Speed;
                    }
                } else {
                    temporarySpeed = minimumSpeed;
                }
                
                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> The speed has exceeded the minimum throttle speed.");
            }

            break;
        case INCREASINGSTATE:
            if (Speed >= temporarySpeed + speedBump) {
                kickCount++;
                increasmentTimer.cancel();
            } else if (averageSpeed >= Speed && kickCount < 1) {
                increasmentTimer.in(increasmentTime, endIncrease);
            }

            if (kickCount >= 1) {
                if (Speed > temporarySpeed + minimumSpeedIncreasement) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(temporarySpeed + minimumSpeedIncreasement);
                }

                ThrottleSpeed(temporarySpeed);
            }

            kickCount = 0;
            break;
        case DRIVINGSTATE:
            if (Speed >= expectedSpeed + speedBump) {
                kickCount++;
                kickResetTimer.cancel();
                kickResetTimer.in(kickResetTime, resetKicks);
            }

            if (kickCount >= kicksBeforeIncreasment) {
                if (Speed > expectedSpeed + minimumSpeedIncreasement) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(expectedSpeed + minimumSpeedIncreasement);
                }

                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                drivingTimer.cancel();
                kickResetTimer.cancel();
                Serial.println("INCREASING ~> The least amount of kicks before increasment has been reached.");
            }

            break;
        case BREAKINGSTATE:
            if (BrakeHandle > breakTriggered) break;
            if (Speed < startThrottle) {
                State = READYSTATE;
                Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
            } else if (Speed >= averageSpeed + speedBump) {
                if (Speed > averageSpeed + minimumSpeedIncreasement) {
                    temporarySpeed = ValidateSpeed(Speed);
                } else {
                    temporarySpeed = ValidateSpeed(averageSpeed + speedBump);
                }

                ThrottleSpeed(temporarySpeed);
                State = INCREASINGSTATE;
                Serial.println("INCREASING ~> Break released and speed increased.");
            }
            
            break;
        default:
            ThrottleWrite(45);
            digitalWrite(LED_PCB, HIGH);
            State = BREAKINGSTATE;
            drivingTimer.cancel();
            kickResetTimer.cancel();
            increasmentTimer.cancel();
            Serial.println("BREAKING ~> Unknown state detected");
    }

}

int resetKicks() {
    kickCount = 0;
}

int endIncrease() {
    expectedSpeed = temporarySpeed;
    kickCount = 0;
    State = DRIVINGSTATE;
    drivingTimer.in(drivingTime, endDrive);
    Serial.println("DRIVING ~> The speed has been stabilized.");
}

int endDrive() {
    drivingTimer.cancel();
    kickResetTimer.cancel();
    ThrottleWrite(45);
    State = READYSTATE;
    Serial.println("READY ~> Speed has dropped under the minimum throttle speed.");
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
    if (requestedSpeed == 0) {
        ThrottleWrite(45);
    } else if (requestedSpeed == maximumSpeed) {
        ThrottleWrite(233);
    } else {
        int throttleRange = 233 - 45;
        float calculatedThrottle = requestedSpeed / (float) maximumSpeed * throttleRange;
        ThrottleWrite((int) calculatedThrottle);
    }
}

int ThrottleWrite(int value) {
    if (value != 0) {
        analogWrite(THROTTLE_PIN, value);
    } else {
        analogWrite(THROTTLE_PIN, 45);
    }
}
