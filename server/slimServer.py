import socket, sys
import select
import matplotlib.pyplot as plt
import struct
import ctypes
import numpy as np
import mmap
import os
import time
import v
from config64 import *
import subprocess


def connectToFPGAs():
	dd = time.asctime().split(' ')
	fname = '{}{}{}{}'.format('ips',dd[4],dd[1],dd[2])
	libc = ctypes.CDLL('libc.so.6')
	#~ dummy = subprocess.check_output("bash getIPs_sshIntoFPGAs.sh", shell=True)

	if os.path.isfile(fname):
		dummy = subprocess.check_output("bash sshIntoFPGAs.sh", shell=True)	
	else:	
		cmdstr = '{}{}'.format('touch ', fname)	
		os.system(cmdstr)
		dummy = subprocess.check_output("bash getIPs_sshIntoFPGAs.sh", shell=True)


connectToFPGAs()

ports = []
boards = []

cmsg = struct.Struct('= 4i')

MAX_DATA_LEN = 8191


class UserShell():
	
	def setTrigDelay(self,trigDelay):
		self.trigDelay = trigDelay
		td = int(trigDelay*HPSCLOCK)
		msg = struct.pack('iiii',0,td,0,0)
		self.psock.send(msg)
	
		
	def setRecLen(self,recLen):
		if recLen > 1 and recLen < MAX_DATA_LEN:
			self.recLen = recLen
			self.recvBuffer = 8*self.recLen*np.dtype(np.uint8).itemsize
			msg = struct.pack('iiii',1,self.recLen,0,0)
			self.psock.send(msg)
		else:
			print 'invalid recLen, setting to 1024'
			self.recLen = 1024
			self.recvBuffer = 8*self.recLen*np.dtype(np.uint8).itemsize
			msg = struct.pack('iiii',1,self.recLen,0,0)
			self.psock.send(msg)
				
	
	def changePollTime(self,polltime):
		if polltime > 99 and polltime < 10000000:
			self.polltime = polltime
			msg = struct.pack('iiii',2,polltime,0,0)
			self.psock.send(msg)
		else:
			self.polltime = 400
			print 'invalid value, valid range = 99 - 1e7 (us), setting to 400'
	
		
	def closeFPGAdataAcq(self):

		msg = struct.pack('iiii',3,0,0,0)
		self.psock.send(msg)
			
	
	def dataAcqGo(self):
		msg = struct.pack('iiii',4,1,0,0)
		self.psock.send(msg)
	
	
	def dataTimerStart(self):
		msg = struct.pack('iiii',6,0,0,0)
		self.psock.send(msg)
	
		
	def dataTimerEnd(self):
		t1 = 0
		while t1 == 0:
			msg = struct.pack('iiii',7,0,0,0)
			self.psock.send(msg)
			tmpmsg = self.psock.recv(16,socket.MSG_WAITALL)
			inmsg = np.array(cmsg.unpack(tmpmsg),dtype=np.int)
			t1 = inmsg[1]
			
		return inmsg[0]
	
		
	def dataAcqStop(self):
		msg = struct.pack('iiii',4,0,0,0)
		self.psock.send(msg)
	
				
	def closeProgramAndSaveData(self):
		msg = struct.pack('iiii',8,0,0,0)
		self.psock.send(msg)
	
	
	def set_idx1len(self,idx1len):
		if idx1len >= 1:
			self.idx1len = idx1len
			msg = struct.pack('iiii',9,self.idx1len,0,0)
			self.psock.send(msg)
		else:
			print 'invalid idx1len, setting to 1'
			self.idx1len = 1
			msg = struct.pack('iiii',9,self.idx1len,0,0)
			self.psock.send(msg)
	
			
	def set_idx2len(self,idx2len):
		if idx2len >= 1:
			self.idx2len = idx2len
			msg = struct.pack('iiii',10,self.idx2len,0,0)
			self.psock.send(msg)
		else:
			print 'invalid idx2len, setting to 1'
			self.idx2len = 1
			msg = struct.pack('iiii',10,self.idx2len,0,0)
			self.psock.send(msg)

	
	def set_idx3len(self,idx3len):
		if idx3len >= 1:
			self.idx3len = idx3len
			msg = struct.pack('iiii',11,self.idx3len,0,0)
			self.psock.send(msg)
		else:
			print 'invalid idx3len, setting to 1'
			self.idx3len = 1
			msg = struct.pack('iiii',11,self.idx3len,0,0)
			self.psock.send(msg)

	
	def initializeDataStorage(self,ds):
		if ds == 1 or ds == 0:
			self.dataStorage = ds
			msg = struct.pack('iiii',12,self.dataStorage,0,0)
			self.psock.send(msg)
		else:
			print 'invalid nlocs, turning off datastorage'
			self.dataStorage = 0
			msg = struct.pack('iiii',12,self.dataStorage,0,0)
			self.psock.send(msg)
	
			
	def declareIdxs(self,idx1n,idx2n,idx3n):
		
		if idx1n>=0 and idx1n<self.idx1len and idx2n>=0 and idx2n<self.idx2len and idx3n>=0 and idx3n<self.idx3len:
			self.idx1n = idx1n
			self.idx2n = idx2n
			self.idx3n = idx3n
			msg = struct.pack('iiii',13,self.idx1n,self.idx2n,self.idx3n)
			self.psock.send(msg)
		else:
			print 'invalid idxNs, setting all to 0, prepare to overwrite data'
			self.idx1n = 0
			self.idx2n = 0
			self.idx3n = 0
			msg = struct.pack('iiii',13,self.idx1n,self.idx2n,self.idx3n)
			self.psock.send(msg)
		
			
	def __init__(self,psock):
		self.psock = psock
		self.trigDelay = 0
		self.recLen = 1024
		self.polltime = 400
		self.idx1len = 1
		self.idx2len = 1
		self.idx3len = 1
		self.dataStorage = 0
		self.idx1n = 0
		self.idx2n = 0
		self.idx3n = 0


