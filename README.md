# This is a fork! If you have already read this stuff in the original repo by PsychoMnts / Glenn, this is what was added in the README:
- [This fork](#this-fork)
- [Issues](#issues)
- [Releases](#releases)
- [Custom Telegram group](#custom-telegram-group)

### **Please use the 3.4 release and configure baseThrottle (ln. 27) accordingly to the comments!**

Thanks :)

## Xiaomi-Scooter-Motion-Control
Modification to legalise the Xiaomi Mi Scooters in The Netherlands.

The idea is to make an small hardware modification on the Xiaomi scooters so they comply with the Dutch law. 

To use an scooter (or how we call it here: e-step) in The Netherlands, you must comply with the following rules:
- There must NO throttle button.
- The motor must be limited to 250 watts.
- The motor can only give a "boost" when you kick with your feet.
- The motor should stop working when stop kicking. (fade-out)
- Max speed is 25 km/h.

Example:
Micro has e-steps which are modified to comply with dutch rules with an deactivated throttle, like the MICRO M1 COLIBRI.
https://www.micro-step.nl/nl/emicro-m1-colibri-nl.html


The best scooter to do this modification is the Xiaomi Mi Electric Scooter Essential:
- The motor is 250 watts
- Max speed is 20 km/h, which is already fast to give push offs with your feet.

Check out [stepombouw.nl](https://stepombouw.nl) for more information and guides!

# Librarys

- https://github.com/contrem/arduino-timer (For 3.1 and 3.2)

# How it works

An Arduino Nano will be used to read out the serial-bus of the Xiaomi Mi Scooter.
The speedometer will be monitored if there are any kicks with your feed. When there is a kick, the throttle will be opened to 100% for 8 seconds and then goes to 10% (0% is regen breaking).
When the brakehandle is being touched the throttle will be released immediately. Also the Mi scooter itself disables the throttle also in case of braking.

# Custom telegram group

I have created a custom telegram group for questions and the development specially for my V3.X fork of off the original software. This way I hope to reduce the amount of unnessacery messages for the "overall" / "main" group and forward them to the "V3.X group".

- [Main group](https://t.me/joinchat/IuIjHecjckhK1h-a)
- [This fork](https://t.me/scooter_motion_control_v3)

# This fork

I have made this fork to challenge myself in writing a custom firmware for my Xiaomi Mi Essential, but mainly to customize the functionality to my needs.

The fork is based of off [Glenn's V1.3](https://github.com/PsychoMnts/Xiaomi-Scooter-Motion-Control/blob/main/Xiaomi-Scooter-Motion-Control_V1.3/Xiaomi-Scooter-Motion-Control_V1.3.ino). I chose to number my builds with V3 since this the third public variation to the firmware for the arduino.

This firmware leaves behind the concept of different specified gears. Instead, the vehicle will adjust to the speed that you are going.

Another aspect that makes this firmware stand out is that it is able to detect kicks while the vehicle is throttling. It does so by storing and comparing an expected speed to the actual speed of the vehicle. If the actual speed exceeds expected speed by a defined integer, it is registered as a kick.

**The following section is configurable. Can be set to one kick aswell.**

One kick while throttling will reset the driving timer, two kicks will put the vehicle into INCREASINGSTATE. Whilst in INCREASINGSTATE, the speed of the vehicle can be increased (duh...). When you speed the vehicle by making a kick, the vehicle will adjust to that new speed. If a defined time has passed by without the driver making a new kick, the vehicle will be put back into DRIVINGSTATE and the driving timer will be started again.
The vehicle will also be switched to INCREASINGSTATE when you first start driving or after you have released the break and increasing speed.

Once the driving timer has expired, the throttle will be released. When you make a new kick the vehicle will adjust to the averageSpeed of the past defined history of speeds recordings.

# Issues

(3.4) Some false kicks may occur, we are working on the optimal calculatedSpeed in the ThrottleSpeed function. Additonally, a variant with PID support is in the making so that the vehicle will better adjust to the expected speed.

# Releases

- V3.1
    
    This is the first release of V3 (This variation of the firmware).
    It is a proof of concept and works unexpectedly well. The two issues that exist in this build are:
    - that the expected speeds are not consistent with the actual speed obtain from the vehicle.
    - and, and issue in the concept, that it's a little bit hard to speed up above 18 ~ 20km/u.

- V3.2

    The issues have been resolved. It is now possible to percentually lower the speedBump per km/u speed with the lowerSpeedBump option.

    A few recommended values have been provided.

- V3.3

    Skipped due to the amount of issues.

- V3.4

    - Added a option to set a minimum speed for when the minimumSpeedIncreasment will fire at first.
    - Added baseThrottle and additionalSpeed to be able to easily adjust the calculatedSpeed within ThrottleSpeed.
    - Added a kickDelay. This is the minimum time before another kick can be registered again.
    - Switched to timers based on the millis() function. No memory exhaustion anymore!
    - Added a DRIVEOUTSTATE to prevent false kicks after the boost has expired.
    - Probably more changes, cleaned up a lot of stuff.

- V3.4.1

    - Changed default value for lowerSpeedBump to 0.9875 since kicks at higher speeds were impossible.
    - Added forgetSpeed since you could currently drive out from 25km/u to 6km/u, make a kick and go full speed again.
    - Made small changes in the behaviour of the DRIVEOUTSTATE.


# Other firmwares

Feel free to try these other firmwares, I will try to include as many firmwares that are out there.

- Glenn: https://github.com/PsychoMnts/Xiaomi-Scooter-Motion-Control
- Jelle: https://github.com/jelzo/Xiaomi-Scooter-Motion-Control
- Job: https://github.com/mysidejob/step-support 

# Hardware

- Arduino Nano
- 1k resistor
- 0.47uF Capacitor

If you don't want to solder on your scooter, you need also:

- JST-ZH male-plug. (or cut it from the trottle, a new one is 2 - 4 euro) https://nl.aliexpress.com/item/1005001992213252.html
- A male and female 4-pole e-bike plug like: https://nl.aliexpress.com/item/4001091169417.html (Blue plug)


# Wiring

![alt text](https://github.com/Kearfy/Xiaomi-Scooter-Motion-Control/blob/main/readme-sources/Wiring%20Scheme_v3.png?raw=true)

# Supported models
- XIAOMI Mi Electric Scooter Essential
- XIAOMI Mi Electric Scooter 1S EU

Limit motor modification required:
- XIAOMI Mi Electric Scooter M365 Pro
- XIAOMI Mi Electric Scooter Pro 2


Guide: https://github.com/Kearfy/Xiaomi-Scooter-Motion-Control/blob/main/readme-sources/How%20to%20limit%20the%20Xiaomi%20Pro2%20scooter.docx?raw=true

XIAOMI Mi Electric Scooter M365 without Speed-O-Meter is NOT compatible!

To help supporting more scooters, please use the sniffing tool and share the serial bus data. Join our telegram group if you want to help in this project. https://t.me/joinchat/IuIjHecjckhK1h-a


# IenW over steps met stepondersteuning

"We stellen ons echter op het standpunt dat een tweewielig voertuig, dat met eigen spierkracht wordt voortbewogen en dat duidelijk op het fietspad thuishoort, in de categorie fiets hoort te vallen. (...)  Door de aard van de ondersteuning vallen deze steppen dus ook in de categorie ‘fiets met trapondersteuning’ en hoeven ze niet apart als bijzondere bromfiets te worden toegelaten. U mag hiermee tot maximaal 25 km/u op de openbare weg rijden."

DE MINISTER VAN INFRASTRUCTUUR EN WATERSTAAT,

Namens deze,

Hoofd afdeling Verkeersveiligheid en Wegvervoer

drs. M.N.E.J.G. Philippens


