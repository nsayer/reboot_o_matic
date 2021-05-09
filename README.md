
Firmware for https://hackaday.io/project/168150-reboot-o-matic/

This is relatively simple - an open-drain input must be pulled low and kept low for a second and then an
output will be set high for 10 seconds. After the input goes low, it must go back high for a second before
it can go low again, and any subsequent low pulse within an hour of the last one will be ignored.

There are two python scripts that can be used with a Raspberry Pi to control Reboot-o-matic. The first is a ping script that's intended to be run from cron. It tries to ping a number of public DNS servers on the Internet for a half hour, and if none of them are reachable, then the reboot is triggered.

The secnod script works if you have a SMS capable GSM/HSPA/whatever modem connected via USB. It waits for SMS messages. If they start with "!" then they are echoed back to the sender (this prevents spam texts from getting a response, which invites more spam). If you send "!STATUS" thatn you will additionally get back a "Ready." response, which proves that the parser is working. If you send "!REBOOT" then the reboot is triggered.