class DataServer():
	
		
	def setupSockets(self,csock,dataPort):
		cliSock,addr = [],[]
		cliSock.append(csock)
		addr.append(csock)
		
		dataSock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		dataSock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
		dataSock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
		dataSock.bind((HOST,dataPort))
		dataSock.listen(30)
		if os.path.isfile("ipList"):
			os.system("rm ipList")
			
		os.system("touch ipList")
		
		while len(cliSock)<NFPGAS+1:
			tmpsock, tmpaddr = dataSock.accept()
			datatmp = tmpsock.recv(16,socket.MSG_WAITALL)
			data = np.array(cmsg.unpack(datatmp),dtype=np.int)
			newport = int(data[0])+BASE_PORT
			tmpsock.send(struct.pack('iiii',newport,0,0,0))
			
			newdataSock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
			newdataSock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
			newdataSock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
			newdataSock.bind((HOST,newport))
			newdataSock.listen(1)
			
			tmpsock.send(struct.pack('iiii',newport,0,0,0))
			tmpsock.close()
			
			csock, caddr = newdataSock.accept()
			cliSock.append(csock)
			addr.append(caddr[0])
			boards.append(newport-BASE_PORT)
			ports.append(int(caddr[0].split('.')[3]))
			cmdstr = '{}{}{}'.format('echo ',caddr[0], ' >> ipList')	
			os.system(cmdstr)
		
		dataSock.close()
		for nn in range(0,NFPGAS):
			print boards[nn],ports[nn]
		return cliSock, addr
	
	
	def DataAcqFork(self):
					
		dataSock, sockAddr = self.setupSockets(self.csock, BASE_PORT) # connect to fpgas
		cntArray = np.zeros(NFPGAS,dtype='int')

		lithiumRunner = 1
		dataGo = 0
		
		while lithiumRunner == 1:
					
			readableSocks,wrSock,erSock = select.select(dataSock, [], [])
			nreadable=len(readableSocks)
							
			for rdsock in readableSocks:
				sidx = dataSock.index(rdsock)

				if sidx == 0: # message from parent
					tmpmsg = self.csock.recv(16,socket.MSG_WAITALL)
					inmsg = np.array(cmsg.unpack(tmpmsg),dtype=np.int)
					
					if inmsg[0] == 0: # set trigDelay
						if inmsg[1] >=0 and inmsg[1] < 1e3:
							self.trigDelay = inmsg[1]
						else:
							print 'invalid trigDelay, setting to 0'
							self.trigDelay = 0
					
						msg = struct.pack('iiii',inmsg[0],self.trigDelay,0,0)
						for nds in range(1,len(dataSock)):
							dataSock[nds].send(msg)
							
					elif inmsg[0] == 1: # set recLen
						if inmsg[1] > 0 and inmsg[1] <= MAX_DATA_LEN:
							self.recLen = inmsg[1]
							self.CDATASTRUCT = struct.Struct('{}{}{}'.format('= ',2*self.recLen,'L')) 
							self.recvBuffer = 2*self.recLen*np.dtype(np.uint32).itemsize
						else:
							print 'invalid recLen, setting to 1024'
							self.recLen = 1024
							self.CDATASTRUCT = struct.Struct('{}{}{}'.format('= ',2*self.recLen,'L')) 
							self.recvBuffer = 2*self.recLen*np.dtype(np.uint32).itemsize
							
						msg = struct.pack('iiii',inmsg[0],self.recLen,0,0)
						for nds in range(1,len(dataSock)):
							dataSock[nds].send(msg)	
					
					elif inmsg[0] == 2: # set timeout for how often to check if data is available to transmit
						if inmsg[1] > 99 and inmsg[1] <= 10e6:
							msg = struct.pack('iiii',inmsg[0],inmsg[1],0,0)
							
						else:
							print 'invalid polltime, setting to 500us'
							msg = struct.pack('iiii',inmsg[0],500,0,0)
									
						for nds in range(1,len(dataSock)):
							dataSock[nds].send(msg)	
								
					elif inmsg[0] == 3: # close ethernet connection/HPS-FPGA
						msg = struct.pack('iiii',inmsg[0],inmsg[1],inmsg[2],inmsg[3])
						for nds in range(1,len(dataSock)):
							dataSock[nds].send(msg)
		
					elif inmsg[0] == 4: # set dataAcq state on HPS
						dataGo = inmsg[1]
						if inmsg[2] == 0: # reset the pulse counter
							cntArray = np.zeros(NFPGAS,dtype='int')	
						msg = struct.pack('iiii',inmsg[0],dataGo,0,0)	
						for nds in range(1,len(dataSock)):
							dataSock[nds].send(msg)
							
					elif inmsg[0] == 6: # start timer
						self.t0 = time.time()
							
					elif inmsg[0] == 7: # end timer
						if self.t1>0:
							msg = struct.pack('iiii',int((self.t1-self.t0)*1e6),1,0,0)
						else:
							msg = struct.pack('iiii',0,0,0,0)
						self.csock.send(msg)
						if self.t1>0:
							self.t1 = 0
							self.t0 = 0
						
					elif inmsg[0] == 8: # this is where you change the filename for saving
						lithiumRunner = 0
						if self.dataStorage == 1:
							fname = '{}{}{}'.format('../acquiredData/data',int(time.time()),'.npy')
							np.save(fname,self.data)
								
					elif inmsg[0] == 9: # set size of dimension 1 in storage array
						self.idx1len = inmsg[1]
						
					elif inmsg[0] == 10: # set size of dimension 2 in storage array
						self.idx2len = inmsg[1]
					
					elif inmsg[0] == 11: # set size of dimension 3 in storage array
						self.idx3len = inmsg[1]
							
					elif inmsg[0] == 12: # initialize storage array
						self.dataStorage = inmsg[1]
						if self.dataStorage == 1:
							self.data = np.zeros((self.idx1len,self.idx2len,self.idx3len,self.recLen,NFPGAS*CHANNELS_PER_BOARD))
							
					elif inmsg[0] == 13: # declare indices in storage array to put data
						self.idx1n = inmsg[1]
						self.idx2n = inmsg[2]
						self.idx3n = inmsg[3]
						
					else:
						lithiumRunner = 0
						dataGo = 0
						break
							
				elif sidx in range(1,len(dataSock)): # message/data from FPGA
					if dataGo == 1:
						dataTmp = dataSock[sidx].recv(self.recvBuffer,socket.MSG_WAITALL)
						data = np.array(self.CDATASTRUCT.unpack(dataTmp),dtype=np.uint32)
						if self.dataStorage == 1:
							c18 = data.view(dtype=self.dt14)
							c1,c2,c3,c4 = c18['c1'],c18['c2'],c18['c3'],c18['c4']
							
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8] = c1[0:self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+1] = c2[0:self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+2] = c3[0:self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+3] = c4[0:self.recLen]
							
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+4] = c1[self.recLen:2*self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+5] = c2[self.recLen:2*self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+6] = c3[self.recLen:2*self.recLen]
							self.data[self.idx1n,self.idx2n,self.idx3n,:,(boards[(sidx-1)]-1)*8+7] = c4[self.recLen:2*self.recLen]
						
						cntArray[sidx-1]+=1
												
				else: # ghosts
					for sock in dataSock:
						sock.close()
					os._exit(0)
			
			
			if np.all(cntArray>self.lcount) and np.mod(np.sum(cntArray),len(cntArray)) == 0:
				self.t1 = time.time()
				self.lcount = np.max(cntArray)
				
			
		
		dataGo = 0
		
		msg = struct.pack('iiii',3,0,0,0)			
		for nds in range(0,len(dataSock)):
			if nds > 0:
				dataSock[nds].send(msg)
			dataSock[nds].close()
		
		return 'You have committed filicide... Cheers!'	
		
									
	def __init__(self,csock):
		self.dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
		
		self.lcount = 0
		self.csock = csock
		self.comms = []
		self.runner = 1
		
		self.trigDelay = 0
		self.recLen = 1024
		self.CDATASTRUCT = struct.Struct('{}{}{}'.format('= ',2*self.recLen,'L')) 
		self.recvBuffer = 2*self.recLen*np.dtype(np.uint32).itemsize
		
		self.dataStorage = 0
		self.idx1len = 1
		self.idx2len = 1
		self.idx3len = 1
		self.idx1n = 0
		self.idx2n = 0
		self.idx3n = 0
		
		
		self.t0,self.t1 = 0, 0


