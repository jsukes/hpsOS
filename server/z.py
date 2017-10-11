# Loads all functions including a commands, b commands, tests, and basic steering
# type "from varLoader import *" on startup of python using "sudo python"
#
# MaizeChipPy 1.0 - JJM - May 2017
import sys
import socket, select
import numpy as np
from fpgaClass import *
from dataServer import *
import scipy.io as sio
from math import pi
import matplotlib.pyplot as plt
import struct
import time
import os, subprocess
import ctypes
from scipy.signal import hilbert
from scipy.signal import savgol_filter
from scipy.signal import butter,lfilter,freqz

RECVBOARDS = 8
MAIZEBOARDS = 8
x = FPGA()
a = a_funcs(x)
b = b_funcs(x)
b.stop()
d = dataServer()
#~ d.connect()

els = np.array([8, 10, 12, 15, 18, 23, 34, 38,		# board 1 (top, stack 1)
			40, 44, 48, 50, 56, 61, 68, 78,		 
			79, 86, 89, 91, 92, 93, 95, 97,
			100, 103, 107, 111, 112, 115, 117, 127,		# board 4 (bottom, stack 1)
			136, 142, 150, 156, 157, 159, 167, 168,			# board 5 (top, stack 2)
			169, 173, 176, 177, 181, 183, 189, 191,
			201, 206, 211, 225, 229, 236, 238, 242,
			246, 248, 249, 251, 253, 254, 258, 260])-8


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


def unmask():	
	for mb in range(0,MAIZEBOARDS):
		b.select_motherboard(mb)
		b.set_mask_off_forcefully()
	
		
def uploadDelays():
	
	chargetime = 500*np.ones(MAIZEBOARDS*32)
	chargetime = chargetime
		
	for mother in range(0,MAIZEBOARDS):
		b.select_motherboard(mother)
		chanset = np.asarray(range(0,32))+mother*32 
		b.set_chipmem_wloc(0)
		b.write_array_pattern_16bit(chargetime[chanset])
		for mm in range(1,501):
			b.set_chipmem_wloc(mm)
			b.write_array_pattern_16bit(chargetime[chanset])


def uploadDelays2(delays): #~ one with bubbles and without bubbles
	
	chargetime = 500*np.ones(MAIZEBOARDS*32)
	chargetime2 = 100*np.ones(MAIZEBOARDS*32)
		
	for mother in range(0,MAIZEBOARDS):
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
	a.loadincr_chipmem(1,0)
	a.wait(1)
	a.set_phase(0)
	a.wait(1)
	a.fire(0)
	
	a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
	a.waitsec(5e-6)
	a.set_trig(0)
	a.waitsec(50e-6)
	a.halt()

	
def uploadCommands2():
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
	
	a.waitsec(400e-6)
	
	a.loadincr_chipmem(1,1)
	a.wait(1)
	a.set_amp(0)
	a.wait(1)
	a.fire(0)
	
	a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
	a.waitsec(5e-6)
	a.set_trig(0)
	a.waitsec(50e-6)
	a.halt()


def shutdown():
	d.connect()
	d.shutdown()
	
	
