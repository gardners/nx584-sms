To use this software, you will need:

1. Install https://github.com/kk7ds/pynx584.git
2. Install an NX584 board in your alarm.
3. Follow the steps in PANEL-SETUP.md to let the NX584 do things.
4. Run the nx584_sever program, capturing output to a log file, e.g.:
     nx584_server > /somewhere/alarm.log 2>&1
5. Run the nx584-sms program with the alarm log, path to nx584_client program, and master alarm pin on the command line like this:
     nx584-sms /somewhere/alarm.log nx584_client=../nx584_client master=1234

It will then monitor the alarm.  If you want to interact with this command to use any of the commands that can be used
via the SMS interface from the command line, add - to the end of the command line, e.g.:

     nx584-sms /somewhere/alarm.log nx584_client=../nx584_client master=1234 -

Note that this program will automatically figure out which is the log, and which is the modem.

To find out what commands you can use, type help to the command interface (either interactively, or via SMS).

TODO: Run nx584_server automatically after working out which device is modem, and which is the nx584 serial interface.
