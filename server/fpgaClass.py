# Class for controlling histotripsy FPGAs
# use varLoader.py to initialize class
#
# MaizeChip 1.0 Feb 2014 TLH
# MaizeChipPy 1.0 May 2017 JJM

import serial
import numpy as np
import array
from math import floor
from math import ceil
import glob

class FPGA(object):
	
	def __init__(self,portNum=None):
		

		print 'Initializing communications...\n'
		
		ports = glob.glob('/dev/ttyUSB*')
		print ports
		if len(ports) == 1:
			self.ser = serial.Serial(ports[0])
			print 'driving system communications initialized\n'
		else:
			if portNum == None:	
				nn = 0	
				print 'more than one USB device is connected. '
				for p in ports:
					print 'port label =', p, ', port num = ', nn
					nn+=1
				
				
				serNum = input("Input port num associated with correct port label: ")
				print '\n'
				try:
					self.ser = serial.Serial(ports[int(serNum)])
					#~ self.ser = serial.Serial('/dev/ttyUSB%s' % serNum)
					#~ self.ser.reset_output_buffer()
					print 'driving system communications initialized\n'
				except:
					print 'port number invalid'		
					
			else:
				nn = 0
				for p in ports:
					pn = int(p.split('ttyUSB')[1])
					if pn == portNum:
						print pn, portNum, ports[nn]
						self.ser = serial.Serial(ports[nn])
					nn+=1
						