def loadSortData(recLen,nlocs,npulses,nct):
	#~ data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id2*g_idx3len*2*g_recLen;
	#~ data_idx += g_id3*2*g_recLen;
	c = np.fromfile("bmode_pings2",dtype=np.uint32,count=-1)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	f = np.zeros((nct,npulses,nlocs,recLen,RECVBOARDS*8))
	#~ print len(d['c1'])/(2*2048*256),len(c)
	
	for mb in range(0,RECVBOARDS):
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

	
def bsortData(ipcdata,recLen,nlocs,npulses,nct):
	#~ data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
	#~ data_idx += g_id2*g_idx3len*2*g_recLen;
	#~ data_idx += g_id3*2*g_recLen;
	#~ c = np.fromfile("steerTest1",dtype=np.uint32,count=-1)
	#~ print len(ipcdata)
	CDATASTRUCT = struct.Struct('{}{}{}'.format('= ',2*recLen*nlocs*npulses*nct*RECVBOARDS,'L')) 
	c = np.array(CDATASTRUCT.unpack(ipcdata),dtype=np.uint32)
	dt14 = np.dtype((np.uint32,{'c1':(np.uint8,0),'c2':(np.uint8,1),'c3':(np.uint8,2),'c4':(np.uint8,3)}))
	d = c.view(dt14)
	f = np.zeros((nct,npulses,nlocs,recLen,RECVBOARDS*8))
	
	for mb in range(0,RECVBOARDS):
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
	ZZ = np.zeros(XX.shape)#-2.0*(XX+20)/40.0-1
	xyz,nxyz = loadArray()
	

	RR = np.zeros((81,81,RECVBOARDS*8))
	for n in range(0,RECVBOARDS*8):
		RR[:,:,n] = ((xyz[els[n],0]-XX)**2+(xyz[els[n],1]-YY)**2+(xyz[els[n],2]-ZZ)**2)**0.5
		
	recLen = 2048
	
	def getField(ipcdata,pt,dtt):
		ev = 5
		t0 = 20
		t1 = recLen-20
		AA = np.zeros(XX.shape)
		f = bsortData(ipcdata,recLen,1,1,1)
		T = 150
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))[::ev]-100
		ll = len(tw)
		hwf = np.zeros(ll+1)
		
		for n in range(0,RECVBOARDS*8):
			ff = f[0,0,0,t0:t1:ev,n]
			ff-=np.mean(ff)
			ff[ff>=129]=128.9
			wf = 8*ff/(129.0**2-ff**2)**0.5
			wf -= np.mean(wf)
			hwf[0:-1] = np.abs(hilbert(wf))
			hwf[ll] = 0
			rr = ((xyz[els[n],0]-XX)**2+(xyz[els[n],1]-YY)**2+(xyz[els[n],2]-ZZ)**2)**0.5					
			r = (tw+dtt-5)*1.48
			rridx = ((rr-np.min(r))/(np.max(r)-np.min(r))*len(r)).astype(int)
			rridx[rridx<0] = ll
			rridx[rridx>=len(hwf)] = ll
			
			AA+=hwf[rridx]
		
		return AA
		
	def getFieldb(ipcdata,pt,dtt):
		ev = 10
		#~ window = np.ones(ev)/float(ev)
		t0 = 20
		t1 = recLen-20
		AA = np.zeros(XX.shape)
		f = bsortData(ipcdata,recLen,1,1,1)[:,:,:,t0:t1:ev,:]
		T = 150
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))[::ev]-100
		ll = len(tw)
		hwf = np.zeros(ll+1)
		f = f-np.mean(f,axis=3)
		f[f>=129]=128.9
		wfa = 8*f/(129.0**2-f**2)**0.5
		r = (tw+dtt-5)*1.48
		rmn,lr = r.min(),len(r)
		dr = r.max()-rmn
		for n in range(0,64):
			wf = wfa[0,0,0,:,n]
			hwf[0:-1] = np.abs(hilbert(wf))
			hwf[ll] = 0
			rr = RR[:,:,n]				
			
			rridx = ((rr-rmn)*lr/dr).astype(int)
			rridx[rridx<0] = ll
			rridx[rridx>ll] = ll
			
			AA+=hwf[rridx]
				
		AA/=AA.max()
		return AA**3
	
	def getwf(ipcdata,pt,dtt):
		ev = 1
		#~ window = np.ones(ev)/float(ev)
		t0 = 20
		t1 = recLen-20
		AA = np.zeros(XX.shape)
		f = bsortData(ipcdata,recLen,1,1,1)[:,:,:,t0:t1:ev,:]
		T = 150
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))[::ev]-100
		ll = len(tw)
		hwf = np.zeros(ll+1)
		f = f-np.mean(f,axis=3)
		f[f>=129]=128.9
		wfa = 8*f/(129.0**2-f**2)**0.5
		
		return wfa
			
	pts = makePattern(5)
	delays = calcDelays(pts)
	uploadDelays2(delays)
	uploadCommands()
	nlocs = pts.shape[0]
	npulses = 4
	
	pack_size = 512
	timeOut = 1100
	PRF = 2
	
	d.resetVars()
	
	d.setTrigDelay(150)
	
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataAlloc(1,1,1)
	d.allocMem()
	d.dataAcqStart(1)
	
	fig,ax = plt.subplots(1,1)
	#~ ax.set_aspect('equal')
	ax.set_xlim([0,2048])
	ax.set_ylim([-25,25])
	fig.canvas.draw()
	#~ ax.contourf(XX,YY,ZZ*0)
	#~ axbg = fig.canvas.copy_from_bbox(ax.bbox)
	
	tt = np.zeros(npulses*nlocs)
	for ctloc in range(0,1):
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,int(ctloc))
		for plsn in range(0,npulses):
			for locn in range(0,nlocs):
				t0 = time.time()
				setIDX(0,0,0)
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,1)
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				d.ipcWait()
				cc = d.getData()
				
				time.sleep(0.5/PRF)
				
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,0)
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				d.ipcWait()
				d.getDataIPC()
				
				for dtt in range(-12,-11):
					wfa = getwf(dd,pts[int(np.mod(locn,nlocs)),:],dtt)
					wfb = getwf(cc,pts[int(np.mod(locn,nlocs)),:],dtt)
					ax.plot(wfa[0,0,0,500:,0])
					ax.plot(3.5*wfb[0,0,0,500:,0])
					ax.set_xlim([0,2048])
					ax.set_ylim([-10,10])
					
					ax.set_title(dtt)
					plt.pause(0.1e-6)
					ax.clear()
				
				dt = (time.time()-t0)+0.00015
				
				if(dt>0 and dt<1.0/PRF):
					time.sleep(1.0/PRF-dt)
				#~ ax.clear()	
				tt[plsn*nlocs+locn] = (time.time()-t0)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	

