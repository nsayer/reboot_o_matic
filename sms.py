#!/usr/bin/python

import serial
import io
import csv
import RPi.GPIO as GPIO
import time
import syslog

ser = serial.Serial('/dev/ttyUSB2')
# Use this for debugging
#ser = serial.serial_for_url('spy:///dev/ttyUSB2')
sio = io.TextIOWrapper(io.BufferedRWPair(ser, ser))

syslog.openlog(facility=syslog.LOG_DAEMON)

# GPIO pin 4 is actually pin 7 on the GPIO header.
# It's a good choice since pin 5 is a ground.
reset_pin = 4

def doCommand(command):
	ser.flushInput()
	sio.write(unicode(command + "\r"))
	sio.flush()
	out = []
	while True:
		out.append(ser.readline().rstrip())
		if (out[-1].startswith("ERROR")):
			raise Exception("command returned error")
		if (out[-1].startswith("OK")):
			break
	return out

def deleteSMS(record):
	doCommand("AT+CMGD=" + str(record))

def sendSMS(recipient, text):
	ser.flushInput()
	sio.write(unicode("AT+CMGS=\"" + recipient + "\"\r"))
	sio.flush()
	sio.write(unicode(text + "\r"))
	sio.flush()
	sio.write(unicode("\x1a"))
	sio.flush()
	while True:
		line = ser.readline().rstrip()
		if (line.startswith("ERROR")):	
			raise Exception("Got error sending SMS")
		if (line.startswith("OK")):
			break

def pollSMS():
	result = doCommand("AT+CMGL=\"ALL\"")
	result = result[1:-1] # remove the echo and the OK
	out = []
	for i in range(0, len(result)/2):
		infoLine = result[2 * i]
		textLine = result[2 * i + 1]
		# Why does this comparison fail?
		#if (not infoLine.startswith("+CGML:")):
		#	raise Exception("Unexpected CGML response line")
		infoLine = infoLine[7:]
		infos = csv.reader([infoLine], delimiter=',', quotechar='"').next()
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
		sendSMS(text['sender'], msg)
		msg = msg[1:]
		if (msg == 'STATUS'):
			sendSMS(text['sender'], "Ready.")
		if (msg == 'REBOOT'):
			doReboot()
			sendSMS(text['sender'], "Reboot performed.")
	time.sleep(15)
