
# Compile this program
make
# Install it
sudo cp nx584-sms /usr/local/bin
# download pynx584
( cd .. ; git clone https://github.com/kk7ds/pynx584.git )
# Install some dependencies
sudo apt-get install gammu wammu
sudo pip install stevedore
# Install and customise the init.d script:
sudo cp alarm-monitor /etc/init.d
sudo chmod 755 /etc/init.d/alarm-monitor
# Modify the nx584_server start line to replace the serial port name with the real path
# (We recommend using /dev/serial/by-id/.... path to the port, so that it doesn't change unexpectedly)
sudo vi /etc/init.d/alarm-monitor
# Create /etc/gammurc with the serial ports for your USB dongle in there. Again, use /dev/serial/by-id/... paths to avoid confusion (it will otherwise wait forever if it tries to talk to the alarm's serial port!)  gammu-detect can be used to create a template config file, which you can then modify
sudo update-rc.d alarm-monitor defaults
# Replace the fake phone number below with the phone number of person who will run the system:
echo "admin +98173489172348971239847128397" | sudo tee /usr/local/etc/nx584-sms.conf