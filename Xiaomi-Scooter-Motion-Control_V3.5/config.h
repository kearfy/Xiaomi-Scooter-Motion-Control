// +-============================================================================-+
// |-============================== CONFIGURATION ===============================-|
// +-============================================================================-+

// TIMERS
const int drivingTime = 8000;                           //Defines how long one kick should take.
const int kickResetTime = 2000;                         //Defines how much time the amount of kicks before increasment can take up.
const int increasmentTime = 2000;                       //Defines the amount of time the INCREASING state will wait for a new kick.
const int kickDelay = 500;                              //Defines how much time should be in between two kicks.

// KICK DETECTION
const int speedBump = 2;                                //Defines how much km/u increasment is detected as a kick.
const float lowerSpeedBump = 0.99;                      //Defines how much percent the speedBump should go down per km in speed. (it's harder to speed up when driving 20km/u.)
const int kicksBeforeIncreasment = 1;                   //Amount of kicks it takes to switch over the INCREASING state.

// MINIMUM SPEED INCREASMENT
const int enforceMinimumSpeedIncreasmentFrom = 15;      //From which speed should the minimumSpeedIncreasment be activated.
const int minimumSpeedIncreasment = 0;                  //Defines what the minimal increasement every kick should make.

// THROTTLE BEHAVIOUR
const int startThrottle = 5;                            //Minimum speed before throtteling, usually just 5.
const int minimumSpeed = 5;                             //Minimum speed to drive at.
const int maximumSpeed = 25;                            //Maximum speed that your scooter can drive at. This value does not have to be accurate but helps to initially drive near the requested speed.
const int baseThrottle = 0;
const int additionalSpeed = 4;
const float initialThrottleLimiter = 1.0;               //When the initial throttle based on the max speed is calculated it will be multiplied by this to prevent an overshoot by the PID controller.

//PID TUNING
const double kpHigh = 25;
const double kiHigh = 25;
const double kdHigh = 1;

const double kpLow = 2;
const double kiLow = 25;
const double kdLow = 0;

// ADVANCED CONFIGURATION.
const int extendLowRange = 1;
const int PIDSampleTimeHigh = 100;
const int PIDSampleTimeLow = 200;
const int historySize = 20;                             //How many past speed readings should be saved for average speed calculation.
const int breakTriggered = 47;                          //Don't touch unless you know what you are doing.
const int SERIAL_READ_PIN = 2;                          //Arduino pin to which the yellow wire is connected, used to read the scooter.
const int THROTTLE_PIN = 10;                            //Arduino pin where throttle is connected to. (only pin 9 & 10 is ok to use)
int LED_PCB = 13;                                       //Arduino pin where throttle is connected to (only pin 9 & 10 is ok to use)        //Pin on the arduino of the internal led.
