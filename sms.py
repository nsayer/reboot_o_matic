#!/usr/bin/python3

import serial
import csv
import RPi.GPIO as GPIO
import time
import syslog

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
			sendSMS(text['sender'], "Ready.")
		elif (msg == 'REBOOT'):
			doReboot()
			sendSMS(text['sender'], "Reboot performed.")
		else:
			sendSMS(text['sender'], "unk: " + msg)
	time.sleep(15)