def steerSafelyGNUPlot():
	
	xx = np.linspace(-20,20,81)
	yy = np.linspace(-20,20,81)
	XX,YY = np.meshgrid(xx,yy)
	ZZ = np.zeros(XX.shape)#-2.0*(XX+20)/40.0-1
	xyz,nxyz = loadArray()
	
	RR = np.zeros((len(xx),len(yy),RECVBOARDS*8))
	for n in range(0,RECVBOARDS*8):
		RR[:,:,n] = ((xyz[els[n],0]-XX)**2+(xyz[els[n],1]-YY)**2+(xyz[els[n],2]-ZZ)**2)**0.5
		
	recLen = 2048
	
	unmask()
		
	def getFieldb(ipcdata,pt,dtt):
		ev = 10
		#~ window = np.ones(ev)/float(ev)
		t0 = 20
		t1 = recLen-20
		AA = np.zeros(XX.shape)
		f = bsortData(ipcdata,recLen,1,1,1)[:,:,:,t0:t1:ev,:]
		T = 150
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))[::ev]-100
		ll = len(tw)
		hwf = np.zeros(ll+1)
		f = f-np.mean(f,axis=3)
		f[f>=129]=128.9
		wfa = 8*f/(129.0**2-f**2)**0.5
		r = (tw+dtt-5)*1.48
		rmn,lr = r.min(),len(r)
		dr = r.max()-rmn
		for n in range(0,RECVBOARDS*8):
			wf = wfa[0,0,0,:,n]
			hwf[0:-1] = np.abs(hilbert(wf-np.mean(wf)))
			hwf[ll] = 0
			rr = RR[:,:,n]				
			
			rridx = ((rr-rmn)*lr/dr).astype(int)
			rridx[rridx<0] = ll
			rridx[rridx>ll] = ll
			
			AA+=hwf[rridx]
				
		#~ AA/=AA.max()
		return AA#**3
		
	pts = makePattern(20)
	delays = calcDelays(pts)
	uploadDelays2(delays)
	uploadCommands()
	nlocs = pts.shape[0]
	npulses = 100
	
	pack_size = 512
	timeOut = 1100
	PRF = 100
	
	d.resetVars()
	
	d.setTrigDelay(150)
	
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataAlloc(1,1,1)
	d.allocMem()
	d.dataAcqStart(1)
	
	pt = subprocess.Popen(['gnuplot','-e'],shell=True,stdin=subprocess.PIPE,)
	pt.stdin.write("set pm3d map; set size square\n")
	
	tt = np.zeros(npulses*nlocs)
	for ctloc in range(0,1):
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,int(ctloc))
		for plsn in range(0,npulses):
			for locn in range(0,nlocs):
				t0 = time.time()
				d.setIDX(0,0,0)
				
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,0)				
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				d.ipcWait()
				cc = d.getDataIPC()
				
				dtt = -(delays[:,locn].max()-delays[:,locn].min())/100.0+1
				
				AA = getFieldb(cc,pts[int(np.mod(locn,nlocs)),:],dtt)
				
				np.savetxt('fdata.dat',(AA)**2,delimiter=' ')
				time.sleep(1e-3)
				pt.stdin.write("set pm3d map; set cbrange [0:100000]; set grid; set size square; splot 'fdata.dat' u (($1-40)/2):(($2-40)/2):3 matrix with image noti\n")
					
				dt = (time.time()-t0)+0.00015
				
				if(dt>0 and dt<1.0/PRF):
					time.sleep(1.0/PRF-dt)
				#~ ax.clear()	
				tt[plsn*nlocs+locn] = (time.time()-t0)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(3)
	pt.stdin.write("exit()\n")


