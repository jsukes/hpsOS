
import socket
import struct
import time
import numpy as np
from dataServerParamConfig import *

class dataServer():

	def setTrigDelay(self,td):
		if (td >= 0) and (td <= TRIG_DELAY_MAX):
			self.trigDelay = td
		else:
			print 'Invalid trig delay value. [ Valid range = 0-1000us ]'
			self.trigDelay = 0
			
		msg = struct.pack(self.cmsg,0,self.trigDelay,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def setRecLen(self,rl):
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
			
	def setPacketSize(self,ps):
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
			
	def setTimeout(self,to):
		if (to>=TRANS_READY_TIMEOUT_MIN):
			self.timeOut = to
		else:
			self.timeOut = 1e3
			print 'Invalid Timeout value, defaulting to 1000us. [ Valid values >= 100us ]'
		msg = struct.pack(self.cmsg,2,self.timeOut,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)

	def setDataAcqMode(self,dam):
		if (dam == 0) or (dam == 1):
			self.dataAcqMode = dam
		else:
			self.dataAcqMode = 0
			print 'Invalid Data Acquisition Mode, defaulting to 0. [ Must be 0 or 1 ]. May cause run-time problems, proceed with caution.'
			
		msg = struct.pack(self.cmsg,3,self.dataAcqMode,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def setDataSize(self,l1,l2,l3):
		if ( l1 > 0 ) and ( l2 > 0 ) and ( l3 > 0 ):
			self.l1, self.l2, self.l3 = l1,l2,l3
		else:
			self.l1, self.l2, self.l3 = 1,1,1
			print 'Invalid data size. All values must be > 0, setting all equal to 1'
		msg = struct.pack(self.cmsg,4,self.l1,self.l2,self.l3,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def allocateMemory(self):
		msg = struct.pack(self.cmsg,5,1,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def dataAcqStart(self,da):
		if (da == 0) or (da == 1):
			self.da = da
		else:
			self.da = 0
			print 'Invalid dataAcqStart value. Must be 0 (dataAcq = off) or 1 (dataAcq = on). Defaulting to 0 (off)'
		msg = struct.pack(self.cmsg,6,self.da,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)

	def setAcqIdx(self,id1,id2,id3):
		if (id1 >= 0) and (id1 < self.l1) and (id2 >= 0) and (id2 < self.l2) and (id3 >= 0) and (id3 < self.l3):
			self.id1,self.id2,self.id3 = id1,id2,id3
			msg = struct.pack(self.cmsg,7,id1,id2,id3,"")
			self.ff.write(msg)
		else:
			print 'Invalid index value detected, turning off data acquisition. Valid index value ranges -> idx1[ 0 -',self.l1,'], idx2[ 0 -',self.l2,'], idx3[ 0 -',self.l3,'].\nValues detected -> [ idx1 =',id1,'], [ idx2 =',id2,'], [ idx3 =',id3,']'
			msg = struct.pack(self.cmsg,6,0,0,0,"")
			self.ff.write(msg)
			time.sleep(0.05)
						
	def closeFPGA(self): # use this to shutdown just the FPGAs
		msg = struct.pack(self.cmsg,8,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def resetVars(self):
		msg = struct.pack(self.cmsg,9,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def saveData(self,fname):
		msg = struct.pack(self.cmsg,10,0,0,0,fname)
		self.ff.write(msg)
		time.sleep(0.05)
		
	def getData(self,recvBoards):
		if (recvBoards > 0):
			msg = struct.pack(self.cmsg,12,0,0,0,"")
			self.ff.write(msg)
			return self.ipcsock.recv(2*self.recLen*recvBoards*np.dtype(np.uint32).itemsize,socket.MSG_WAITALL)
		else:
			print 'Invalid number of boards to getData from. Must be greater than zero. Since this function is supposed to return something, you probably have to restart the python server'
	
	def ipcWait(self):
		return self.ipcsock.recv(4,socket.MSG_WAITALL)
	
	def shutdown(self): # shutdown FPGAs and C server
		self.ipcsock.close()
		msg = struct.pack(self.cmsg,17,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)

	def disconnect(self): # disconnect from the C server but leave it running
		self.ipcsock.close()
		self.ff.close()
		time.sleep(0.05)
		
	def connect(self): # connect to C server, blocks until C server is running 
		self.ff = open(self.IPCFIFO,"w",0)
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
	
	def __init__(self):	
		self.IPCSOCK = "./lithium_ipc"
		self.IPCFIFO = "data_pipe"
		self.cmsg = '4i100s'
		self.trigDelay = 0
		self.packetSize = 512
		self.da = 0
		self.dataAcqMode = 0
		self.timeOut = 1e3
		self.recLen = 2048
		self.id1,self.id2,self.id3 = 0,0,0
		self.l1, self.l2, self.l3 = 1,1,1
		

	
	





