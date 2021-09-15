#!/usr/bin/python3

import RPi.GPIO as GPIO
import csv
import json
import random
import serial
import subprocess
import syslog
import time

random.seed()

ser = serial.Serial('/dev/ttyUSB2')
# Use this for debugging
#ser = serial.serial_for_url('spy:///dev/ttyUSB2')

syslog.openlog(facility=syslog.LOG_DAEMON)

# GPIO pin 4 is actually pin 7 on the GPIO header.
# It's a good choice since pin 5 is a ground.
reset_pin = 4

def doCommand(command):
	ser.flushInput()
	ser.write((command + "\r").encode())
	ser.flush()
	out = []
	while True:
		line = ser.readline().rstrip().decode()
		if (line.startswith("ERROR")):
			raise Exception("command returned error")
		if (line.startswith("OK")):
			break
		out.append(line)
	return out[1:] # Skip over the echo of the command

def deleteSMS(record):
	doCommand("AT+CMGD=" + str(record))

def sendSMS(recipient, text):
	ser.flushInput()
	ser.write(("AT+CMGS=\"" + recipient + "\"\r").encode())
	ser.flush()
	ser.write((text + "\r").encode())
	ser.flush()
	ser.write(("\x1a").encode())
	ser.flush()
	while True:
		line = ser.readline().rstrip().decode()
		if (line.startswith("ERROR")):	
			raise Exception("Got error sending SMS")
		if (line.startswith("OK")):
			break

def pollSMS():
	result = doCommand("AT+CMGL=\"ALL\"")
	out = []
	for i in range(0, int(len(result)/2)):
		infoLine = result[2 * i]
		textLine = result[2 * i + 1]
		if (not infoLine.startswith("+CMGL:")):
			raise Exception("Unexpected CMGL response line: " + infoLine)
		infoLine = infoLine[7:]
		infos = next(csv.reader([infoLine], delimiter=',', quotechar='"'))
		out.append({'id':int(infos[0]), 'status':infos[1], 'sender':infos[2], 'time':infos[4], 'text':textLine})
	return out

def doStatus(sender):
	jsonTxt = subprocess.run(["ip", "-f", "inet", "-j", "address"], capture_output=True, text=True).stdout
	jsonDat = json.loads(jsonTxt)
	sTxt = ""
	for block in jsonDat:
		if (block['ifname'] == 'lo'):
			continue
		iface = block['ifname']
		for addr_info in block['addr_info']:
			if (addr_info['family'] != 'inet'):
				continue
			address = addr_info['local']
		sTxt += "Interface " + iface + " has address " + address + "\r"

	res = subprocess.run(["ping", "-c", "3", "-W", "5", "192.168.20.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
	if (res == 0):
		sTxt += "router is up." + "\r"
	else:
		sTxt += "router is down!" + "\r"

	hosts = ["1.1.1.1", "1.0.0.1", "8.8.8.8", "8.8.4.4", "8.26.56.26", "8.20.247.20", "9.9.9.9", "149.112.112.112", "64.6.64.6", "64.6.65.6"]
	random.shuffle(hosts)
	good = False
	for host in hosts:
		res = subprocess.run(["ping", "-c", "3", "-W", "5", host], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
		if (res == 0):
			sTxt += host + " is up." + "\r"
			good = True
			break
	if (good == False):
		sTxt += "All hosts down!" + "\r"
	sendSMS(sender, sTxt)

def doReboot():
	syslog.syslog(syslog.LOG_CRIT, "remotely commanded reset of router")
	GPIO.setmode(GPIO.BCM)
	GPIO.setup(reset_pin, GPIO.OUT, initial=GPIO.LOW)
	time.sleep(2)
	GPIO.cleanup()
	
while True:
	texts = pollSMS()
	for text in texts:
		syslog.syslog(syslog.LOG_INFO, "Received message from " + text['sender'] + ": " + text['text'])
		deleteSMS(text['id'])
		msg = text['text']
		if (not msg.startswith("!")):
			continue
		msg = msg[1:]
		if (msg == 'STATUS'):
			doStatus(text['sender'])
		elif (msg == 'REBOOT'):
			doReboot()
			sendSMS(text['sender'], "Reboot performed.")
		else:
			sendSMS(text['sender'], "unk: " + msg)
	time.sleep(15)