def pingSafely():
	npulses = MAIZEBOARDS*32
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
		for mother in range(0,MAIZEBOARDS):
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
		for mother in range(0,MAIZEBOARDS):
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


def pingMaskTest():
	d.connect()
	unmask()
	
	def uploadPingDelaysAll():
	
		chargetime = 500*np.ones(MAIZEBOARDS*32)
		#~ chargetime[128:] = 0
			
		for mother in range(0,MAIZEBOARDS):
			b.select_motherboard(mother)
			chanset = np.asarray(range(0,32))+mother*32 
			b.set_chipmem_wloc(0)
			b.write_array_pattern_16bit(chargetime[chanset])
			for mm in range(1,501):
				b.set_chipmem_wloc(mm)
				b.write_array_pattern_16bit(chargetime[chanset])
	
	def uploadPingCommandsAll():
		b.stop_execution()
		
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,0)
		a.wait(1)
		a.set_amp(0)
		a.wait(1)
		a.loadincr_chipmem(1,0)
		a.wait(1)
		a.set_phase(0)
		a.wait(1)
		a.fire(0)
		
		a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
		a.waitsec(5e-6)
		a.set_trig(0)
		a.waitsec(50e-6)
		a.halt()
		
	
	uploadPingDelaysAll()
	uploadPingCommandsAll()
	
	d.resetVars()
	
	pack_size = 1024
	timeOut = 100
	PRF = 200
	npulses = 64
	recLen = 2048*2
	
	d.setTrigDelay(100)
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataSize(1,1,1)
	d.allocateMemory()
	d.dataAcqStart(1)
	
	
	tt = np.zeros(npulses)
	for plsn in range(0,npulses):
		t0 = time.time()
		
		rcvmask = np.zeros(8*RECVBOARDS).astype(int)
		rcvmask[plsn] = 1
			
		if np.mod(plsn,64) == 63:	
			mst = 2
		else:
			mst = 1
						
		d.setAcqIdx(0,0,0)		
		d.setChannelMask(rcvmask,mst)
		b.go()
		d.ipcWait()
		
		if mst == 2:
			cc = d.getData(RECVBOARDS)
			
		dt = time.time()-t0
		
		if dt<(1.0/PRF):
			time.sleep(1.0/PRF-dt)
		tt[plsn] = (time.time()-t0)
	cmask[:]=1	
	wf = bsortData(cc,recLen,1,1,1)[0,0,0,:,:]	
	
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	for m in range(0,64):
		print m, np.round(np.mean(wf[:,m]),2)
		plt.plot(wf[:,m]+ m*270)
		
	plt.show()	
	time.sleep(1)
	d.setTimeout(5e6)
	d.disconnect()
	
	
