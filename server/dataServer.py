

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
		
	def setRecLen(self,rl):
		# sets the number of data points to collect per acquisition, NOT the time duration of acquisition. takes integer 'rl' as input, unitless
		# the acquisition time window = [rl/20] us
		if ( rl >= MIN_PACKETSIZE ) and (rl <= REC_LEN_MAX):
			self.recLen = int(rl)
		else:
			print 'Invalid Record Length. [ Valid range = 128-8192 ]. Setting recLen to 2048, packetsize to 512'
			self.recLen = 2048
			self.packetsize = 512
			
		msg = struct.pack(self.cmsg,1,self.recLen,0,0,"")
		self.ipcsock.send(msg)
		msg = struct.pack(self.cmsg,2,self.packetsize,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
	
	def setPacketsize(self,ps):
		# sets the number of data points to collect per acquisition, NOT the time duration of acquisition. takes integer 'rl' as input, unitless
		# the acquisition time window = [rl/20] us
		if (ps >= MIN_PACKETSIZE) and (ps <= self.recLen):
			self.packetsize = int(ps)
		else:
			print 'Invalid packetsize, setting equal to recLen.'
			self.packetsize = self.recLen
			
		msg = struct.pack(self.cmsg,2,self.packetsize,0,0,"")
		self.ipcsock.send(msg)
		time.sleep(0.05)
		
	def updateFDSet(self):
		# sets the number of data points to collect per acquisition, NOT the time duration of acquisition. takes integer 'rl' as input, unitless
		# the acquisition time window = [rl/20] us
		
			
		msg = struct.pack(self.cmsg,3,self.packetsize,0,0,"")
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
		self.getBoardCount()
		
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
		
		msg = struct.pack(self.cmsg,11,0,0,0,"")
		self.ipcsock.send(msg)
		self.ipcWait()
		
		# Create a shared memory object
		memory = sysv_ipc.SharedMemory(self.shmkey)
		
		# Read value from shared memory
		memory_value = memory.read()
		
		return memory_value
  	
	def getData(self):
		# this function tells the cServer to transfer the array it is storing the data in directly to the python server for the user to do with what they want. this function returns a binary array containing the data to the user. 
		if (self.boardCount > 0):
			msg = struct.pack(self.cmsg,11,0,0,0,"")
			self.ipcsock.send(msg)
			return self.ipcsock.recv(2*self.recLen*self.l1*self.l2*self.l3*self.boardCount*np.dtype(np.uint32).itemsize,socket.MSG_WAITALL)
		else:
			print 'Invalid number of boards detected,returning single value \'0\' '
			return 0
	
	def queryBoardInfo(self):
		msg = struct.pack(self.cmsg,12,0,0,0,"")
		self.ipcsock.send(msg)
			
	def getBoardInfo(self):
		# gets the identifying numbers of the boards connected to the cServer 
		
		msg = struct.pack(self.cmsg,13,0,0,0,"")
		self.ipcsock.send(msg)
		self.boardNums = np.array(struct.Struct('{}{}{}'.format('=64I')).unpack(self.ipcsock.recv(64*4,socket.MSG_WAITALL)))
		self.boardCount = len(np.argwhere(self.boardNums>0))
		
		return self.boardNums
	
	def queryData(self):
		# makes the socs check for data 
		msg = struct.pack(self.cmsg,16,0,0,0,"")
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
		#~ self.ff.close()
		time.sleep(0.05)
		
	def connect(self): 
		# connect to the C server. 
		# Note: python will block until it is connected to the cServer. if the python program gets 'stuck' before it begins it's likely because the cServer stopped running or crashed, just relaunch and python should connect to it. You don't have to restart python before (re)launching the cServer, but if it still hangs or returns an error restart them both. If it still doesn't work, delete the files 'data_pipe' and 'lithium_ipc' in the folder containing the cServer and restart the cServer and python program again. 
		#~ self.ff = open(self.IPCFIFO,"w",0)
		self.ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.ipcsock.connect(self.IPCSOCK)
	
	def ipcWait(self,to=None):
		# tells python to wait for confirmation from the cServer that all data has been received from the SoCs. used to synchronize transmit and receive systems
		# 'to' is an optional timeout argument that tells python that if no data arrives within the specified timeout, to return the value '0' and continue the regular running of the program
		if to != None:
			nready = select.select([self.ipcsock], [], [], to)
			if nready[0]:
				#~ print 'yoyoyo'
				return self.ipcsock.recv(4,socket.MSG_WAITALL)
			else:
				print 'ipcWait timed out'
				return 0
		else:
			return self.ipcsock.recv(4,socket.MSG_WAITALL)
				
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
		self.packetsize = 512
		self.id1,self.id2,self.id3 = 0,0,0
		self.l1, self.l2, self.l3 = 1,1,1
		self.boardCount = 0
		self.boardNums = []
		

	
	
''' dataServer Class Description

	This class contains commands for interacting with the cServer to acquire data from the receiver boards
	
	*** Notes ***
	
	- Runtime parameter constants are imported from 'dataServerParamConfig.py'. This file contains the values TRIG_DELAY_MAX, REC_LEN_MAX, TRANS_READY_TIMEOUT_MIN, and RECVBOARDS. These are things that are hardcoded into the SoCs/FPGAs and cServer that may change in the future depending on our needs, but which need to be enforced in the dataServer. (RECVBOARDS isn't actually hardcoded anywhere, it's just convenient to leave it in the config file so it doesn't cause problems when porting code between systems with different numbers of receive boards.)
	
	- All inputs to functions within the dataServer class (except 'fname' in the 'saveData' function, which is a string with 100 characters or less) should be of the type uint32_t (unsigned 32-bit integers). All inputs are cast into integers before they are sent to the cServer in the 'struct.pack(...)' commands, this means any inputs with decimal values will be rounded down to the nearest whole number before they are sent to the cServer. Any illegal values, eg negative numbers, are either rejected or set to the defaults.
	
	
	*** Programming rules and recommendations ***
	
		- Rule: Issue the 'connect' command at the beginning of your function unless you don't want to collect data. You have to connect to the cServer to interact with it in order to acquire data from the SoCs so this makes sense.
		
		- Rule: Issue the 'disconnect' command at the end of your function (note: this leaves the cServer running and you can reconnect to it later with another call to 'connect'. If your experiment is completely over, issue the 'shutdown' command instead.) You can connect to the cServer multiple times from python without disconnecting, but it usually leads to communications issues with the FIFO and IPC socket that are the links between python and the cServer, which tends to make everything crash. Unfortunately, I can't automate/error check for this at runtime. If you forget to include it in your script, you can issue the 'disconnect' command from the same terminal you ran your function in after the program ends and before you run another one to the same effect. It won't break things to not disconnect if you only run your function once and then shutdown python itself.
		
		- Rule: DO NOT issue more than ONE trigger from the transmit system using the 'a' and 'b' commands per acquisition event using the receive system. eg, if you want to loop through a set of steering locations while acquiring data with the receive system, you either have to issue a new set of 'a'/'b' commands with a SINGLE trigger from within the python loop before you fire each pulse (do this, it's different than how it worked in matlab, but it's easy. trust me), or you have to ensure that the program you upload to the transmit boards using the 'a' and 'b' commands, even if it's firing the array *elements* multiple times, only fires the trigger once (keep in mind that this means that you'll only be acquiring data for the one pulse you issue the trigger on). This rule is in place because I cannot guarantee synchronous operation between the transmit and receive systems if the transmit system triggers the receive system faster than the receive system can process the data due to the limited data transfer rates over the slow bridge between the FPGAs and SoCs in the receive system (hence the inclusion of a synchronization command, 'ipcWait', see below). To force the issue, the receive system is explicitly designed such that, unless you issue a command to the receive system that tells it where to put the acquired data after each trigger (which you cannot do using the 'a' and 'b' commands) it will just continually overwrite the first memory location in the data array after each trigger and you'll lose all your data. This is not a bug and I have no intention of 'fixing' it. 
		
		- Recommendation: Issue the 'resetVars' command before other commands to set the runtime variables (ie, 'setTrigDelay' or 'setRecLen') on the SoCs and cServer. The cServer doesn't overwrite/clear old variables after you disconnect from it, so if you leave the cServer running, then reconnect to it, and then try to run a new program from the python side, any variables that you don't explicitly overwrite in your new function will carry over from the last experiment, which could result in unexpected behavior. 
		
		- Recommendation: If you are going to change both the record length and the number of acquisitions (ie the size of the array which the cServer will store the acquired data in), you should issue the 'setRecLen' command BEFORE the 'setDataArraySize' command. Nothing bad will happen if you don't, it's just to try to minimize repeated memory reallocations in the cServer.
		
		- Recommendation: Issue the 'setRecLen' command before you issue the 'setEnetPacketSize' command. The packet size can't be greater than the record length, and if it is it will get overwritten and set equal to the record length, which could affect performance. The most likely situation this would happen in is if the new packet size is less than the current or yet-to-be-set record length. for example, after a call to 'resetVars', the record length gets its default value of 2048. if you then want to set both the record length and packet size to 4096, but issue the 'setEnetPacketSize' command first, the packet size would get reset to 2048 before you issued the 'setRecLen' command because that's what the 'resetVars' command would have set it to. When running your program then, the packet size would be 2048 and the record length would be 4096. It should be noted that the packet size is only an option for optimizing transfer speeds, and even in the case where the packet size does get reset by this, it won't result in any data loss and the correct number data points will still be collected and transmitted to the cServer/python. 
		
		- Recommendation: Set the packet size to be some value 2^N (ie, 2, 4, 8, 16, ... 256, 512, 1024, ... etc). Data transfer is much more efficient that way. Also, keep the packet size at 1024 or less, setting the packet size larger requires multiple 'write's to the ethernet socket which tends to slow things down. That said, it's most efficient to transmit as large a packet as you can, so keep it at 1024 unless your record length is smaller than that, or your own optimization testing shows that some other value is better. 
		
		- Recommendation/Note: Don't change the packet size from it's default value unless you really need to or things are running slower than you think they should be. This command is in here to improve speed/performance of the system because the slow bridge transfer of data from the FPGAs to the SoCs is awful (it takes ~1ms per 1000 data points, which adds up for larger record lengths. eg, for a record length of 4096, ~4.1ms are spent just transferring data from the FPGAs to the SoCs, which places a hard limit on how fast you can run the system while acquiring data, it gets worse when accounting for the time it takes to transfer all that data over ethernet to the server, too). Essentially, what setting the packet size does is break up the transfer of data from the FPGA to the SoC into smaller chunks, which get written to ethernet as soon as they are on the SoC (writing 1000 pts to ethernet takes ~20us from what I've been able to tell). It this way, the data can be transferred to the cServer from the SoCs during what would otherwise be slow-bridge 'dead time'. This has the added benefit of reducing congestion over ethernet by breaking the data up into smaller chunks which can be processed more quickly by the cServer, which is especially useful as the number of receiver boards increases. Setting the packet size to be less than the record length helps get around the problems caused by the slow bridge but does not solve them, so if you wanted to collect a record length of 8192 data points, you'd be hard capped at ~100Hz at best, no matter what.
		
		- Rule: Issue the 'allocateDataArrayMemory' AFTER issuing the 'setDataArraySize' and 'setRecLen' commands and BEFORE you begin running the experiment. The cServer is not set up to dynamically reallocate memory on the fly to expand the data set pulse-by-pulse, so all memory needs to be explicitly allocated using this command before you begin acquiring data. If you forget to do this the cServer will only have limited space to store the data you are trying to acquire, and all the data you do collect will continually get overwritten in the same spot in memory. Not issuing this command may also segfault the cServer and crash the program if you try to write to an unavailable location in memory using the 'declareDataAcqIdx' command. Unfortunately, no part of the code checks whether or not you remember to allocate the memory for the data before running the program, and I'm not inclined to have the cServer automatically reallocate the memory for the data after every pulse you want to acquire data from because it could lead to fragmentation issues if done over and over again, which would slow things down, and more likely and worse, it could easily lead to 'accidental' overflows of the available system memory during a long experiment, which would pretty much crash the whole computer and wipe out all your data along with it. I didn't build in anything to prevent anyone from trying to allocate too much memory up front, but if you do the cServer will probably throw up an error and crash, or the computer will get real slow and maybe also crash. I am very inclined not to implement/allow on-the-fly memory allocation because forcing the allocation up front serves as a fail-safe mechanism to make the computer crash *before* you begin running the experiment as opposed to *during* the experiment, in which case you just have to restart the cServer or computer, but at least you won't have lost all the data and wasted a phantom or tissue like you would have otherwise. 
		
		- Rule: Issue the command 'toggleDataAcq(1)' before you begin acquiring data. This puts the SoCs/FPGAs into a state to acquire data and transmit it to the cServer after each pulse. You don't have to do 'toggleDataAcq(0)' at the end of the program, it gets reset every time you 'connect' to the cServer at the beginning of your function. If the SoC is not in a data acquisition mode, it will never return data to the cServer, if the cServer never receives data, it will never reply to the python UI that it got data from the SoCs, if it doesn't reply to python that it got data from the SoCs, python will hang indefinitely waiting for data from the cServer and the program will freeze. The caveat here though, is that if you don't issue the 'ipcWait' command after each pulse, which makes python wait for the cServer to respond to it that it is finished acquiring all the data from the SoCs from the previous pulse ( see below ), the program will likely run seemingly without issue. However, if you later issue the 'getData' or 'saveData' commands at the end of your program, they will either cause a segfault and crash the cServer, or will return/save an array of all zeros.
		
		- Rule: Issue the command 'declareDataAcqIdx' BEFORE you issue the 'b.go()' command every pulse you want to acquire data from. This tells the cServer where in the data array to put the data acquired for that pulse. Everything will still run if you don't do this, but if you don't the cServer will just keep overwriting the first element of the data array with the acquired data after each pulse. See the description below the function definition below for an example.
		
		- Rule: Issue the command 'ipcWait' AFTER the 'b.go()' command every pulse/trigger you want to acquire data from. This function essentially serves to synchronize the otherwise asynchronous transmit and receive systems by making python wait for confirmation from the cServer that the cServer actually acquired all the data it was supposed to from the last pulse before firing the next one. If this command is not issued, the transmit system can fire/trigger however often you tell it to, even if the receive system isn't done collecting/transmitting data from the last pulse. Almost no matter what, whenever the FPGA receives a trigger signal it begins writing the newly acquired data to the FPGA memory buffer. This can result in a situation where the data being transfered from the FPGA to the SoC can come from two separate pulses, where the first half of the data was transfered to the SoC before the new trigger, and the second half was transferred to the SoC after the new trigger. Without synchronizing the transmit and receive systems, the only way to avoid this is to go slow enough to ensure there was enough time for all the data to get from the FPGA to the SoC over the slow bridge after each pulse. If you forget to issue this command, especially if you're trying to go fast with a large record length, it will almost certainly result in the cServer not acquiring all the data it expects to acquire because some of data will likely get overwritten on the SoC before the previous data set was fully transferred, so instead of receiving 2048 data points from two separate pulses for a combined length of 4096, it might receive 1900 from the first pulse and 2048 from the second for a combined length of 3948, which would make the cServer wait indefinitely for the remaining 148 data points which will never arrive. Even if you could pull the data from the cServer in the event of a hang up like that though, (which I don't think you can, but might be wrong about that), the way the data is filled in to the data array is essentially 1 bit at a time, so the first set of data in the array wouldn't only have the 1900 pts from the first pulse plus 148 zeros, it'd have the 1900 pts from the first pulse and the first 148 pts from the next pulse, and the second set of data in the array would only have the last 1900 pts from that pulse. This is related to the Rule above about only issuing ONE trigger per acquisition event, except instead of issuing multiple triggers by embedding them in the 'a'/'b' commands, in this case you'd be issuing them by calling 'b.go()' too often from within your python loop, to essentially producing the same result.
		
		- Recommendation: Don't use the 'setChannelMask' command. You can read the description of it in the function definition below and the cServer code, but it's kind of a complicated mess. Long story short, it allows you to write data to a single data array on the SoCs from multiple trigger/data acquisition events without transfering that data to the cServer after each one. The caveat is that it requires you to pick sub-apertures of the array to acquire data from, eg. lets say you have a data array on the SoC of size [recLen,8]. during one pulse you could acquire data from elements 1-4 into the first 4 columns of the array, then during the next pulse you could acquire data from elements 5-8 into columns 5-8 of the same array. After that, you could transfer the whole [recLen,8] array, containing data from two separate pulses, to the cServer. I recommend not using this because the system is generally fast enough not to need it and because this is function is different than all the other ones in that it requires the cServer and python to receive confirmation from the SoCs that they actually received the commands in real time during operation. The problem is I didn't write the code running on the SoCs or the cServer to handle that type of thing in any sort of natural way, and implemented it through a series of convoluted bit masking and shifting operations on one the lesser used variables that gets sent to the cServer and SoCs when issuing other commands. That said, under very specific sets of conditions, it can offer speed increases of ~30-50%, so it could be worth it if you run into the slow bridge bottle neck and can get by with acquiring only subsets of data for each pulse that don't get transfered until all subsets are acquired. It's described under the function definition if you really want to use it, but this one is still a work in progress and will very likely change.
		
	*** Example Program ***
		
		### import all the modules needed to run the program
		from fpgaClass import *
		from dataServer import *
		import time
		
		### initialize the modules containing the transmit and receive commands
		x = FPGA()
		a = a_funcs(x)
		b = b_funcs(x)
		b.stop()
		d = dataServer()
		
		### declare how many receiving boards should be acquiring data
		RECVBOARDS = 8
		
		### declare runtime variables required by the transmit system/python loop
		PRF = 50
		
		### declare runtime variables required by the transmit AND receive systems	
		nSteeringLocations = 20
		nPulsesPerLoc = 50
		nChargeTimes = 5 
		
		#---> upload your program to the transmit system using the 'a' and 'b' commands <---#
		
		### declare runtime variables required by only the receive system	
		packet_size = 512
		transReadyTimeout = 1100 # us
		trigDelay = 150 # us
		recLen = 2048
		
		### connect to the receive system and issue the resetVars command to initialize everything to the defualt values
		d.connect()
		d.resetVars()
		
		### send the runtime variables to the receive system
		d.setTrigDelay(trigDelay)
		d.setRecLen(recLen)
		d.setEnetPacketSize(packet_size)
		d.setSocTransReadyTimeout(transReadyTimeout)
		
		### set the size of the data array to be acquired and allocate memory accordingly
		d.setDataArraySize(nChargeTimes,nPulsesPerLoc,nLocations)
		d.allocateDataArrayMemory()
		if RECVBOARDS != d.boardCount:
			print 'the number of boards connected (',d.boardCount,') doesn't match the number specified (',RECVBOARDS,')'
			print 'board numbers of connected socs:', d.getBoardNums()
		
		### put the cServer and SoCs in a state to be ready to acquire data
		d.toggleDataAcq(1)
		
		### loop through the steering positions/pulses/chargetimes and fire the array and acquire data
		for chargetimeN in range(0,nChargeTimes):
			for pulseN in range(0,nPulsesPerLoc):
				for locN in range(0,nSteeringLocations):
					
					### timer variable ( PRF is enforced through timers in python by pausing appropriately after each data acquisition event before firing the next pulse ) 
					t0 = time.time()
					
					######################################################################################################
					#																									 #
					#---> update the steering location or other transmit system variables using the 'a'/'b' commands <---#
					#																									 #
					######################################################################################################
					
					### tell the cServer the index location in the data array where to store the next data set to be acquired
					d.declareDataAcqIdx(chargetimeN,pulseN,locN)
					
					### fire the array
					b.go()
					
					### wait for the cServer to reply that it has received all the data from the SoCs
					d.ipcWait()
					
					### check whether the data acquisition took less time than require to meet the PRF requirement and if so wait the appopriate amount of time before pulsing again
					dt = time.time()-t0
					if( dt < 1.0/PRF ):
						time.sleep((1.0/PRF-dt))
						
						### Note: if it takes longer than the PRF to fire the pulse and acquire all the data, the system will run slower than the desired PRF. If you need to go faster you'll have to decrease the record length or acquire data after every nth pulse instead of after every pulse (or fiddle with the setChannelMask command if you're daring)
		
		
		#----------------------------------------------------#
		
		### transfer the data from the cServer to python ###
		binary_data = d.getData()
		
		#            #
		#---> OR <---#	
		#            #
		
		### save the data to a binary file ###	
		d.saveData('dataFile')
		
		#----------------------------------------------------#
		
		
		### disconnect from the cServer
		d.disconnect()
		
		### shutdown the cServer and SoCs
		d.shutdown()
	

'''





