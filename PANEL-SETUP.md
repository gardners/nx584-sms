To enable this program to work, you have to install an NX584 in your alarm.
Assuming it is on the standard address of 72, and you have a dial-pad panel
for your alarm, use the following sequence to give the nx584 permission to
arm and disarm the alarm, and generally query it:

*8   to enter programming mode. all LEDs begin to flash
9713 to enter service mode (your code might be different). service light will flash, and function leds will be on solid.
72#  to select the NX584 board. Armed led will light.
0#    to program location 0. armed led will go off, service LED will begin flashing
1    if required, to make the "1" digit display. This enables ASCII output mode
*    to save changes. Armed led goes back on solid.
1#   to program location 1. armed led goes off, service LED will begin flashing.
1-8  press digits until only the '3' digit is visible. This selects 9600bps
*    save changes. Armed led goes back on solid.
2#   to program location 2. armed led goes off, service LED will begin flashing.
1-8  press digits until exactly 2,5,6,7 and 8 are lit.
*    save changes
1-8  press digits until exactly 1,2 and 4 are lit.
*    save changes. Armed led goes back on solid.
3#   to program location 3. armed led goes off, service LED will begin flashing.
1-8  press digits until exactly 2,4,5,6,7 and 8 are lit.
*    save changes.
1-8  press digits until exactly 1,2,3,4 and 5 are lit.
*    save changes.
1-8  press digits until exactly 1,2,3,4,5,6,7 and 8 are lit.
*    save changes.
1-8  press digits until exactly 3,4,5,6,7 and 8 are lit.
*    save changes. Armed led goes back on solid.
EXIT Armed light goes off.
EXIT Service LED stops blinking, READY and POWER LEDs are back on solid.

This should be done before running the software