def steerSafelyPlotWF():
		
	recLen = 2048
	
	def getwf(ipcdata):
		ev = 4
		wf = bsortData(ipcdata,recLen,1,1,1)[:,:,:,::ev,:]
		
		return wf

	pts = makePattern(1)
	print 'pattern'
	delays = calcDelays(pts)
	print 'calcdelays'
	uploadDelays2(delays)
	print 'uploaddelays'
	uploadCommands()
	print 'uploadcommands'
	nlocs = pts.shape[0]
	npulses = 1
	
	pack_size = 512
	timeOut = 1100
	PRF = 1
	d.connect()
	d.resetVars()
	d.setTrigDelay(0)
	
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataSize(1,1,1)
	d.allocateMemory()
	d.dataAcqStart(1)

	
	tt = np.zeros(npulses*nlocs)
	for ctloc in range(0,1):
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,int(ctloc))
		for plsn in range(0,npulses):
			for locn in range(0,nlocs):
				t0 = time.time()
				d.setAcqIdx(0,0,0)
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,0)
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+2))
				b.go()
				
				#~ time.sleep(1.0/(10.0*PRF))#-dt)
				dummy = d.ipcWait()
				cc = d.getData(RECVBOARDS)
				
				dt = (time.time()-t0)*1e3
				print dt
				#~ time.sleep(1.0/PRF)
				
				
				wfa = getwf(cc)
				
				for n in range(0,RECVBOARDS*8):
					plt.plot(wfa[0,0,0,0:200,n]/256.0+n)
				plt.show()	
				
				if(dt>0 and dt<1.0/PRF):
					time.sleep(1.0/PRF-dt)
				
				tt[plsn*nlocs+locn] = (time.time()-t0)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	
		
	#~ d.disconnect()
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	

def steerSafelyBmode():
	def uploadDelaysBmode(delays): #~ one with bubbles and without bubbles
	
		chargetime = 200*np.ones(MAIZEBOARDS*32)
		
		for mother in range(0,MAIZEBOARDS):
			b.select_motherboard(mother)
			chanset = np.asarray(range(0,32))+mother*32 
			b.set_chipmem_wloc(0)
			b.write_array_pattern_16bit(chargetime[chanset])					
			for ptn in range(0,delays.shape[1]):
				b.set_chipmem_wloc(64+ptn+1)
				dd = (delays[chanset,ptn]+chargetime[chanset]).astype(int)
				b.write_array_pattern_16bit(dd)
		nn=1
		for el in els:
			chargetime2 = 0*np.ones(MAIZEBOARDS*32)
			chargetime2[el]=499
			for mother in range(0,MAIZEBOARDS):
				b.select_motherboard(mother)
				chanset = np.asarray(range(0,32))+mother*32 
				b.set_chipmem_wloc(nn)
				b.write_array_pattern_16bit(chargetime2[chanset])
			nn+=1
					
		print 'nn = ',nn
		
	def uploadCommandsBmode():
		b.stop_execution()
		
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,0) 	# 0
		a.wait(1) 					# 1
		a.set_amp(0) 				# 2
		a.wait(1) 					# 3
		a.loadincr_chipmem(1,0) 	# 4
		a.wait(1) 					# 5
		a.set_phase(0) 				# 6
		a.wait(1) 					# 7
		a.fire(0) 					# 8
		a.waitsec(50e-6)			# 9
		
		a.waitsec(500e-6)			# 10
		a.loadincr_chipmem(1,1) 	# 11
		a.wait(1) 					# 12
		a.set_amp(0)				# 13
		a.wait(1) 					# 14
		a.loadincr_chipmem(1,1) 	# 15
		a.wait(1) 					# 16
		a.set_phase(0) 				# 17
		a.wait(1) 					# 18
		a.fire(0) 					# 19
		
		a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
		a.waitsec(5e-6)
		a.set_trig(0)
		a.waitsec(50e-6)
		a.halt()

			
	recLen = 2048
	
	unmask()
		
	pts = makePattern(10)
	delays = calcDelays(pts)
	uploadDelaysBmode(delays)
	uploadCommandsBmode()
	nlocs = pts.shape[0]
	npulses = 10
	
	pack_size = 512
	timeOut = 1100
	PRF = 50
	d.resetVars()
	
	d.setTrigDelay(150)
	
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataSize(10,10,64)
	d.allocateMemory()
	d.dataAcqStart(1)
	
	tt = np.zeros(npulses*nlocs*64)

	for plsn in range(0,npulses):
		for locn in range(0,nlocs):
			for aa in range(0,64):
				
				d.setAcqIdx(plsn,locn,aa)
				t0 = time.time()
				
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,0)				
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+65))
				
				b.set_imem_wloc(11)
				a.loadincr_chipmem(1,aa+1)
				b.set_imem_wloc(15)
				a.loadincr_chipmem(1,aa+1)
				b.go()
				
				d.ipcWait()
				#~ time.sleep(1.0/PRF)
				dt = time.time()-t0
				#~ if( dt < 1.0/PRF ):
				time.sleep(1.0/PRF-dt)
				

				
			#~ dt = (time.time()-t0)+0.00015
			
			#~ if(dt>0 and dt<1.0/PRF):
				#~ time.sleep(1.0/PRF-dt)
	
				tt[plsn*nlocs*64+locn*64+aa] = (time.time()-t0)
		time.sleep(1)
	d.saveData("Bmode_10-10-64_50Hz")
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(3)
	

