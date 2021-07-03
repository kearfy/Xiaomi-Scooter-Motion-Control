#include <Arduino.h>

#include <arduino-timer.h>

#include <SoftwareSerial.h>

// +-============================================================================-+
// |-================================ SETTINGS ==================================-|
// +-============================================================================-+

int speedBump = 3;
int minimumSpeedIncreasement = 5;

int minimumSpeed = 12;
int maximumSpeed = 27;

int startThrottle = 5;
int kicksBeforeIncreasment = 2;
int breakTriggered = 47;

int minimumDrivingTime = 1000;
int maximumDrivingTime = 8000;

int speedRange = maximumSpeed - minimumSpeed;
int drivingTimeRange = maximumDrivingTime - minimumDrivingTime;
int historySize = 20;

// Arduino pin where throttle is connected to (only pin 9 & 10 is ok to use)
const int THROTTLE_PIN = 10;
int LED_PCB = 13;

//TX & RX pin
SoftwareSerial SoftSerial(2, 3); // RX, TX

//END OF SETTINGS

// +-============================================================================-+
// |-=============================== VARIABLES ==================================-|
// +-============================================================================-+


auto timer_m = timer_create_default();

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
#define readystate 0
#define increasingstate 1
#define drivingstate 2
#define breakingstate 3

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
    averageSpeed = historyTotal / speedReadings;

    Serial.print("Speed: ");
    Serial.print(AverageSpeed);
    Serial.print(" Brake: ");
    Serial.print(BrakeHandle);
    Serial.print(" State: ");
    Serial.print(motionstate);
    Serial.println(" ");

    motion_control();
    timer_m.tick();
}

void motion_control() {
    if ((Speed != 0) && (Speed < startThrottle)) {
        // If speed is under 5 km/h, stop throttle
        ThrottleWrite(45); //  0% throttle
    }

    if (BrakeHandle > breakTriggered) {
        ThrottleWrite(45); //close throttle directly when break is touched. 0% throttle
        Serial.println("BRAKE detected!!!");
        digitalWrite(LED_PCB, HIGH);
        State = breakingstate;
        timer_m.cancel();
    } else {
        State = readystate;
        digitalWrite(LED_PCB, LOW);
    }

    switch(State) {
        case readystate:
            if (Speed > startThrottle) {

            }

            break;
        case increasingstate:
            
            break;
        case drivingstate:
            
            break;
        case breakingstate:
            
            break;
    }

    if (Speed != 0) {
        // Check if auto throttle is off and speed is increasing
        if (motionstate == motionready) {
            trend = AverageSpeed - OldAverageSpeed;

            if (trend > 0) {
                // speed is increasing
                // Check if speed is at least 5 km/h
                if (AverageSpeed > minimum_throttle_speed) {
                    // Open throttle for 5 seconds
                    AnalyseKick();
                    Serial.println("Kick detected!");
                    motionstate = motionbusy;
                }
            } else if (trend < 0) {
                // speed is decreasing
            } else {
                // no change in speed
            }
        }

        oldspeed = Speed;
        OldAverageSpeed = AverageSpeed;
    }

}

bool release_throttle(void * ) {
    Serial.println("Timer expired, stopping...");

    if (SCOOTERTYPE == 0) {
        if (THROTTLE_FULL_RELEASE == 0) {
            //Keep throttle open for 10% to disable KERS. best for essential.
            ThrottleWrite(80); 
        } else {
            ThrottleWrite(45);
        }
    } else if (SCOOTERTYPE == 1 || SCOOTERTYPE == 2) {
        //Close throttle. best for pro 2 & 1S.
        ThrottleWrite(45); 
    }

    timer_m.in(kickdelay, motion_wait);
    return false; // false to stop
}

bool motion_wait(void * ) {
    Serial.println("Ready for new kick!");
    motionstate = motionready;
    return false; // false to stop
}



void AnalyseKick() {
    if (AverageSpeed < 10) {
        ThrottleWrite(140); //  40% throttle
        timer_m.in(boosttimer_tier1, release_throttle); //Set timer to release throttle
    } else if ((AverageSpeed >= 10) & (AverageSpeed < 14)) {
        ThrottleWrite(190); //  80% throttle
        timer_m.in(boosttimer_tier2, release_throttle); //Set timer to release throttle
    } else {
        ThrottleWrite(233); //  100% throttle
        timer_m.in(boosttimer_tier3, release_throttle); //Set timer to release throttle
    }

}

int ThrottleSpeed(int requestedSpeed) {
    int throttleRange = 233 - 45;
    int useThrottle = requestedSpeed / maximumSpeed * throttleRange;
    Serial.println("Throttling to speed");
    Serial.println(requestedSpeed);
    Serial.println("By using throttle");
    Serial.println(useThrottle);
    Serial.println(' ');
}

int ThrottleWrite(int value) {
    if (value != 0) {
        analogWrite(THROTTLE_PIN, value);
    } else {
        analogWrite(THROTTLE_PIN, 45);
    }
}