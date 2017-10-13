
''' dataServer Class

	contains commands for interacting with the cServer to acquire data from the receiver boards
	
	notes: 
	
	- Runtime parameter constants are imported from 'dataServerParamConfig.py'. This file contains the values TRIG_DELAY_MAX, REC_LEN_MAX, TRANS_READY_TIMEOUT_MIN, and RECVBOARDS. These are things that are hardcoded into the SoCs/cServer that may change in the future depending on our needs, but which need to be enforced in the dataServer.
	
	- All inputs to functions within the dataServer class (except 'fname' in the 'saveData' function, which is a string with 100 characters or less) should be of the type uint32_t (unsigned 32-bit integers). All inputs are cast into this type before they are sent to the cServer in the 'struct.pack(...)' commands, this means any inputs with decimal values will be rounded down to the nearest whole number before they are sent to the cServer. 
	
	- Programming rules and recommendations:
		- Rule: You MUST connect to the cServer to interact with it and acquire data from the SoCs. Unless you don't want to collect data, issue the 'connect' command at the beginning of your function. 
		
		- Recommendation/Rule: Issue the 'disconnect' command at the end of your function (note: this leaves the cServer running and you can reconnect to it later with another call to 'connect'. If your experiment is completely over, issue the 'shutdown' command instead.) You can connect to the cServer multiple times from python, even without disconnecting, but it usually leads to issues because of how the FIFO and IPC socket handle things, wherein communications between the cServer and python breakdown and ultimately crash. There's no clear way for me to enforce this as a rule that would prevent the user from writing or executing a function that doesn't issue this command, and it won't break things if you only run your function once and then shutdown python itself.
		
		- Rule: DO NOT issue more than ONE trigger from the transmit system using the 'a' and 'b' commands per acquisition event using the receive system. eg, if you want to loop through a set of steering locations while acquiring data with the receive system, you either have to issue a new set of 'a'/'b' commands with a SINGLE trigger from within a python loop before you fire each pulse (do this, it's different than how it worked in matlab, but it's easy. trust me), or you have to ensure that the program you upload to the transmit boards using the 'a' and 'b' commands, even if it's firing the array *elements* multiple times, only fires the trigger once (keep in mind that this means that you'll only be acquiring data for the one pulse you issue the trigger on). This rule is in place because I cannot guarantee synchronous operation between the transmit and receive systems if the transmit system triggers the receive system faster than the receive system can process the data due to the limited data transfer rates over the slow bridge between the FPGAs and SoCs in the receive system (hence the inclusion of a synchronization command, 'ipcWait', see below). As such, the receive system is explicitly designed such that, unless you issue a command to the receive system that tells it where to put the acquired data after each trigger, which you cannot do using the 'a' and 'b' commands, it will essentially fail and just continually overwrite the memory location where the data from the previous pulse was stored after each trigger. This is not a bug and I have no intention of 'fixing' it.
		
		- Recommendation: Issue the 'resetVars' command before setting any other variables on the SoCs or cServer. The cServer doesn't overwrite old variables after you disconnect from it, so if you leave it running, then reconnect, and then try to run a new program from the python side, any variables that you don't explicitly overwrite in your new function will carry over from the last experiment. It's good practice to set all data acquisition parameters explicitly before your experiment, anyway.
		
		- Recommendation: If you are going to change both the record length and the number of acquisitions (ie the size of the array which the cServer will store the acquired data in), you should issue the 'setRecLen' command BEFORE the 'setDataArraySize' command. Nothing bad will happen if you don't, it's just to try to minimize repeated memory reallocations in the cServer.
		
		- Recommendation: Issue the 'setRecLen' command before you issue the 'setEnetPacketSize' command. The packet size can't be greater than the record length, and if it is it will get overwritten which could affect performance. The two situations where this might be an issue are 1) if the new packet size is less than the upcoming new record length (the user should be aware enough to catch this one...) and 2) if the new packet size is greater than the old record length (this could happen if you're not careful, particularly after a call to 'reserVars' which sets the record length to its default value of 2048. if you then want to set the record length and packet size to 4096, but issue the 'setEnetPacketSize' command first, that'd create a conflict and the packet size would get reset to 2048 before you issued the 'setRecLen' command to set the record length to 4096. [as an aside. this would actually improve performance because the slow bridge is awful and setting the packet size to be less than the record length helps get around that problem when it arises, eventually the slow bridge will probably be fixed though and then this could cause actual problems.])
		
		- Rule: Issue the 'allocateDataArrayMemory' command after you issue the 'setRecLen' and 'setDataArraySize' commands. the cServer is not set up to allocate memory dynamically, so if you forget to do this the cServer will only have limited space to put the data you are trying to acquire, and all the data you do collect will continually get overwritten and/or end up in the wrong place in memory. If you don't issue this command it is also very likely that you will segfault the cServer and crash the program. No part of the code checks whether or not you remember to allocate the memory for the data before running the program, and I'm not inclined to have the cServer automatically reallocate the memory for the data every time one of the resizing commands is issued (it can take a bit of time if the data size is large, especially if it happens to be larger than what's free in RAM, and can lead to some fragmentation issues if done too often). Your data is probably doomed if you forget to do this. Sorry. 
		
		- Rule: Issue the command 'toggleDataAcq(1)' before you begin acquiring data. This puts the SoCs/FPGAs into a state to acquire data and transmit it to the cServer after each pulse. You don't have to do 'toggleDataAcq(0)' at the end of the program, it gets reset every time you 'connect' to the cServer at the beginning of your function. If the SoC is not in a data acquisition mode, it will never return data to the cServer, if the cServer never receives data, it will never reply to the python UI that it got data from the SoCs, if it doesn't reply to python that it got data for the SoCs, python will hang indefinitely waiting for data and the program will freeze. The caveat here though, is that if the rest of your program DOESN'T follow the rules ( ie you don't issue the 'ipcWait' command after each pulse, which tells the cServer to actually wait for the data and makes python wait for the cServer's reply that it got it all, see below ), the program will likely run seemingly without issue. However, if you issue the 'getData' or 'saveData' commands at the end of your program, they will either cause a segfault and crash the cServer, or will return/save an array of all zeros.
		
		- Rule: Issue the command 'declareDataAcqIdx' BEFORE you issue the 'b.go()' command every pulse you want to acquire data from. This tells the cServer where in the data array to put the data acquired for that pulse. Everything will still run if you don't do this, but if you don't the cServer will just keep overwriting the first element of the data array with the acquired data after each pulse.
		
		- Rule: Issue the command 'ipcWait' AFTER the 'b.go()' command every pulse you want to acquire data from. This function makes python wait for confirmation from the cServer that the cServer actually acquired all the data it was supposed to from the last pulse that before firing the next one. This synchronizes the otherwise asynchronous operation of the transmit and receive systems. If this command is not issued, the transmit system can fire/trigger however often you tell it to, even if the receive system isn't done collecting/transmitting data from the last pulse. Almost no matter what, whenever the FPGA receives a trigger signal it begins writing the acquired data to the FPGA memory buffer. This can result in a situation where the data being transfered from the FPGA to the SoC can come from two separate pulses, where the first half of the array was transfered to the SoC before the new trigger, and the second half was transfered to the SoC after the new trigger. Without synchronizing the transmit and receive systems, the only way to avoid this is to go slow enough to ensure there was enough time for all the data to get from the FPGA to the SoC over the slow bridge after each pulse. If you forget to do this, especially if you're trying to go fast with a large record length, it will almost certainly result in the cServer not acquiring all the data it expects to acquire because some of it got overwritten on the SoC before it was transferred, and what did make it through will probably be 'garbage' because there's no way to guarantee that the data you did acquire really came from just one pulse.
		
		- Recommendation: Don't use the 'setChannelMask' command. You can read the description of it in the function definition below, but it's kind of a complicated mess. Long story short, it allows you to write data to a single data array on the SoCs, after acquiring data from multiple trigger events, without transfering that data to the cServer after each one. The caveat is that it requires you to pick sub-apertures of the array to acquire data from, eg. lets say you have a data array on the SoC of size [recLen,8]. during one pulse you could acquire data from elements 1-4 into the first 4 columns of the array, then during the next pulse you could acquire data from elements 5-8 into columns 5-8 of the same array. After that, you could transfer the whole [recLen,8] array, containing data from two separate pulses, to the cServer. I recommend not using this because the system is generally fast enough not to need it, and because this is function is different than all the other ones in that it requires the cServer and python to receive confirmation from the SoCs that they actually received the commands in real time during operation. The problem is, I didn't write the code running on the SoCs or the cServer to handle that type of thing in any sort of natural way, and implemented it through a series of convoluted bit masking and shifting operations on one the lesser used variables that gets sent to the cServer and SoCs when issuing other commands. That said, under very specific sets of conditions, it can offer speed ups of ~30-50%, so it could be worth it if you run into the slow bridge bottle neck and can get by with acquiring only subsets of data that don't get transfered after each pulse. It's described under the function definition if you really want to use it.
		
example program:
	
	from fpgaClass import *
	from dataServer import *
	import time
	
	RECVBOARDS = 8
	x = FPGA()
	a = a_funcs(x)
	b = b_funcs(x)
	b.stop()
	d = dataServer()
	
	PRF = 50
	
	nSteeringLocations = 20
	nPulsesPerLoc = 50
	nChargeTimes = 5 
	
	#* upload your program to the transmit system using the a and b commands *#
		
	packet_size = 512
	transReadyTimeout = 1100 # us
	trigDelay = 150 # us
	recLen = 2048
	
	d.connect()
	
	d.resetVars()
	
	d.setTrigDelay(trigDelay)
	d.setRecLen(recLen)
	d.setEnetPacketSize(packet_size)
	d.setSocTransReadyTimeout(transReadyTimeout)
	d.setDataArraySize(nChargeTimes,nPulsesPerLoc,nLocations)
	d.allocateDataArrayMemory()
	d.toggleDataAcq(1)
	
	for chargetimeN in range(0,nChargeTimes):
		for pulseN in range(0,nPulsesPerLoc):
			for locN in range(0,nSteeringLocations):
				t0 = time.time()
				d.declareDataAcqIdx(chargetimeN,pulseN,locN)
				
				#* do some things *#
				
				b.go()
				d.ipcWait()
				
				dt = time.time()-t0
				if( dt < 1.0/PRF ):
					time.sleep((1.0/PRF-dt))
	
	
	binary_data = d.getData(RECVBOARDS)
	#* OR *#				
	d.saveData('dataFile')
	
	d.disconnect()
	d.shutdown()
	

'''

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
		# sets the number of data points to collect per acquisition. takes integer 'rl' as input, unitless
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
			
	def setEnetPacketSize(self,ps):
		# sets the size of the packets to be sent over ethernet from the socs. takes integer 'ps' as input, units = bytes. should be a power of two, must be an even divisor of the record length. Recommended to keep it 1024 or less
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
		if ( l1 >= 0 ) and ( l2 >= 0 ) and ( l3 >= 0 ):
			self.l1 = ( l1 if l1>0 else 1 )
			self.l2 = ( l2 if l2>0 else 1 )
			self.l3 = ( l3 if l3>0 else 1 )
		else:
			self.l1, self.l2, self.l3 = 1,1,1
			print 'Error all indices must be greater than or equal to 0, setting all equal to 1'
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
		# this tells the cServer where to store the incoming data after each pulse, needs to be set explicitly before each pulse. takes integer values 'id1','id2' and 'id3' as inputs, which are the array array indices into which the cServer will put the acquired data. eg, if your experiment were steering to N locations, treating each location with M pulses, and using K different charge times, you would tell the cServer, "this pulse corresponds to location 'n', pulse 'm', charge time 'k'", and it would put that data into the array at the corresponding location. if you don't set this variable each time, the cServer will just overwrite the values stored memory location [0,0,0] after each pulse. Note C starts counting at 0, not 1, so the first index is 0
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
		# this functions restores the variables in the cServer and SoCs to their default values
		msg = struct.pack(self.cmsg,9,0,0,0,"")
		self.ff.write(msg)
		time.sleep(0.05)
		
	def saveData(self,fname):
		# this function tells the cServer to save the acquired data into a binary file named 'fname'. takes string 'fname' as input, 'fname' must be less than or equal to 100 character long
		msg = struct.pack(self.cmsg,10,0,0,0,fname)
		self.ff.write(msg)
		time.sleep(0.05)
		
	def getData(self,recvBoards):
		# this function tells the cServer to transfer the array it is storing data in directly to the python server for the user to do with what they want. takes integer value 'recvBoards' as the input, 'recvBoards' is an integer value corresponding the number of boards currently acquiring data. this function returns a binary array containing the data to the user. if 'recvBoards' is greater than the number of connected SoCs, the program will hang. Weird things might happen if you set it to less than the number of connected boards
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
		# connect to C server, python will wait until cServer is running if it isn't when this function is called. python will hang until the cServer is launched, blocking user interactions
		self.ff = open(self.IPCFIFO,"w",0)
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
	
	def __init__(self):	
		# class intialization for the python data server 
		
		self.IPCSOCK = "./lithium_ipc"	# name of the ipc socket to connect to the cServer through
		self.IPCFIFO = "data_pipe"		# name of the FIFO to connect to the cServer through
		self.cmsg = '4I100s'			# tells python how to package messages to send to the cServer -> [4*(unsinged 32-bit int), string (up to 100 characters)]
		
		# default values of data acquisition variables
		self.trigDelay = 0
		self.packetSize = 512
		self.da = 0
		self.dataAcqMode = 0
		self.timeOut = 1e3
		self.recLen = 2048
		self.id1,self.id2,self.id3 = 0,0,0
		self.l1, self.l2, self.l3 = 1,1,1
		

	
	





