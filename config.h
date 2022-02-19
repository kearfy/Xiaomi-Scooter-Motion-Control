// +-============================================================================-+
// |-============================== CONFIGURATION ===============================-|
// +-============================================================================-+

// TIMERS
const int drivingTime = 5000;                           //Defines how long a boost should last.
const int kickResetTime = 2000;                         //Defines the timespan in between the x amount of kicks should be made, starting when the first kick is detected.
const int increasmentTime = 2000;                       //Defines the amount of time the INCREASING state will wait for a new kick.
const int kickDelay = 300;                              //Defines how much time should be in between two kicks.

// KICK DETECTION
const int speedBump = 3;                                //Defines how much km/u increasment is detected as a kick.
const float lowerSpeedBump = 0.9872;                    //Defines how much percent the speedBump should go down per km in speed. (it's harder to speed up when driving 20km/u.)
const int kicksBeforeIncreasment = 1;                   //Amount of kicks it takes to switch over the INCREASING state.
const int forgetSpeed = 10;                             //After how many X amount of km/u dropped, should the remembered speed to catch up to be forgotten.

// MINIMUM SPEED INCREASMENT
const int enforceMinimumSpeedIncreasmentFrom = 18;      //From which speed should the minimumSpeedIncreasment be activated.
const int minimumSpeedIncreasment = 5;                  //Defines what the minimal increasement every kick should make.

// THROTTLE BEHAVIOUR
const int startThrottle = 5;                            //Minimum speed before throtteling, usually just 5.
const int minimumSpeed = 5;                             //Minimum speed to drive at.
const int maximumSpeed = 25;                            //Maximum speed that your scooter can drive at. This value does not have to be accurate but helps to initially drive near the requested speed.
const int baseThrottle = 45;
const int additionalSpeed = 0;

// ADVANCED CONFIGURATION.
const int historySize = 20;                             //How many past speed readings should be saved for average speed calculation.
const int breakTriggered = 47;                          //Don't touch unless you know what you are doing.
const int SERIAL_READ_PIN = 2;                          //Arduino pin to which the yellow wire is connected, used to read the scooter.
const int THROTTLE_PIN = 10;                            //Arduino pin where throttle is connected to. (only pin 9 & 10 is ok to use)
int LED_PCB = 13;                                       //Arduino pin where throttle is connected to (only pin 9 & 10 is ok to use)        //Pin on the arduino of the internal led