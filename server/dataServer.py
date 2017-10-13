
import socket
import struct
import time
import numpy as np
from dataServerParamConfig import *

class dataServer():

	def setTrigDelay(self,td):
		# sets the trigger delay on the fpgas, takes integer 'td' as input, units = us
		if (td >= 0) and (td <= TRIG_DELAY_MAX):
			self.trigDelay = td
		else:
			print 'Invalid trig delay value. [ Valid range = 0-1000us ]'
			self.trigDelay = 0
			
		msg = struct.pack(self.cmsg,0,self.trigDelay,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def setRecLen(self,rl):
		# sets the number of points to collect per acquisition. takes integer 'rl' as input, unitless
		# the acquisition time window = [rl/20] us
		if (rl > 0) and (rl < REC_LEN_MAX):
			self.recLen = rl
		else:
			print 'Invalid Record Length. [ Valid range = 1-8191 ]'
			self.recLen = 2048
			
		msg = struct.pack(self.cmsg,1,self.recLen,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		if self.recLen < self.packetSize:
			self.packetSize = self.recLen
			msg = struct.pack(self.cmsg,11,self.packetSize,0,0,"")
			self.ff.write(msg)
			print 'Record Length less than previous packet size, packet size set to record length (', self.packetSize,')'
			time.sleep(0.05)
			
	def setENETPacketSize(self,ps):
		# sets the size of the packets to be sent over ethernet from the socs. takes integer 'ps' as input, units = bytes. should be a power of two, must be an even divisor of the record length.
		if (ps > 0) and (ps <= self.recLen):
			self.packetSize = ps
		else:
			if self.recLen >= 512:
				self.packetSize = 512
			else:
				self.packetSize = self.recLen
			print 'Invalid packet size, setting packet size equal to record length (',self.packetSize,'). [ Valid range = 1 - record length ]'
			
		msg = struct.pack(self.cmsg,11,self.packetSize,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
			
	def setSocTransReadyTimeout(self,to):
		# sets how often the SoCs check whether they have data ready to transmit. takes integer 'to' as input, units = us. lower values make the SoC check more often but put it into more of a busy wait state which burns cpu time. should be set as high as possible without slowing down the program.
		if (to>=TRANS_READY_TIMEOUT_MIN):
			self.timeOut = to
		else:
			self.timeOut = 1e3
			print 'Invalid Timeout value, defaulting to 1000us. [ Valid values >= 10us ]'
		msg = struct.pack(self.cmsg,2,self.timeOut,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def setDataArraySize(self,l1,l2,l3):
		# used to define the amount of data that will be collected during the experiment. takes integer values 'l1','l2', and 'l3' as input, unitless. memory must be allocated in the cServer before data acquisition begins. data is stored in a 5D array of size [l1,l2,l3,recLen,elementN], these values give the size of the first 3 dimensions of the data array.
		if ( l1 > 0 ) and ( l2 > 0 ) and ( l3 > 0 ):
			self.l1, self.l2, self.l3 = l1,l2,l3
		else:
			self.l1, self.l2, self.l3 = 1,1,1
			print 'Invalid data size. All values must be > 0, setting all equal to 1'
		msg = struct.pack(self.cmsg,4,self.l1,self.l2,self.l3,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def allocateDataArrayMemory(self):
		# once all of the data size variables have been set, this tells the cServer to allocate the memory for the data to be acquired
		msg = struct.pack(self.cmsg,5,1,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def toggleDataAcq(self,da):
		# this tells the cServer/SoCs whether or not to acquire data. takes integer 'da' as input
		# da = 0 -> don't acquire data
		# da = 1 -> acquire data
		if (da == 0) or (da == 1):
			self.da = da
		else:
			self.da = 0
			print 'Invalid dataAcqStart value. Must be 0 (dataAcq = off) or 1 (dataAcq = on). Defaulting to 0 (off)'
		msg = struct.pack(self.cmsg,6,self.da,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)

	def declareDataAcqIdx(self,id1,id2,id3):
		# this tells the cServer where to store the incoming data after each pulse, needs to be set explicitly before each pulse. takes integer values 'id1','id2' and 'id3' as inputs, which are the array array indices into which the cServer will put the acquired data. eg, if your experiment were steering to N locations, treating each location with M pulses, and using K different charge times, you would tell the cServer, "this pulse corresponds to location 'n', pulse 'm', charge time 'k'", and it would put that data into the array at the corresponding location. if you don't set this variable each time, the cServer will just overwrite the values stored memory location [0,0,0] after each pulse
		if (id1 >= 0) and (id1 < self.l1) and (id2 >= 0) and (id2 < self.l2) and (id3 >= 0) and (id3 < self.l3):
			self.id1,self.id2,self.id3 = id1,id2,id3
			msg = struct.pack(self.cmsg,7,id1,id2,id3,"")
			self.ff.write(msg)
		else:
			print 'Invalid index value detected, turning off data acquisition. Valid index value ranges -> idx1[ 0 -',self.l1,'], idx2[ 0 -',self.l2,'], idx3[ 0 -',self.l3,'].\nValues detected -> [ idx1 =',id1,'], [ idx2 =',id2,'], [ idx3 =',id3,']'
			msg = struct.pack(self.cmsg,6,0,0,0,"")
			self.ff.write(msg)
			time.sleep(0.05)
						
	def closeFPGA(self): 
		# shuts down the SoCs
		msg = struct.pack(self.cmsg,8,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def resetVars(self):
		# this functions restores the cServer and SoCs to their default states
		msg = struct.pack(self.cmsg,9,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def saveData(self,fname):
		# this function tells the cServer to save the acquired data into a binary file named 'fname'. takes string 'fname' as input, 'fname' must be less than or equal to 100 character long
		msg = struct.pack(self.cmsg,10,0,0,0,fname)
		self.ff.write(msg)
		time.sleep(0.05)
		
	def getData(self,recvBoards):
		# this function tells the cServer to transfer the last set of acquired data directly to the python server for the user to do with what they want. takes integer value 'recvBoards' as the input, 'recvBoards' is an integer value corresponding the number of boards currently acquiring data. this function returns a binary array containing the data to the user. if 'recvBoards' is greater than the number of connected SoCs, the program will hang. 
		if (recvBoards > 0):
			msg = struct.pack(self.cmsg,12,0,0,0,"")
			self.ff.write(msg)
			return self.ipcsock.recv(2*self.recLen*recvBoards*np.dtype(np.uint32).itemsize,socket.MSG_WAITALL)
		else:
			print 'Invalid number of boards to getData from. Must be greater than zero. Since this function is supposed to return something, you probably have to restart the python server'
	
	def setChannelMask(self,cmask,maskState):
		'''tells the SoCs to acquire data only from channels not under the mask, and whether or not to transmit that data to the cServer after a given pulse. takes integer array 'cmask' and integer value 'maskState' as inputs. 
		
		- cmask is an array of 1's and 0's with a length equal to the number of receiving elements currently connected to the array. a value of 1 in cmask tells the cServer to acquire data from an element, a value of 0 tells it not to. each element of cmask masks or unmask the receiving element with the corresponding index in the cmask array. 
		
		- maskState is an integer which tells the cServer/SoCs what to with the masks
			maskState = 0, no mask set, transmit acquired data to the cServer after every pulse
			maskState = 1, mask set, DO NOT transmit data after acquisition event
			maskState = 2, mask set, but this is the last acquisition event under the mask, DO transmit after trigger (use for last pulse in sequence)
			
		example use case:
		1) in maskState 1, element zero is unmasked and all other elements are masked. upon triggering, the SoC will only acquire data from element zero and not transmit the acquired data to the cServer. 
		2) still in maskState 1, element one is unmasked and all other elements are masked. upon triggering, the SoC will only acquire data from element one and not transmit the acquired data. the SoC now has data from pulse one, element zero and pulse two, element one stored in it.
		3) now in maskState 2, elements two throught N are unmasked and elements zero and one are masked. upon triggering, the SoC will acquired data from elements two through N AND transmit the acquired data to the cServer. the acquired data will contain the signal from element zero from the first pulse, the signal from element one from the second pulse, and the signals from elements two through N from the third pulse.
		
		This lets you acquire data from multiple pulses without transfering it over ethernet to the cServer after each one, but it doesn't let you acquire multiple pulses from all elements simultaneously without masking others. That feature may or may not be built in in the future, but I'm not going to bother with it now because it would require a bigger rewrite of the code base than this solution did because of potential memory allocation/reallocation issues. It might also not be necessary if the slow bridge issue ever gets solved, but even if it doesn't the system is fast enough now to mostly get around it'''
		
		for boardN in range(0,RECVBOARDS):
			m14,m58,mbn = 0,0,0
			for m in range(0,4):
				if cmask[boardN*8+m]>0:
					m14 |= (0xff << m*8)
				if cmask[boardN*8+m+4]>0:
					m58 |= (0xff << m*8)
			if maskState != 0:
				mbn |= (0xff << 24)
			mbn |= maskState
			mbn |= (boardN << 8)
					
			msg = struct.pack(self.cmsg,13,m14,m58,mbn,"")
			self.ff.write(msg)
		
		self.ipcWait()
		
	def ipcWait(self):
		# tells the python server to wait for confirmation from the cServer that all data has been received from the SoCs
		return self.ipcsock.recv(4,socket.MSG_WAITALL)
	
	def shutdown(self):
		# shuts down the SoCs and C server
		self.ipcsock.close()
		msg = struct.pack(self.cmsg,17,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)

	def disconnect(self): 
		# disconnect from the C server but leave it running
		self.ipcsock.close()
		self.ff.close()
		time.sleep(0.05)
		
	def connect(self): 
		# connect to C server, python will wait until cServer is running, blocking user interactions
		self.ff = open(self.IPCFIFO,"w",0)
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
	
	def __init__(self):	
		# class intialization for the python data server 
		
		self.IPCSOCK = "./lithium_ipc"	# name of the ipc socket to connect to the cServer through
		self.IPCFIFO = "data_pipe"		# name of the FIFO to connect to the cServer through
		self.cmsg = '4I100s'			# tells python how to package message to send to the cServer -> [4*(unsinged 32-bit int), string (up to 100 characters)]
		
		# default values of data acquisition variables
		self.trigDelay = 0
		self.packetSize = 512
		self.da = 0
		self.dataAcqMode = 0
		self.timeOut = 1e3
		self.recLen = 2048
		self.id1,self.id2,self.id3 = 0,0,0
		self.l1, self.l2, self.l3 = 1,1,1
		

	
	





