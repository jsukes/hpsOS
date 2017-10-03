import socket, select
import struct
import time

class comServer:
	
	def setTrigDelay(self,td):
		self.td = td
		msg = struct.pack(self.cmsg,0,td,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def setRecLen(self,rl):
		self.rl = rl
		msg = struct.pack(self.cmsg,1,rl,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def setTimeout(self,to):
		self.to = to
		msg = struct.pack(self.cmsg,2,to,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)

	def setDataAcqMode(self,dam):
		self.dam = dam
		msg = struct.pack(self.cmsg,3,dam,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def setDataAlloc(l1,l2,l3):
		self.l1, self.l2, self.l3 = l1,l2,l3
		msg = struct.pack(self.cmsg,4,l1,l2,l3,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def allocMem(self):
		msg = struct.pack(self.cmsg,5,1,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def dataAcqStart(self,da):
		self.da = da
		msg = struct.pack(self.cmsg,6,da,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)

	def setIDX(self,id1,id2,id3):
		self.id1, self.id2, self.id3 = id1,id2,id3
		msg = struct.pack(self.cmsg,7,id1,id2,id3,"")
		self.ff.write(msg)
		
	def closeFPGA(self):
		msg = struct.pack(self.cmsg,8,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def resetVars(self):
		self.rl, self.td, self.to = 1024, 0, 500
		self.id1, self.id2, self.id3 = 0, 0, 0
		self.l1, self.l2, self.l3 = 0, 0, 0
		self.da, self.dam = 0, 0
		msg = struct.pack(self.cmsg,9,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)
		
	def saveData(self,fname):
		self.fname = fname
		msg = struct.pack(self.cmsg,10,0,0,0,fname)
		self.ff.write(msg)
		time.sleep(0.01)
		
	def shutdown(self):
		msg = struct.pack(self.cmsg,17,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.01)

	def connectIPC(self):
		self.ff = open("data_pipe","w",0)
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
		
	def __init__(self):
		self.IPCSOCK = "/home/jonathan/Desktop/hpsOS/workingVersion/server/lithium_ipc"
		self.rl, self.td, self.to = 1024, 0, 500
		self.id1, self.id2, self.id3 = 0, 0, 0
		self.l1, self.l2, self.l3 = 0, 0, 0
		self.da, self.dam = 0, 0
		self.fname = "dummy"
		self.cmsg = '4i100s'

