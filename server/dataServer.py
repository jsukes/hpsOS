

import socket
import struct
import time
import select
import numpy as np
from dataServerParamConfig import *
import sysv_ipc


class dataServer():

	def setTrigDelay(self,td):
		# sets the trigger delay on the fpgas, takes integer 'td' as input, units = us
		if (td >= 0) and (td <= TRIG_DELAY_MAX):
			self.trigDelay = int(td)
		else:
			print 'Invalid trig delay value. [ Valid range = 0-1000us ]'
			self.trigDelay = 0
			
		msg = struct.pack(self.cmsg,0,self.trigDelay,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def setRecLen(self,rl,ps=0):
		# sets the number of data points to collect per acquisition, NOT the time duration of acquisition. takes integer 'rl' as input, unitless
		# the acquisition time window = [rl/20] us
		if ( rl >= MIN_PACKETSIZE ) and (rl <= REC_LEN_MAX):
			self.recLen = int(rl)
			if ( ps >= MIN_PACKETSIZE ) and (ps <= rl):
				self.packetsize = int(ps)
			else:
				ps = int(rl)
				if (ps != 0):
					print 'invalid packetsize, setting equal to recLen'	
				self.packetsize = int(ps)
				
		else:
			print 'Invalid Record Length. [ Valid range = 128-8192 ]. Setting recLen to 2048, packetsize to 2048'
			self.recLen = 2048
			self.packetsize = 2048
			
		msg = struct.pack(self.cmsg,1,self.recLen,self.packetsize,0,"")
		self.ipcsock.send(msg)
		#~ msg = struct.pack(self.cmsg,2,self.packetsize,0,0,"")
		#~ self.ipcsock.send(msg)
		time.sleep(0.05)
	
	def setInterleaveDepthAndTimer(self,interleaveDepth,interleaveTimeDelay,packetWait=0):
		if (interleaveDepth>0) and (interleaveTimeDelay<5000):
			self.interleaveDepth = interleaveDepth
			self.interleaveTimeDelay = interleaveTimeDelay
			self.packetWait = packetWait
			if packetWait<0 or packetWait>5000:
				self.packetWait = 0
			msg = struct.pack(self.cmsg,2,interleaveDepth,interleaveTimeDelay,packetWait,"")
		else:
			self.interleaveDepth = 1
			self.interleaveTimeDelay = 0
			print 'invalid modulo settings'		
			msg = struct.pack(self.cmsg,2,1,0,0,"")
			
		self.ipcsock.send(msg)
		time.sleep(0.05)
			
	def setDataArraySize(self,l1,l2,l3):
		# used to define the amount of data that will be collected during the experiment. takes integer values 'l1','l2', and 'l3' as input, unitless. memory must be allocated in the cServer before data acquisition begins. data is stored in a 5D array of size [l1,l2,l3,2*recLen,(number of receiving boards)], these values give the size of the first 3 dimensions of the data array in the cServer. 
		if ( l1 >= 0 ) and ( l2 >= 0 ) and ( l3 >= 0 ):
			self.l1 = ( int(l1) if l1>0 else 1 )
			self.l2 = ( int(l2) if l2>0 else 1 )
			self.l3 = ( int(l3) if l3>0 else 1 )
		else:
			self.l1, self.l2, self.l3 = 1,1,1
			print 'Error all indices must be greater than or equal to 0, setting all equal to 1'
		msg = struct.pack(self.cmsg,4,self.l1,self.l2,self.l3,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def allocateDataArrayMemory(self):
		# tells the cServer to allocate the memory for the data to be stored in based on the inputs from the 'setRecLen' and 'setDataArraySize' commands
		msg = struct.pack(self.cmsg,5,1,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def toggleDataAcq(self,da):
		# puts the cServer/SoCs into or out of a data acquisition state. takes integer 'da' as input
		# da = 0 --> don't acquire data
		# da = 1 --> acquire data
		if (da == 0) or (da == 1):
			self.da = int(da)
		else:
			self.da = 0
			print 'Invalid dataAcqStart value. Must be 0 (dataAcq = off) or 1 (dataAcq = on). Defaulting to 0 (off)'
		msg = struct.pack(self.cmsg,6,self.da,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)

	def declareDataAcqIdx(self,id1,id2,id3):
		# this tells the cServer where to store the incoming data after each pulse, needs to be set explicitly BEFORE each pulse. takes integer values 'id1','id2' and 'id3' as inputs, which are the array array indices into which the cServer will put the acquired data. eg, if your experiment were steering to N locations, treating each location with M pulses, and using K different charge times, you would tell the cServer, "the upcoming pulse corresponds to steering location 'n', pulse 'm', charge time 'k'" by issuing the command 'declareDataAcqIdx(n,m,k)', and it would put that data into the data array at that location. if you don't set this variable each time, the cServer will just overwrite the values stored memory location [0,0,0] after each pulse. Note C starts counting at 0, not 1, so the first index for all dimensions of the array is 0
		if (id1 >= 0) and (id1 < self.l1) and (id2 >= 0) and (id2 < self.l2) and (id3 >= 0) and (id3 < self.l3):
			self.id1,self.id2,self.id3 = int(id1),int(id2),int(id3)
			msg = struct.pack(self.cmsg,7,id1,id2,id3,"")
			self.ipcsock.send(msg)
		else:
			print 'Invalid index value detected, turning off data acquisition. Valid index value ranges -> idx1[ 0 -',self.l1,'], idx2[ 0 -',self.l2,'], idx3[ 0 -',self.l3,'].\nValues detected -> [ idx1 =',id1,'], [ idx2 =',id2,'], [ idx3 =',id3,']'
			msg = struct.pack(self.cmsg,7,0,0,0,"")
			self.ipcsock.send(msg)
			time.sleep(0.05)
						
	def closeFPGA(self): 
		# shuts down the program running on the SoCs but not the cServer.
		msg = struct.pack(self.cmsg,8,0,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def resetVars(self):
		# this function restores the variables in the cServer and SoCs to their default values
		msg = struct.pack(self.cmsg,9,0,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def saveData(self,fname):
		# this function tells the cServer to save the acquired data into a binary file named 'fname'. takes string 'fname' as input, 'fname' must be less than or equal to 100 characters long
		msg = struct.pack(self.cmsg,10,0,0,0,fname)
		self.ipcsock.send(msg)
		time.sleep(0.05)
	
	def readData(self):	
		# the '1' after the '11' tells the cServer to setup and share the memory region
		msg = struct.pack(self.cmsg,11,1,0,0,"")
		self.ipcsock.send(msg)
		
		if self.ipcWait(2) > 0:	
			# Create a shared memory object
			memory = sysv_ipc.SharedMemory(self.shmkey)
			
			# Read value from shared memory
			memory_value = memory.read()
			print len(memory_value)
			# the '0' after the '11' tells the cServer that we've acquired the data and that it can release it
			msg = struct.pack(self.cmsg,11,0,0,0,"")
			self.ipcsock.send(msg)
		
		return memory_value
			
	def queryBoardInfo(self):
		#~ self.ipcsock.recv(16,0)
		# gets the identifying numbers of the boards connected to the cServer 
		msg = struct.pack(self.cmsg,12,0,0,0,"")
		self.ipcsock.send(msg)
		
		while(1):
			bn = self.ipcWait(0.5)
			if bn == 0:
				break
			else:
				print 'connected to board:', struct.Struct('=I').unpack(bn)[0], struct.Struct('=I').unpack(bn)[0]%self.interleaveDepth
		
		msg = struct.pack(self.cmsg,13,1,0,0,"")
		self.ipcsock.send(msg)
		dummy = self.ipcsock.recv(64*4,socket.MSG_WAITALL)
		self.boardNums = np.array(struct.Struct('{}'.format('=64I')).unpack(dummy))
		self.boardCount = len(np.argwhere(self.boardNums>0))
	
	def setQueryDataTimeout(self,qdto=1000):
		# makes the socs check for data 
		if qdto > MIN_QUERY_DATA_TIMEOUT:
			self.queryDataTimeout = qdto		
		else:
			print 'Minimum Query Data Timeout = 1000us (setting to 10 ms)'
			self.queryDataTimeout = 10000
			
		msg = struct.pack(self.cmsg,15,self.queryDataTimeout,0,0,"")	
		self.ipcsock.send(msg)
					
	def queryData(self,nbd=0):
		# makes the socs check for data 
		msg = struct.pack(self.cmsg,16,nbd,0,0,"")
		self.ipcsock.send(msg)
			
	def shutdown(self):
		# shuts down the SoCs and C server
		msg = struct.pack(self.cmsg,17,0,0,0,"")
		self.ipcsock.send(msg)
		self.ipcsock.close()
		time.sleep(0.05)
		
	def disconnect(self): 
		# disconnect python from the C server but leave it running in the background so you can reconnect to it later
		self.ipcsock.close()
		time.sleep(0.05)
		
	def connect(self): 
		# connect to the C server. 
		# Note: python will block until it is connected to the cServer. if the python program gets 'stuck' before it begins it's likely because the cServer stopped running or crashed, just relaunch and python should connect to it. You don't have to restart python before (re)launching the cServer, but if it still hangs or returns an error restart them both. If it still doesn't work, delete the files 'data_pipe' and 'lithium_ipc' in the folder containing the cServer and restart the cServer and python program again. 
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
		time.sleep(0.1)
	
	def ipcWait(self,to=None):
		# tells python to wait for confirmation from the cServer that all data has been received from the SoCs. used to synchronize transmit and receive systems
		# 'to' is an optional timeout argument that tells python that if no data arrives within the specified timeout, to return the value '0' and continue the regular running of the program
		if to != None:
			nready = select.select([self.ipcsock], [], [], to)
			if nready[0]:
				#~ print 'yoyoyo'
				dummy = self.ipcsock.recv(4, socket.MSG_WAITALL)
				return dummy
			else:
				print 'ipcWait timed out'
				return 0
		else:
			dummy = self.ipcsock.recv(4, socket.MSG_WAITALL)
			#~ print 'wait2',len(dummy)
			return dummy
				
	def __init__(self):	
		# class intialization for the python data server 
		
		self.IPCSOCK = "./lithium_ipc"	# name of the ipc socket to connect to the cServer through
		#~ self.IPCFIFO = "data_pipe"		# name of the FIFO to connect to the cServer through
		self.cmsg = '4I100s'			# tells python how to package messages to send to the cServer -> [4*(unsinged 32-bit int), string (up to 100 characters)]
		self.shmkey = 1234
		# default values of data acquisition variables
		self.trigDelay = 0
		self.da = 0
		self.dataAcqMode = 0
		self.timeOut = 1e3
		self.recLen = 2048
		self.packetsize = 2048
		self.interleaveDepth = 1
		self.interleaveTimeDelay = 0
		self.packetWait = 0
		self.queryDataTimeout = 1000
		self.id1,self.id2,self.id3 = 0,0,0
		self.l1, self.l2, self.l3 = 1,1,1
		self.boardCount = 0
		self.boardNums = []
		

