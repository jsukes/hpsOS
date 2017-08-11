# Loads all functions including a commands, b commands, tests, and basic steering
# type "from varLoader import *" on startup of python using "sudo python"
#
# MaizeChipPy 1.0 - JJM - May 2017
import sys
import socket, select
import numpy as np
from fpgaClass import *
import scipy.io as sio
from math import pi
import matplotlib.pyplot as plt
import struct
import time
import os

IPCSOCK = "/home/jonathan/Desktop/hpsOS/workingVersion/server/lithium_ipc"
x = FPGA()
a = a_funcs(x)
b = b_funcs(x)
b.stop()
trigWidth = 5e-6
cmsg = '4i100s'


def uploadDelays():
	
	chargetime = 500*np.ones(112)
	tmp = sio.loadmat('delays_112.mat')					# load calibrated delays
	calibrationDelays = np.round(tmp['delays_FPGA']) 	# the argument here will change depending on what you named the variable in your matfile
	calibrationDelays.shape = (112) 					# reshape to match chargetime
	chargetime = chargetime + calibrationDelays
		
	for mother in range(0,3):
		b.select_motherboard(mother)
		chanset = np.asarray(range(0,32))+mother*32 
		b.set_chipmem_wloc(0)
		b.write_array_pattern_16bit(chargetime[chanset])
		
def CamTest(PRF,nPulses,camDelay_us,camFPS_Hz,camFrames):
	uploadDelays()
	camTrigDelay = camDelay_us*1e-6
	camFPS = float(1)/camFPS_Hz
	picoTrigDelay = 5e-6

	b.stop_execution()
	b.mask_off()
	
	b.set_imem_wloc(0)
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_amp(0)
	a.wait(1)
	a.set_phase(0)
	a.wait(1)
	
	### Send a few triggers to the camera to prime it ###
	a.start_loop(1,4)
	a.set_trig(2)
	a.waitsec(1e-6)
	a.set_trig(0)
	a.end_loop(1)
	
	### START OUTER LOOP ####
	a.start_loop(1,nPulses)
	a.fire(0)
	
	# fire pico and receiver
	a.set_trig(5)
	a.waitsec(picoTrigDelay)
	a.set_trig(0)
	
	a.waitsec(camTrigDelay-picoTrigDelay) # wait X seconds to trigger the camera
	
	#### start camera loop ####
	a.start_loop(2,camFrames)
	a.set_trig(2)
	a.waitsec(1e-6)
	a.set_trig(0)
	a.waitsec(camFPS)
	a.end_loop(2)
	#### end camera loop ####
	
	a.waitsec(float(1)/PRF - camFPS*camFrames - camTrigDelay - picoTrigDelay)
	
	a.end_loop(1)
	### END OUTER LOOP ###
	
	a.halt()
	
def CamRx(nPulses,camDelay_us,camFPS_Hz,camFrames):
	camTrigDelay = camDelay_us*1e-6
	camFPS = float(1)/camFPS_Hz
	picoTrigDelay = 5e-6

	b.stop_execution()
	b.mask_off()
	
	b.set_imem_wloc(0)
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_amp(0)
	a.wait(1)
	a.set_phase(0)
	a.wait(1)
	
	### Send a few triggers to the camera to prime it ###
	a.start_loop(1,4)
	a.set_trig(2)
	a.waitsec(1e-6)
	a.set_trig(0)
	a.end_loop(1)

	a.fire(0)
	
	# fire pico and receiver
	a.set_trig(5)
	a.waitsec(picoTrigDelay)
	a.set_trig(0)
	
	a.waitsec(camTrigDelay-picoTrigDelay) # wait X seconds to trigger the camera
	
	#### start camera loop ####
	a.start_loop(2,camFrames)
	a.set_trig(2)
	a.waitsec(1e-6)
	a.set_trig(0)
	a.waitsec(camFPS)
	a.end_loop(2)

	a.halt()
	
def uploadCommands():
	b.stop_execution()
	
	b.set_imem_wloc(0)
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_amp(0)
	a.wait(1)
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_phase(0)
	a.fire(0)
	
	a.set_trig(15)
	a.waitsec(5e-6)
	a.set_trig(0)
	
	a.halt()
	
uploadDelays()
CamRx(100,30,250000,75)

ff = open("data_pipe","w",0)
#~ try:
	#~ os.unlink(IPCSOCK)
#~ except OSError:
	#~ if os.path.exists(IPCSOCK):
		#~ raise
		

#~ rd = open("msg_pipe", "r")

def setTrigDelay(td):
	msg = struct.pack(cmsg,0,td,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def setRecLen(rl):
	msg = struct.pack(cmsg,1,rl,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def setTimeout(to):
	msg = struct.pack(cmsg,2,to,0,0,"")
	ff.write(msg)
	time.sleep(0.01)

def setDataAcqMode(dam):
	msg = struct.pack(cmsg,3,dam,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def setDataAlloc(l1,l2,l3):
	msg = struct.pack(cmsg,4,l1,l2,l3,"")
	ff.write(msg)
	time.sleep(0.01)
	
def allocMem():
	msg = struct.pack(cmsg,5,1,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def dataAcqStart(da):
	msg = struct.pack(cmsg,6,da,0,0,"")
	ff.write(msg)
	time.sleep(0.01)

def setIDX(id1,id2,id3):
	msg = struct.pack(cmsg,7,id1,id2,id3,"")
	ff.write(msg)
	
def closeFPGA():
	msg = struct.pack(cmsg,8,0,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def resetVars():
	msg = struct.pack(cmsg,9,0,0,0,"")
	ff.write(msg)
	time.sleep(0.01)
	
def saveData(fname):
	msg = struct.pack(cmsg,10,0,0,0,fname)
	ff.write(msg)
	time.sleep(0.01)
	
def shutdown():
	msg = struct.pack(cmsg,17,0,0,0,"")
	ff.write(msg)
	time.sleep(0.01)

def connectIPC():
	ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	ipcsock.connect(IPCSOCK)
	return ipcsock

ipcsock = connectIPC()
	
def goSafely(npulses,PRF):
	resetVars()
	
	setTrigDelay(0)
	setRecLen(1024)
	setTimeout(1000)
	setDataAlloc(1,1,npulses)
	allocMem()
	dataAcqStart(1)
	
	tt = np.zeros(npulses)
	for n in range(0,npulses):
		t0 = time.time()
		setIDX(0,0,n)

		b.go()
		time.sleep(1.0/PRF-0.00035)
		
		dummy = ipcsock.recv(4,socket.MSG_WAITALL)
		dt = (time.time()-t0)*1e3
		tt[np.mod(n,npulses)] = (time.time()-t0)
		
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print 'avg time: ',np.mean(tt)*1e3,'+-',np.std(tt)*1e3
	print 'PRF =', 1.0/np.mean(tt)
	print 'min/max times:', np.min(tt)*1e3,'  -  ',np.max(tt)*1e3
	time.sleep(1)
	setTimeout(5e6)
	saveData("ACE20170810_AGAR10_T10")


def plot():
	c = np.fromfile("hello",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	print np.min(d['c1']),np.max(d['c1'])
	plt.plot(d['c1'])
	plt.plot(d['c2']+256)
	plt.plot(d['c3']+512)
	plt.plot(d['c4']+768)
	plt.show()