psock,csock = socket.socketpair(socket.AF_UNIX,socket.SOCK_STREAM)
pid = os.fork()

if pid == 0:
	psock.close()
	ds = DataServer(csock)
	ds.DataAcqFork()
	os._exit(0)
	
csock.close()
time.sleep(1)
ms = UserShell(psock)


# variables for data acquisition
trigDelay = 0
recLen = 1024

ms.setTrigDelay(trigDelay) # when to start data acq after input trigger (us)
ms.changePollTime(500) # don't worry about it and don't change it
ms.setRecLen(recLen) # size of data to acquire after each trigger (duration of acquisition = N/20))

v.uploadDelays()
v.uploadCommands()

NLOCS = 10
NPPL = 10

# variables for data storage 
idx1len = NPPL # N pulses per loc
idx2len = NLOCS # N locs
idx3len = 1 # N chargetimes

ms.set_idx1len(idx1len)
ms.set_idx2len(idx2len)
ms.set_idx3len(idx3len)

ms.initializeDataStorage(1)
ms.dataAcqGo()

idx1n,idx2n,idx3n = 0,0,0
for idx1n in range(0,idx1len):
	for idx2n in range(0,idx2len):
		for idx3n in range(0,idx3len):
			ms.declareIdxs(idx1n,idx2n,idx3n)
			v.pulseAtLoc(idx1n)
			time.sleep(0.5)

		
ms.closeProgramAndSaveData()
		
raw_input("dataAcq done press enter to kill it dead...\n")

pid = os.getpid()
pgid = os.getpgid(pid)
os.killpg(pgid,9)