def steerSafelyBmodeRealTime():
	
	def uploadDelaysBmode(delays): #~ one with bubbles and without bubbles
	
		chargetime = 200*np.ones(MAIZEBOARDS*32)
		
		for mother in range(0,MAIZEBOARDS):
			b.select_motherboard(mother)
			chanset = np.asarray(range(0,32))+mother*32 
			b.set_chipmem_wloc(0)
			b.write_array_pattern_16bit(chargetime[chanset])					
			for ptn in range(0,delays.shape[1]):
				b.set_chipmem_wloc(64+ptn+1)
				dd = (delays[chanset,ptn]+chargetime[chanset]).astype(int)
				b.write_array_pattern_16bit(dd)
		nn=1
		for el in els:
			chargetime2 = 0*np.ones(MAIZEBOARDS*32)
			chargetime2[el]=499
			for mother in range(0,MAIZEBOARDS):
				b.select_motherboard(mother)
				chanset = np.asarray(range(0,32))+mother*32 
				b.set_chipmem_wloc(nn)
				b.write_array_pattern_16bit(chargetime2[chanset])
			nn+=1
					
		print 'nn = ',nn
			
	def uploadCommandsBmode():
		b.stop_execution()
		
		b.set_imem_wloc(0)
		a.loadincr_chipmem(1,0) 	# 0
		a.wait(1) 					# 1
		a.set_amp(0) 				# 2
		a.wait(1) 					# 3
		a.loadincr_chipmem(1,0) 	# 4
		a.wait(1) 					# 5
		a.set_phase(0) 				# 6
		a.wait(1) 					# 7
		a.fire(0) 					# 8
		a.waitsec(50e-6)			# 9
		
		a.waitsec(500e-6)			# 10
		a.loadincr_chipmem(1,1) 	# 11
		a.wait(1) 					# 12
		a.set_amp(0)				# 13
		a.wait(1) 					# 14
		a.loadincr_chipmem(1,1) 	# 15
		a.wait(1) 					# 16
		a.set_phase(0) 				# 17
		a.wait(1) 					# 18
		a.fire(0) 					# 19
		
		a.set_trig(15) # [ 0 0 0 0 ] 2^3 + 2^2 + 2^1 + 2^0
		a.waitsec(5e-6)
		a.set_trig(0)
		a.waitsec(50e-6)
		a.halt()

	xx = np.linspace(-20,20,81)
	yy = np.linspace(-20,20,81)
	XX,YY = np.meshgrid(xx,yy)
	ZZ = np.zeros(XX.shape)#-2.0*(XX+20)/40.0-1
	xyz,nxyz = loadArray()
	
	RR = np.zeros((len(xx),len(yy),RECVBOARDS*8))
	for n in range(0,RECVBOARDS*8):
		RR[:,:,n] = ((xyz[els[n],0]-XX)**2+(xyz[els[n],1]-YY)**2+(xyz[els[n],2]-ZZ)**2)**0.5
		
	recLen = 2048
	
	unmask()
	
		
	def getwf(ipcdata):
		ev = 4
		wf = bsortData(ipcdata,recLen,1,1,1)[:,:,:,::ev,:]
		
		return wf
		
	def getFieldb(ipcdata,pt,dtt,elN,AA):
		ev = 3
		t0 = 20
		t1 = recLen-20

		f = bsortData(ipcdata,recLen,1,1,1)[0,0,0,t0:t1:ev,elN]
		T = 150
		tw = np.linspace(T+t0/20.,T+t0/20.+(t1-t0)/20,(t1-t0))[::ev]/2
		ll = len(tw)
		hwf = np.zeros(ll+1)
		f = f-np.mean(f)
		f[f>=129]=128.9
		wf = 8*f/(129.0**2-f**2)**0.5
		hwf[0:-1] = np.abs(hilbert(wf-np.mean(wf)))
		hwf[ll] = 0
		for dtt2 in range(dtt,dtt+1):
			r = (tw+dtt2-5)*1.48
			rmn,lr = r.min(),len(r)
			dr = r.max()-rmn

			
			rr = RR[:,:,elN]				
			
			rridx = ((rr-rmn)*lr/dr).astype(int)
			rridx[rridx<0] = ll
			rridx[rridx>ll] = ll
			
			AA+=hwf[rridx]
				
		return AA#**3
		
	pts = makePattern(10)
	delays = calcDelays(pts)
	uploadDelaysBmode(delays)
	uploadCommandsBmode()
	nlocs = pts.shape[0]
	npulses = 1
	
	pack_size = 512
	timeOut = 1100
	PRF = 50
	#~ d.connect()
	d.resetVars()
	
	d.setTrigDelay(150)
	
	d.setRecLen(recLen)
	d.setPacketSize(pack_size)
	d.setTimeout(timeOut)
	d.setDataSize(1,1,1)
	d.allocateMemory()
	d.dataAcqStart(1)
	
	pt = subprocess.Popen(['gnuplot','-e'],shell=True,stdin=subprocess.PIPE,)
	pt.stdin.write("set pm3d map; set size square\n")
	
	tt = np.zeros(npulses*nlocs)

	for plsn in range(0,npulses):
		for locn in range(0,nlocs):
			#~ t0 = time.time()				
			AA = np.zeros(XX.shape)
			for aa in range(0,64):
				unmask()
				d.setAcqIdx(0,0,0)
				t0 = time.time()
				
				b.stop_execution()
				b.set_imem_wloc(0)
				a.loadincr_chipmem(1,0)				
				b.set_imem_wloc(4)
				a.loadincr_chipmem(1,int(np.mod(locn,nlocs)+65))
				
				b.set_imem_wloc(11)
				a.loadincr_chipmem(1,aa+1)
				b.set_imem_wloc(15)
				a.loadincr_chipmem(1,aa+1)
				b.go()
				
				d.ipcWait()
				cc = d.getData(RECVBOARDS)
				
				#~ wfa = getwf(cc)
			
				#~ for n in range(0,RECVBOARDS*8):
					#~ plt.plot(wfa[0,0,0,::4,n]/256.0+n)
				#~ plt.show()
				dtt = 0
				
				AA = getFieldb(cc,pts[int(np.mod(locn,nlocs)),:],dtt,aa,AA)
				dt = time.time()-t0
				#~ print '\ndaq step', (time.time()-t0)
				if( dt < 1.0/PRF ):
					time.sleep((1.0/PRF-dt)/2.0)
				#~ print 'whole loop',(time.time()-t0)
				
			np.savetxt('fdata.dat',(AA)**2,delimiter=' ')
			#~ time.sleep(1e-3)
			pt.stdin.write("set pm3d map; set grid; set size square; splot 'fdata.dat' u (($1-40)/2):(($2-40)/2):3 matrix with image noti\n") #set cbrange [0:100000];
				
			dt = (time.time()-t0)+0.00015
			
			if(dt>0 and dt<1.0/PRF):
				time.sleep(1.0/PRF-dt)
			#~ ax.clear()	
			tt[plsn*nlocs+locn] = (time.time()-t0)
		time.sleep(1)
		#~ print 'time =',(time.time()-t0)*1e3,'ms'
	print '\navg time: ',np.round(np.mean(tt)*1e3,1),'+-',np.round(np.std(tt)*1e3,1)
	print 'PRF =', np.round(1.0/np.mean(tt),1)
	print 'min/max times:', np.round(np.min(tt)*1e3,1),'  -  ',np.round(np.max(tt)*1e3,1),'\n\n'
	time.sleep(3)
	pt.stdin.write("exit()\n")



