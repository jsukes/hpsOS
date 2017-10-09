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
import os, subprocess
import ctypes
from scipy.signal import hilbert
from scipy.signal import savgol_filter

IPCSOCK = "/home/five_hundred_khz_array/Desktop/devVersion/server/lithium_ipc"
x = FPGA()
a = a_funcs(x)
b = b_funcs(x)
b.stop()
trigWidth = 5e-6
cmsg = '4i100s'


def loadArray():
	### units in the file are mm
	tmp = sio.loadmat('../dataFiles/256x2cm_Hemispherical_Array_CAD_Coordinates.mat')
	XC,YC,ZC = tmp['XCAD'][7:], tmp['YCAD'][7:], tmp['ZCAD'][7:]
	R = (XC**2+YC**2)**0.5
	XR = R*np.cos(np.arctan2(YC,XC)-1*pi/4)
	YR = R*np.sin(np.arctan2(YC,XC)-1*pi/4)

	xyz,nxyz = np.zeros((256,3)),np.zeros((256,3)),
	
	for m in range(0,256):
		xyz[m,0],xyz[m,1],xyz[m,2] = XR[m], YR[m], ZC[m]

		r = (xyz[m,0]**2+xyz[m,1]**2+xyz[m,2]**2)**0.5
		nxyz[m,0],nxyz[m,1],nxyz[m,2] = -xyz[m,0]/r,-xyz[m,1]/r,-xyz[m,2]/r
	
	return xyz,nxyz


def calcDelays(pts):
	xyz,nxyz = loadArray()
	delays = np.zeros((xyz.shape[0],pts.shape[0]))
	for m in range(0,pts.shape[0]):
		R = ((xyz[:,0]-pts[m,0])**2+(xyz[:,1]-pts[m,1])**2+(xyz[:,2]-pts[m,2])**2)**0.5
		t = R/1.49
		
		delays[:,m] = (np.max(t)-t)*100
		
	return delays.astype(int)


def makePattern(npts):
	pts = np.zeros((npts,3))
	theta = np.linspace(0, 2*np.pi*(npts-1)/npts, npts)
	pts[:,0] = 10*np.cos(theta)
	pts[:,1] = 10*np.sin(theta)
	#~ pts[:,2] = 0*np.linspace(0,10,npts)

	
	return pts
	

def connectHPS():
	dd = time.asctime().split(' ')
	fnamea = '{}{}{}{}'.format('ips',dd[4],dd[1],dd[2])
	libc = ctypes.CDLL('libc.so.6')

	if os.path.isfile(fnamea) and os.path.isfile("ipList"):
		dummy = subprocess.check_output("bash sshIntoFPGAs.sh", shell=True)	
	else:
		os.system("rm ipList")
		os.system("touch ipList")	
		cmdstr = '{}{}'.format('touch ', fnamea)	
		os.system(cmdstr)
		dummy = subprocess.check_output("bash getIPs_sshIntoFPGAs.sh", shell=True)
		aaa = dummy.split('into hpsIP: ')
		ips = []
		for bbb in aaa[1:]:
			if bbb[0:3] not in ips:
				ips.append(bbb[0:3])	
		
		for ip in ips:		
			cmdstr = '{}{}{}'.format('echo "192.168.1.',ip,'" >> ipList')	
			os.system(cmdstr)
		
		print ips

		
def uploadDelays():
	
	chargetime = 500*np.ones(256)
	chargetime = chargetime
		
	for mother in range(0,8):
		b.select_motherboard(mother)
		chanset = np.asarray(range(0,32))+mother*32 
		b.set_chipmem_wloc(0)
		b.write_array_pattern_16bit(chargetime[chanset])
		for mm in range(1,501):
			b.set_chipmem_wloc(mm)
			b.write_array_pattern_16bit(chargetime[chanset])


