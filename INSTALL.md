make
sudo cp nx584-sms /usr/local/bin
sudo apt-get install gammu wammu
sudo pip install stevedore
sudo cp alarm-monitor /etc/init.d
sudo chmod 755 /etc/init.d/alarm-monitor
# Modify the nx584_server start line to replace the serial port name with the real path
# (We recommend using /dev/serial/by-id/.... path to the port, so that it doesn't change unexpectedly)
sudo vi /etc/init.d/alarm-monitor

sudo update-rc.d alarm-monitor defaults
# Replace the fake phone number below with the phone number of person who will run the system:
echo "admin +98173489172348971239847128397" | sudo tee /usr/local/etc/nx584-sms.conf