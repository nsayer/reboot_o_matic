#!/usr/bin/python

# Reboot-o-matic
# Copyright 2019 Nicholas W. Sayer
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warran of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# This should be set up as a cron job, run no more than 4 times daily.

import RPi.GPIO as GPIO
import sys
import time
import subprocess
import os
import random
import syslog

syslog.openlog(facility=syslog.LOG_DAEMON)

# define this as the GPIO pin you're using as the power-cycle signal.
# This pin will go from high impedance to low for 2 seconds, and then
# released (so it's an open-drain output).
#
# GPIO pin 4 on a Raspberry Pi is physical pin 7
reset_pin = 4

# These are all public DNS servers. It's perhaps somewhat of a misuse
# of them to use them like this, but in my defense, answering a ping
# is less resource-intensive than a DNS query, and this script is designed
# to be extremely gentle.

hosts = ["1.1.1.1", "1.0.0.1", "8.8.8.8", "8.8.4.4", "8.26.56.26", "8.20.247.20", "9.9.9.9", "149.112.112.112", "64.6.64.6", "64.6.65.6"]

# do some load balancing
random.seed()
random.shuffle(hosts)

FNULL = open(os.devnull)

start = time.time()
while True:
	for host in hosts:
		res = subprocess.call(["ping", "-c", "3", "-W", "5", host], stdout=FNULL, stderr=FNULL)
		if (res == 0):
			syslog.syslog(syslog.LOG_DEBUG, host + " is up.")
			sys.exit(0) # it worked. Bail
		else:
			syslog.syslog(syslog.LOG_WARN, host + " is down.")
	if time.time() - start > 60*60:
		break
	time.sleep(5 * 60) # wait 5 minutes

syslog.syslog(syslog.LOG_CRIT, "All hosts unreachable for 60 minutes - resetting router")

# Perform the reset operation
GPIO.setmode(GPIO.BCM)
GPIO.setup(reset_pin, GPIO.OUT, initial=GPIO.LOW)
time.sleep(2)
GPIO.cleanup()

sys.exit(1)
