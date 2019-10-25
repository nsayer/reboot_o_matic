
Firmware for https://hackaday.io/project/168150-reboot-o-matic/

This is relatively simple - an open-drain input must be pulled low and kept low for a second and then an
output will be set high for 2 seconds. After the input goes low, it must go back high for a second before
it can go low again, and any subsequent low pulse within an hour of the last one will be ignored.