def uploadDelays2(delays): #~ one with bubbles and without bubbles
	
	chargetime = 500*np.ones(256)
	chargetime2 = 100*np.ones(256)
		
	for mother in range(0,8):
		b.select_motherboard(mother)
		chanset = np.asarray(range(0,32))+mother*32 
		b.set_chipmem_wloc(0)
		b.write_array_pattern_16bit(chargetime[chanset])
		b.set_chipmem_wloc(1)
		b.write_array_pattern_16bit(chargetime2[chanset])
		for ptn in range(0,delays.shape[1]):
			#~ print delays[chanset,ptn]+chargetime[chanset]
			b.set_chipmem_wloc(2+ptn)
			dd = (delays[chanset,ptn]+chargetime[chanset]).astype(int)
			b.write_array_pattern_16bit(dd)

					
def uploadCommands():
	b.stop_execution()
	
	b.set_imem_wloc(0)
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_amp(0)
	a.wait(1)
	a.loadincr_chipmem(1,2)
	a.wait(1)
	a.set_phase(0)
	a.wait(1)
	a.fire(0)
	
	a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
	a.waitsec(5e-6)
	a.set_trig(0)
	a.waitsec(50e-6)
	a.halt()
	

ff = open("data_pipe","w",0)


def setTrigDelay(td):
	msg = struct.pack(cmsg,0,td,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
	
def setRecLen(rl):
	msg = struct.pack(cmsg,1,rl,0,0,"")
	ff.write(msg)
	time.sleep(0.05)

def setPacketSize(ps):
	msg = struct.pack(cmsg,11,ps,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
		
def setTimeout(to):
	msg = struct.pack(cmsg,2,to,0,0,"")
	ff.write(msg)
	time.sleep(0.05)

def setDataAcqMode(dam):
	msg = struct.pack(cmsg,3,dam,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
	
def setDataAlloc(l1,l2,l3):
	msg = struct.pack(cmsg,4,l1,l2,l3,"")
	ff.write(msg)
	time.sleep(0.05)
	
def allocMem():
	msg = struct.pack(cmsg,5,1,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
	
def dataAcqStart(da):
	msg = struct.pack(cmsg,6,da,0,0,"")
	ff.write(msg)
	time.sleep(0.05)

def setIDX(id1,id2,id3):
	msg = struct.pack(cmsg,7,id1,id2,id3,"")
	ff.write(msg)
	
def closeFPGA():
	msg = struct.pack(cmsg,8,0,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
	
def resetVars():
	msg = struct.pack(cmsg,9,0,0,0,"")
	ff.write(msg)
	time.sleep(0.05)
	
def saveData(fname):
	msg = struct.pack(cmsg,10,0,0,0,fname)
	ff.write(msg)
	time.sleep(0.05)
	
def shutdown():
	msg = struct.pack(cmsg,17,0,0,0,"")
	ff.write(msg)
	time.sleep(0.05)

def connectIPC():
	ipcsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	ipcsock.connect(IPCSOCK)
	return ipcsock

ipcsock = connectIPC()
	
def goSafely(npulses,PRF,pack_size,timeOut):
	resetVars()
	nlocs = 1 #~ npulses * nlocs
	setTrigDelay(150)
	setRecLen(2048)
	setPacketSize(pack_size)
	setTimeout(timeOut)
	setDataAlloc(1,npulses,nlocs)
	allocMem()
	dataAcqStart(1)
	
	tt = np.zeros(npulses*nlocs)
	for n in range(0,npulses):
		for locn in range(0,nlocs):
			t0 = time.time()
			setIDX(0,n,locn)
			b.set_imem_wloc(4)
			a.loadincr_chipmem(1,int(np.mod(locn,20)+1))
			b.go()
			time.sleep(1.0/PRF-0.0002)
			#~ if n>0:
				#~ b.execute_program(4)
			
			dummy = ipcsock.recv(4,socket.MSG_WAITALL)
			dt = (time.time()-t0)*1e3
			tt[n*nlocs+locn] = (time.time()-t0)
		
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(1)
	setTimeout(5e6)
	saveData("hello")


def steerSafely():
	pts = makePattern(30)
	delays = calcDelays(pts)
	uploadDelays2(delays)
	uploadCommands()
	nlocs = pts.shape[0]
	npulses = 20
	
	pack_size = 512
	timeOut = 1100
	PRF = 150
	
	resetVars()
	
	setTrigDelay(150)
	
	setRecLen(2048)
	setPacketSize(pack_size)
	setTimeout(timeOut)
	setDataAlloc(1,npulses,nlocs)
	allocMem()
	dataAcqStart(1)
	
	tt = np.zeros(npulses*nlocs)
	for ctloc in range(0,1):
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,int(ctloc))
		for plsn in range(0,npulses):
			for locn in range(0,nlocs):
				t0 = time.time()
				setIDX(ctloc,plsn,locn)
				b.stop_execution()
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				time.sleep(1.0/PRF-0.0002)

				
				dummy = ipcsock.recv(4,socket.MSG_WAITALL)
				dt = (time.time()-t0)*1e3
				tt[plsn*nlocs+locn] = (time.time()-t0)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(1)
	setTimeout(5e6)
	saveData("steerTest1")


def sortData(recLen,nlocs,npulses,nct):
	#~ data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id2*g_idx3len*2*g_recLen;
	#~ data_idx += g_id3*2*g_recLen;
	c = np.fromfile("steerTest1",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	f = np.zeros((nct,npulses,nlocs,recLen,64))
	#~ print len(d['c1'])/(2*2048*256),len(c)
	
	for mb in range(0,8):
		didx0 = mb*nct*nlocs*npulses*2*recLen
		for ctn in range(0,nct):
			didx1 = didx0+ctn*npulses*nlocs*2*recLen
			for plsn in range(0,npulses):
				didx2 = didx1+plsn*nlocs*2*recLen
				for locn in range(0,nlocs):
					didx = didx2+locn*2*recLen
					
					dd = np.zeros((recLen,8))
					dd[:,7] = d['c1'][didx:(didx+2*recLen):2]
					dd[:,3]  = d['c1'][(didx+1):(didx+2*recLen):2]
					dd[:,6]  = d['c2'][didx:(didx+2*recLen):2]
					dd[:,2]  = d['c2'][(didx+1):(didx+2*recLen):2]
					dd[:,5]  = d['c3'][didx:(didx+2*recLen):2]
					dd[:,1]  = d['c3'][(didx+1):(didx+2*recLen):2]
					dd[:,4]  = d['c4'][didx:(didx+2*recLen):2]
					dd[:,0]  = d['c4'][(didx+1):(didx+2*recLen):2]
				
					f[ctn,plsn,locn,:,mb*8:(mb+1)*8] = dd
				
	return f

		
def steerSafelyPlot():
	xx = np.linspace(-20,20,81)
	yy = np.linspace(-20,20,81)
	XX,YY = np.meshgrid(xx,yy)
	ZZ = np.zeros(XX.shape)+0.0
	xyz,nxyz = loadArray()
	
	els = np.array([8, 10, 12, 15, 18, 23, 34, 38,		# board 1 (top, stack 1)
			40, 44, 48, 50, 56, 61, 68, 78,		 
			79, 86, 89, 91, 92, 93, 95, 97,
			100, 103, 107, 111, 112, 115, 117, 127,		# board 4 (bottom, stack 1)
			136, 142, 150, 156, 157, 159, 167, 168,			# board 5 (top, stack 2)
			169, 173, 176, 177, 181, 183, 189, 191,
			201, 206, 211, 225, 229, 236, 238, 242,
			246, 248, 249, 251, 253, 254, 258, 260])-8
	
	recLen = 2048		
	
	def getField(dtt,pt):
		ev = 5
		t0 = 20
		t1 = recLen-20
		T = 153+dtt
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))/2.0
		window = np.ones(ev)/float(ev)
		r = (tw-5)*1.49
		AA = np.zeros(XX.shape)
		f = sortData(recLen,1,1,1)

		for n in range(0,64):
			ff = f[0,0,0,t0:t1:ev,n]
			ff-=np.mean(ff)
			ff[ff>=129]=128.9
			wf = 8*ff/(129.0**2-ff**2)**0.5
			wf -= np.mean(wf)
			
			hwf = np.abs(hilbert(wf))
			#~ hwf/=np.max(hwf[len(hwf)/4.0:3*len(hwf)/4.0])
			hwf[0] = 0
			#~ hwf[hwf<(0.5*max(hwf))]=0
			#~ hwf = np.convolve(hwf,window)
			rr = ((xyz[els[n],0]-XX)**2+(xyz[els[n],1]-YY)**2+(xyz[els[n],2]-ZZ)**2)**0.5
			rridx = ((rr-np.min(r[::ev]))/(np.max(r[::ev])-np.min(r[::ev]))*len(r[::ev])).astype(int)
			rridx[rridx<0] = 0
			rridx[rridx>=len(hwf)] = 0
			cc = hwf[rridx]
			
			#~ rr = ((pt[0]-XX)**2+(pt[1]-YY)**2+(pt[2]-ZZ)**2)**0.5
			bb = 1#np.exp(-0.5*(rr/23.5)**2)
			AA+=cc*bb
		
		return AA
		
	pts = makePattern(20)
	delays = calcDelays(pts)
	uploadDelays2(delays)
	uploadCommands()
	nlocs = pts.shape[0]
	npulses = 10
	
	pack_size = 512
	timeOut = 1100
	PRF = 150
	
	resetVars()
	
	setTrigDelay(150)
	
	setRecLen(recLen)
	setPacketSize(pack_size)
	setTimeout(timeOut)
	setDataAlloc(1,1,1)
	allocMem()
	dataAcqStart(1)
	fig,ax = plt.subplots(1,1)
	ax.set_aspect('equal')
	ax.set_xlim([-25,25])
	ax.set_ylim([-25,25])
	fig.canvas.draw()
	ax.contourf(XX,YY,ZZ*0)
	axbg = fig.canvas.copy_from_bbox(ax.bbox)
	
	tt = np.zeros(npulses*nlocs)
	for ctloc in range(0,1):
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,int(ctloc))
		for plsn in range(0,npulses):
			for locn in range(0,nlocs):
				t0 = time.time()
				setIDX(0,0,0)
				b.stop_execution()
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				
				dummy = ipcsock.recv(4,socket.MSG_WAITALL)
				saveData("steerTest1")
				time.sleep(0.001)
				#~ mm=[0,0]
				AA = np.zeros(XX.shape)
				for dtt in range(2,11):
					AA += getField(dtt,pts[int(np.mod(locn,nlocs)),:])
					#~ if np.max(AA)>mm[0]:
						#~ mm[0] = np.max(AA)
						#~ mm[1] = dtt
					
				fig.canvas.restore_region(axbg)
				ax.contourf(XX,YY,AA)
				ax.set_title(dtt)
				ax.plot(pts[int(np.mod(locn,nlocs)),0],pts[int(np.mod(locn,nlocs)),1],'bo')
				ax.plot(pts[:,0],pts[:,1],'k--')
				ax.plot(pts[:,0]*0.9,pts[:,1]*0.9,'k--')
				ax.plot(pts[:,0]*1.1,pts[:,1]*1.1,'k--')
				fig.canvas.blit(ax.bbox)
				plt.pause(0.1e-7)
				ax.clear()
					
					
				#~ print 'dtt=', mm[1]
				dt = (time.time()-t0)
				tt[plsn*nlocs+locn] = (time.time()-t0)
				
				if(dt<1.0/PRF):
					time.sleep(1.0/PRF-dt)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	


def pingSafely():
	npulses = 256
	uploadDelays()
	uploadCommands()
	
	resetVars()
	
	pack_size = 512
	timeOut = 1100
	PRF = 100
	
	setTrigDelay(0)
	setRecLen(4096)
	setPacketSize(pack_size)
	setTimeout(timeOut)
	setDataAlloc(1,1,npulses)
	allocMem()
	dataAcqStart(1)
	
	tt = np.zeros(npulses)
	for plsn in range(0,npulses):
		t0 = time.time()
		setIDX(0,0,plsn)
		b.set_imem_wloc(4)
		a.loadincr_chipmem(1,1)
		for mother in range(0,8):
			b.select_motherboard(mother)
			if np.floor(plsn/32) == mother:
				print mother,plsn
				b.single_channel_mask(np.mod(plsn,32)+1)
			else:
				b.single_channel_mask(-1)
		if plsn>31:
			b.select_motherboard(0)
			b.single_channel_mask(1)
			b.single_channel_mask(-10)
		b.go()
		for mother in range(0,8):
			b.select_motherboard(mother)
			b.single_channel_mask(-1)
		time.sleep(1.0/PRF-0.0002)

		
		dummy = ipcsock.recv(4,socket.MSG_WAITALL)
		dt = (time.time()-t0)*1e3
		tt[plsn] = (time.time()-t0)
		
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(1)
	setTimeout(5e6)
	saveData("pingTest1")


def sortData(recLen,nlocs,npulses,nct):
	#~ data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id2*g_idx3len*2*g_recLen;
	#~ data_idx += g_id3*2*g_recLen;
	c = np.fromfile("steerTest1",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	f = np.zeros((nct,npulses,nlocs,recLen,64))
	#~ print len(d['c1'])/(2*2048*256),len(c)
	
	for mb in range(0,8):
		didx0 = mb*nct*nlocs*npulses*2*recLen
		for ctn in range(0,nct):
			didx1 = didx0+ctn*npulses*nlocs*2*recLen
			for plsn in range(0,npulses):
				didx2 = didx1+plsn*nlocs*2*recLen
				for locn in range(0,nlocs):
					didx = didx2+locn*2*recLen
					
					dd = np.zeros((recLen,8))
					dd[:,7] = d['c1'][didx:(didx+2*recLen):2]
					dd[:,3]  = d['c1'][(didx+1):(didx+2*recLen):2]
					dd[:,6]  = d['c2'][didx:(didx+2*recLen):2]
					dd[:,2]  = d['c2'][(didx+1):(didx+2*recLen):2]
					dd[:,5]  = d['c3'][didx:(didx+2*recLen):2]
					dd[:,1]  = d['c3'][(didx+1):(didx+2*recLen):2]
					dd[:,4]  = d['c4'][didx:(didx+2*recLen):2]
					dd[:,0]  = d['c4'][(didx+1):(didx+2*recLen):2]
				
					f[ctn,plsn,locn,:,mb*8:(mb+1)*8] = dd
				
	return f


def plot():
	recLen = 2048
	c = np.fromfile("steerTest1",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	print np.min(d['c1']),np.max(d['c1'])
	for n in range(0,20):
		plt.plot(d['c1'][recLen*2*n+1500:recLen*2*n+3500:2]+32*(8+4*n))
	#~ plt.plot(d['c2'][0:4096*28*2:2]+32*10)
	#~ plt.plot(d['c3'][0:4096*28*2:2]+32*12)
	#~ plt.plot(d['c4'][0:4096*28*2:2]+32*14)
	#~ plt.plot(d['c1'][1:4096*28*2+1:2]+32*9)
	#~ plt.plot(d['c2'][1:4096*28*2+1:2]+32*11)
	#~ plt.plot(d['c3'][1:4096*28*2+1:2]+32*13)
	#~ plt.plot(d['c4'][1:4096*28*2+1:2]+32*15)
	plt.show()


def plotSteerDelays():
	ELEMENTS = np.array([8, 10, 12, 15, 18, 23, 34, 38,		# board 1 (top, stack 1)
			40, 44, 48, 50, 56, 61, 68, 78,		 
			79, 86, 89, 91, 92, 93, 95, 97,
			100, 103, 107, 111, 112, 115, 117, 127,		# board 4 (bottom, stack 1)
			136, 142, 150, 156, 157, 159, 167, 168,			# board 5 (top, stack 2)
			169, 173, 176, 177, 181, 183, 189, 191,
			201, 206, 211, 225, 229, 236, 238, 242,
			246, 248, 249, 251, 253, 254, 258, 260])-8
	

	recLen = 2048
	pts = makePattern(10)
	delays = calcDelays(pts)
	nlocs = pts.shape[0]
	npulses = 10
	nct = 2
	
	f = sortData(recLen,nlocs,npulses,nct)
	
	#~ f = np.mean(g[0,:,:,:,:],axis=1)#-3*np.mean(g[1,:,:,:,:],axis=1)
	#~ f -= np.mean(f)	
	#~ f2 = np.mean(g[1,:,:,:,:],axis=1)#-3*np.mean(g[1,:,:,:,:],axis=1)
	#~ f2 -= np.mean(f2)	
	el = 40
	t0 = 10
	t1 = recLen-10
	tw = np.linspace(145,145+(t1-t0)/20,(t1-t0))
	for m in range(0,nlocs):
		tt = []
		for n in range(0,64):
			wf = f[0,0,m,t0:t1,n]-np.mean(f[0,0,m,t0:t1,n])
			hwf = savgol_filter(np.abs(hilbert(wf)),41,2)
			aa = np.argwhere(hwf/np.max(hwf)>0.9)[0]/20.0
			tt.append(aa)
			#~ plt.plot(np.abs(hilbert(f[0,750:1750,n]-np.mean(f[0,750:1750,n])))+128*n)
			#~ plt.plot(f[0,750:1750,n]-np.mean(f[0,750:1750,n])+128*n)
			#~ if np.mod(n,3) == 0:
				#~ plt.plot(f[m,t0:t1,n]+n*64)
				#~ plt.plot(np.abs(hilbert(f[m,t0:t1,n]))+n*64)
		plt.plot(tw,f[0,0,m,t0:t1,el]-np.mean(f[0,0,m,t0:t1,el]))
		plt.plot(tw,(f[1,0,m,t0:t1,el]-np.mean(f[1,0,m,t0:t1,el]))*3+60)
		plt.plot(tw,savgol_filter(np.abs(hilbert(f[0,0,m,t0:t1,el]-np.mean(f[0,0,m,t0:t1,el]))),41,2))
		plt.plot(tw,savgol_filter(np.abs(hilbert(f[1,0,m,t0:t1,el]-np.mean(f[1,0,m,t0:t1,el]))),41,2)*3+60)
		plt.show()
		dt = tt-np.max(tt)
		dtc = np.max(delays[ELEMENTS,m])-delays[ELEMENTS,m]
		plt.plot(dt-np.mean(dt),'b-')
		plt.plot((dtc-np.mean(dtc))/100,'g-')
		plt.show()
		
		

def plotPing():
	ELEMENTS = np.array([8, 10, 12, 15, 18, 23, 34, 38,		# board 1 (top, stack 1)
			40, 44, 48, 50, 56, 61, 68, 78,		 
			79, 86, 89, 91, 92, 93, 95, 97,
			100, 103, 107, 111, 112, 115, 117, 127,		# board 4 (bottom, stack 1)
			136, 142, 150, 156, 157, 159, 167, 168,			# board 5 (top, stack 2)
			169, 173, 176, 177, 181, 183, 189, 191,
			201, 206, 211, 225, 229, 236, 238, 242,
			246, 248, 249, 251, 253, 254, 258, 260])-8
	
	#~ data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id2*g_idx3len*2*g_recLen;
	#~ data_idx += g_id3*2*g_recLen;

	npts = 256
	recLen = 2048
	
	c = np.fromfile("pingTest1",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	f = np.zeros((npts,recLen,64))
	#~ print len(d['c1'])/(2*2048*256),len(c)
	
	for m in range(0,8):
		didx0 = m*npts*2*recLen
		for n in range(0,256):
			didx = didx0+n*2*recLen
			
			dd = np.zeros((recLen,8))
			dd[:,7] = d['c1'][didx:(didx+2*recLen):2]
			dd[:,3]  = d['c1'][(didx+1):(didx+2*recLen):2]
			dd[:,6]  = d['c2'][didx:(didx+2*recLen):2]
			dd[:,2]  = d['c2'][(didx+1):(didx+2*recLen):2]
			dd[:,5]  = d['c3'][didx:(didx+2*recLen):2]
			dd[:,1]  = d['c3'][(didx+1):(didx+2*recLen):2]
			dd[:,4]  = d['c4'][didx:(didx+2*recLen):2]
			dd[:,0]  = d['c4'][(didx+1):(didx+2*recLen):2]
			
			f[n,:,m*8:(m+1)*8] = dd
			

	for m in range(0,256):
		if m in ELEMENTS:
			print m+8, np.argmax(np.max(f[m,:,:],axis=0))
			plt.plot(np.max(f[m,:,:],axis=0))
			plt.show()









