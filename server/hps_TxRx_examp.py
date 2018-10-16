from dataServer import *
import numpy as np
import matplotlib.pyplot as plt
import time
import subprocess
import scipy.io as sio
from math import pi


RESET_ARDUINO_TRIGS = 0x01 | 0x80
ENET_FIRE_ARDUINDO_TRIGS = 0x02
ARDUINO_TRIGS_READY = 0

d = dataServer()
d.connect()
time.sleep(1)
d.queryBoardInfo()

print 'N boards = ', d.boardCount
time.sleep(0.5)


# decalare the number of header pin state change events that are going to issued
# note: turning the outputs high and turning the outputs low are separate events, 
# ie every time you turn an output high then turn it low again it counts as two events
NTRIG_EVENTS = 3

# reset the list of header pin state values stored on the FPGA
d.setArdTrigComms( RESET_ARDUINO_TRIGS )

# tell the FPGA how many header pin state changes are going to be issued (see above)
d.setArdTrigNum(NTRIG_EVENTS) 

# some header pin states for testing
output_state1 = 0xff
output_state2 = 0x00

# populate the lists of header pin state values and their associated durations (see notes in dataServer.py)
for n in range(0,NTRIG_EVENTS):
	if n == 0:
		d.setArdTrigVal(n, 0x8000) 
		d.setArdTrigWait(n,1)
	else:	
		if (n%2) == 1:
			d.setArdTrigVal(n,output_state1)
			d.setArdTrigWait(n,1)
		else:
			d.setArdTrigVal(n,output_state2)
			d.setArdTrigWait(n,1000)	
d.setArdTrigVal(NTRIG_EVENTS,0) # turn off all the trigger outputs at the end
d.setArdTrigWait(NTRIG_EVENTS,1) # issue the necessary wait command for the last trigger
	
# tell the FPGA that you are done populating the list of header pin output states
d.setArdTrigComms( ARDUINO_TRIGS_READY )



# setup the data acquisition stuff
trigDelay = 1 # us
recLen = 512
packetsize = 512

d.setTrigDelay(trigDelay)
d.setRecLen(recLen,packetsize)
d.setInterleaveDepthAndTimer(4,300)

d.toggleDataAcq(1)
d.setDataArraySize(1,1,1)
d.allocateDataArrayMemory()

time.sleep(1)

for n in range(0,5000):
	# tell the FPGA to start changing the header pin states to issue triggers/fire array elements
	# note: every time you issue the fire command, the FPGA sequentially issues ALL triggers stored in elements 0-NTRIG_EVENTS of the output list.
	# once the FPGA begins issuing the triggers, it cannot be stopped. tread carefully.
	d.setArdTrigComms( ENET_FIRE_ARDUINDO_TRIGS )
	
	# receive system stuff		
	d.declareDataAcqIdx(0,0,0)
	d.queryData(1)

	d.ipcWait(2)
	time.sleep(0.5)


d.shutdown()