class a_funcs():
	
	def fire(self, n): # acmd = 8, 0b1000
		acmd = 8
		data = np.array([self.startcode,self.bcmd5,0,0,0,n,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
				
	def halt(self): # acmd = 1, 0b1
		acmd = 1
		data = np.array([self.startcode,self.bcmd5,0,0,0,0,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		 		
	def loadincr_chipmem(self, n,m): # acmd = 11, 0b1011

		acmd = 11
		a = format(acmd,'04b')
		q_val = format(m,'016b')
		
		byte1 = int('{}{}{}{}{}'.format(n,a[0],a[1],a[2],a[3]),2)
		byte2 = int(q_val[8:16],2)
		byte3 = int(q_val[0:8],2)
		byte4 = 0
		data = np.array([self.startcode,self.bcmd5,0,byte4,byte3,byte2,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
			
	def set_amp(self, n): # acmd = 9, 0b1001
		acmd = 9
		data = np.array([self.startcode,self.bcmd5,0,0,0,n,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
			
	def set_LEDs(self, n): # acmd = 7, 0b111
		acmd = 7
		data = np.array([self.startcode,self.bcmd5,0,0,0,n,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def set_phase(self, n): # acmd = 10, 0b1010
		acmd = 10
		data = np.array([self.startcode,self.bcmd5,0,0,0,n,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
				
	def set_trig(self, n): # acmd = 6, 0b110
		acmd = 6
		data = np.array([self.startcode,self.bcmd5,0,0,0,n,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
				
	def start_loop(self, n,m): # acmd = 2, 0b10
		acmd = 2
		a = format(acmd,'04b')
		q_val = format(m,'024b')
		nbin = format(n,'03b')
		
		byte1 = int('{}{}{}{}{}{}{}'.format(nbin[0],nbin[1],nbin[2],a[0],a[1],a[2],a[3]),2)
		byte2 = int(q_val[16:24],2)
		byte3 = int(q_val[8:16],2)
		byte4 = int(q_val[0:8],2)
		data = np.array([self.startcode,self.bcmd5,0,byte4,byte3,byte2,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		
		self.ser.write(data)
				
	def end_loop(self, n): # acmd = 3, 0b11
		acmd = 3
		a = format(acmd,'04b')
		nbin = format(n,'03b')
		byte1 = int('{}{}{}{}{}{}{}'.format(nbin[0],nbin[1],nbin[2],a[0],a[1],a[2],a[3]),2)

		data = np.array([self.startcode,self.bcmd5,0,0,0,0,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
				
	def wait(self, n): # acmd = 4, 0b100
		acmd = 4
		a = format(acmd,'04b')
		
		bits = format(int(n),'028b')
		byte1 = int('{}{}{}{}{}{}{}{}'.format(bits[24],bits[25],bits[26],bits[27],a[0],a[1],a[2],a[3]),2)
		byte2 = int(bits[16:24],2)
		byte3 = int(bits[8:16],2)
		byte4 = int(bits[0:8],2)
		data = np.array([self.startcode,self.bcmd5,0,byte4,byte3,byte2,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
				
	def waitsec(self, t):
		
		n = round(t*100e6)-7
		if n < 0:
			n = 0
		
		acmd = 4
		a = format(acmd,'04b')
		
		bits = format(int(n),'028b')
		byte1 = int('{}{}{}{}{}{}{}{}'.format(bits[24],bits[25],bits[26],bits[27],a[0],a[1],a[2],a[3]),2)
		byte2 = int(bits[16:24],2)
		byte3 = int(bits[8:16],2)
		byte4 = int(bits[0:8],2)
		data = np.array([self.startcode,self.bcmd5,0,byte4,byte3,byte2,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def waitFloat(self,n): # acmd = 4, 0b100
		if n > 1.0995e12:
			print 'Maximum wait must be less than 1.0995e+12, defaulting to 1s'
			n = int(1e8)

		significandbits = 20
		expbits = 5
		floatexp = np.ceil(np.log(n)/np.log(2)-significandbits)
		significand = int(np.round(n/2**floatexp))
		
		if floatexp < 0:
			floatexp = 0
			significand = n
			
		if significand == 2**20:
			significand = 2**19
			floatexp += 1
		acmd = 4
		a = format(acmd,'04b')
		sigbin = format(int(significand),'020b')
		expbin = format(int(floatexp),'05b')
		bits = '{}{}{}'.format(sigbin,expbin,'000')
		byte1 = int('{}{}{}{}{}{}{}{}'.format(bits[24],bits[25],bits[26],bits[27],a[0],a[1],a[2],a[3]),2)
		byte2 = int(bits[16:24],2)
		byte3 = int(bits[8:16],2)
		byte4 = int(bits[0:8],2)
		data = np.array([self.startcode,self.bcmd5,0,byte4,byte3,byte2,byte1,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
			
	def waitsecFloat(self,t):
		n = round(t*100e6)
		if n < 0:
			n = 0
			
		self.waitFloat(n)
		
	def wait_trig(self,x): # acmd = 5, 0b101
		acmd = 5
		data = np.array([self.startcode,self.bcmd5,0,0,0,x,acmd,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
			
	def noop(self, n):
		
		return
			
	def __init__(self,x):
		self.ser = x.ser
		self.startcode = 170
		self.endcode = 85
		self.bcmd5 = 5


		
class b_funcs():
	
	def stop_execution(self):
		
		bdaddr = 0
		bcmd = 0
		data = np.array([self.startcode,bcmd,0,0,0,0,0,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def mask_off(self):
		
		bcmd = 13
		data = np.array([self.startcode,bcmd,0,255,255,255,255,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def select_motherboard(self,n):
		
		bcmd = 10
		data = np.array([self.startcode,bcmd,0,0,0,0,n % 256,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def set_chipmem_wloc(self,n):
		
		bcmd = 6
		data = np.array([self.startcode,bcmd,0,0,0,floor(n/256),n % 256,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def set_imem_wloc(self,n):

		bcmd = 4
		data = np.array([self.startcode,bcmd,0,0,0,floor(n/256),n % 256,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)

	def set_mask_off_forcefully(self):
		#~ %   Sends "b" command instruction to specify a channel mask.
		#~ %   n represents the bit pattern for active and masked channels.
		#~ %   1 = channel active
		#~ %   0 = channel masked (off)
		#~ %   n = 32 bit binary string

		bcmd = 13
		p = list('00000000000000000000000000000000')
		for nn in range(0,len(p)):
			p[nn] = '1'
		q = "".join(p)
		q_val = q[::-1]
		data = np.array([self.startcode,bcmd,0,int(q_val[0:8],2),int(q_val[8:16],2),int(q_val[16:24],2),int(q_val[24:32],2),self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)	
	
	def set_mask_on_forcefully(self):
		#~ %   Sends "b" command instruction to specify a channel mask.
		#~ %   n represents the bit pattern for active and masked channels.
		#~ %   1 = channel active
		#~ %   0 = channel masked (off)
		#~ %   n = 32 bit binary string

		bcmd = 13
		p = list('00000000000000000000000000000000')
		#~ for nn in range(0,len(p)):
			#~ p[nn] = '1'
		q = "".join(p)
		q_val = q[::-1]
		data = np.array([self.startcode,bcmd,0,int(q_val[0:8],2),int(q_val[8:16],2),int(q_val[16:24],2),int(q_val[24:32],2),self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)	
										
	def set_mask(self,n):
		#~ %   Sends "b" command instruction to specify a channel mask.
		#~ %   n represents the bit pattern for active and masked channels.
		#~ %   1 = channel active
		#~ %   0 = channel masked (off)
		#~ %   n = 32 bit binary string

		bcmd = 13
		q_val = q[::-1]
		data = np.array([self.startcode,bcmd,0,int(q_val[0:8],2),int(q_val[8:16],2),int(q_val[16:24],2),int(q_val[24:32],2),self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def single_channel_mask(self,n):
		
		bcmd = 13
		p = list('00000000000000000000000000000000')
		if p > 0:
			p[n-1] = '1'
		q = "".join(p)
		q_val = q[::-1]
		data = np.array([self.startcode,bcmd,0,int(q_val[0:8],2),int(q_val[8:16],2),int(q_val[16:24],2),int(q_val[24:32],2),self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)

	def execute_program(self,n):
		bcmd = 1
		data = np.array([self.startcode,bcmd,0,0,0,floor(n/256),n % 256,self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def write_chipmem(self,n):
		bcmd = 7
		data = np.array([self.startcode,bcmd,0,n[3],n[2],n[1],n[0],self.endcode,1,1,1,1,1,1,1,1],dtype='uint8')
		self.ser.write(data)
		
	def write_array_pattern_16bit(self, data):
		# Sends appropriate commands to write a full set of pattern information
		# to maizechip FPGA program.
		#
		# data is a 32 element vector of 16 bit numbers (0...65535) 
		# representing either phase or amplitude information for a pattern
		#
		# For example, data(5) = 46
		# This is 46 clock tics of phase delay or charge time for channel 5
		
		
		for i in xrange(0,len(data),2):
			bindata_tmp1 = format(int(data[i]),'016b')
			bindata_tmp2 = format(int(data[i+1]),'016b')
			a1 = int(bindata_tmp1[0:8],2)
			b1 = int(bindata_tmp1[8:16],2)
			a2 = int(bindata_tmp2[0:8],2)
			b2 = int(bindata_tmp2[8:16],2)
			data16bit_tmp = np.array([b1,a1,b2,a2])
			self.write_chipmem(data16bit_tmp)
		
	def go(self):
		self.execute_program(0)

	def stop(self):
		self.stop_execution()
	
	def __init__(self,x):
		self.ser = x.ser
		self.startcode = 170
		self.endcode = 85
		self.bcmd5 = 5
	
		
