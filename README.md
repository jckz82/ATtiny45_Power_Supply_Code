# ATtiny45_Power_Supply_Code

Power sensitive control logic for ATtiny45 on a FlashCandy.us 1.8V/3.3V/5V 2A power supply.

See the project and design files at https://flashcandy.us/products/3v32ar3

When the power switch is on, it checks the battery voltage every 4 seconds.  When the battery drops below 3.3V, the output is disabled to protect the battery.

A watchdog timer and deep sleep modes allow for 4uA in idle mode.

Written and compiled in Atmel Studio 7.
