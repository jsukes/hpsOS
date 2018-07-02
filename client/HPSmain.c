// Is black box, comments are for losers. 
// Abandon all hope ye who enter.

/* File Description

	0) HPSmain initializes the HPS and puts it in a state to listen to requests from the server and launch the other processes and programs required. In order, the operations followed by this program are:

	1) load the board-specific data from a file stored on the HPS. As of now, this information is only the board number, which is just a variable used by the server to identify the board since it's IP address is not fixed at launch.

	2) create a signal handler for user defined signals. signals are basically interrupts that allow 'emergency' communication between running processes. the things you can do with signals are very very limited so, for now, all they do is execute the 'BSTOP' command and force the program to exit

	3) initialize communications with server over ethernet. commands over ethernet in this module are used to launch, close, or query the status of the other running modules to report back to the server

	4) setup ipc socket for communications with yet-to-be-launched processes. the other modules will have direct ethernet lines to the server, but querying all of them individually directly from the server is probably not an efficient use of the ethernet comms, especially for a fully connected system.

	5) setup and run a loop to monitor for activity on all open sockets. 
*/
 

//~ #include <sys/types.h> // unused
//~ #include <sys/socket.h> // unsued
#include <sys/mman.h>
//~ #include <netinet/in.h> // unused
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
//~ #include <stdint.h> // unused
//~ #include <string.h> // unused
#include <unistd.h> 
#include <fcntl.h>
#include <signal.h>
//~ #include <errno.h> // unused
#include <sys/time.h> 
//~ #include <time.h> // unused
//~ #include <math.h> // unused
#define soc_cv_av
#include "hwlib.h"
#include "soc_cv_av/socal/socal.h"
#include "soc_cv_av/socal/hps.h"
#include "soc_cv_av/socal/alt_gpio.h"       
#include "hps_0TxRx.h"


#define HW_REGS_BASE ( ALT_STM_OFST )  
#define HW_REGS_SPAN ( 0x04000000 )   
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

//setting for the HPS2FPGA AXI Bridge
#define ALT_AXI_FPGASLVS_OFST (0xC0000000) // axi_master
#define HW_FPGA_AXI_SPAN (0x40000000) // Bridge span 1GB
#define HW_FPGA_AXI_MASK ( HW_FPGA_AXI_SPAN - 1 )
#define ADC_CLK (20)
#define ARDUINO_CLK (20)

#define DREF8(X) ( *(uint8_t *) X )
#define DREF16(X) ( *(uint16_t *) X )
#define DREF(X) ( *(uint32_t *) X )
#define DREFP(X) ( (uint8_t *) X )
#define DREFP16(X) ( (uint16_t *) X )
#define DREFP32(X) ( (uint32_t *) X )

#define INIT_PORT 3400

// case flags for switch statement in FPGA_dataAcqController
#define CASE_TRIGDELAY 0
#define CASE_RECLEN 1
#define CASE_SET_INTERLEAVE_DEPTH_AND_TIMER 2
#define CASE_CLOSE_PROGRAM 8
#define CASE_DATAGO 6
#define CASE_QUERY_BOARD_INFO 12
#define CASE_QUERY_DATA 16
#define CASE_ARDUINO_TRIG_NUM 20
#define CASE_ARDUINO_TRIG_VALS 21
#define CASE_ARDUINO_TRIG_WAITS 22
#define CASE_ARDUINO_TRIG_COMMS 23
#define CASE_ARDUINO_TRIG 24
#define CASE_KILLPROGRAM 170

#define MSG_QUERY_TIMEOUT 16

#define MAX_RECLEN 8191
#define MIN_PACKETSIZE 128
#define COMM_PORT 0

int RUN_MAIN = 1;
const int ONE = 1;
const int ZERO = 0;	

uint32_t g_recLen;
uint32_t g_packetsize;

uint32_t enetmsg[4] = {0}; // messaging variable to handle messages from cServer
uint32_t emsg[4] = {0}; // messaging variable to send to messages to cServer
uint32_t g_boardData[4] = {0};

uint32_t g_boardNum;

int g_dataAcqGo=0;
int g_numPorts=0;
int g_maxfd;

uint32_t g_moduloBoardNum;
uint32_t g_moduloTimer;
uint32_t g_packetWait;

const char *g_serverIP;
int g_numTrigs=0;

// load user defined functions 
#include "client_funcs.h"


int main(int argc, char *argv[]) { printf("into main!\n");
	g_recLen = 2048;
    g_packetsize = 2048;
	g_numPorts = (g_recLen-1)/g_packetsize+1;
	g_moduloBoardNum = 1;
	g_moduloTimer = 0;
	g_packetWait = 0;
	g_serverIP=argv[1];
	
	struct FPGAvars FPGA;
	FPGA_init(&FPGA);
	
	// create ethernet socket to communicate with server and establish connection
	struct ENETsock *ENET = NULL;
	struct ENETsock *enet;
	
	// declare and initialize variables for the select loop
	int n;
	int nready,nrecv;
	fd_set masterfds;
    fd_set readfds;
	FD_ZERO(&masterfds);
	
	addEnetSock(&ENET, COMM_PORT);
	connectEnetSock(&ENET, COMM_PORT, &masterfds);

	struct timeval tv;
	while(RUN_MAIN == 1){
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		
		FD_ZERO(&readfds);
		readfds = masterfds;
        
		nready = select(g_maxfd+1, &readfds, NULL, NULL, &tv);
		enet = ENET->commsock;
		
		if( nready > 0 ){
            n = 0;
			while( enet!= NULL && enet->portNum <= g_numPorts ){
				if( FD_ISSET( enet->sockfd, &readfds ) ){ // incoming message from cServer
				    n++;
                    if( enet->is_commsock ){
						nrecv = recv(enet->sockfd,&enetmsg,4*sizeof(uint32_t),0);
						//~ printf("outer enetmsg %u, %u, %u, %u\n",enetmsg[0],enetmsg[1],enetmsg[2],enetmsg[3]);	
						setsockopt(enet->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));	
						if( nrecv == 0 ){
							RUN_MAIN = 0;
						}
						
						if( enetmsg[0]<CASE_KILLPROGRAM && enetmsg[0] != CASE_CLOSE_PROGRAM ){ // if message doesn't shutdown the SoC, forward message to child process
							FPGA_dataAcqController(&FPGA, &ENET, &masterfds);
						} else { 
							RUN_MAIN = 0;
						}

					} else { 
						nrecv = recv(enet->sockfd,&enetmsg,4*sizeof(uint32_t),0);
	                    if(nrecv == 0){
	                        disconnectEnetSock(&ENET, enet->portNum, &masterfds);
	                    } else if (nrecv == -1){
	                        perror("recv being dumb\n");
	                    } else {
	                        printf("illegal recv (n = %d) on port %d, msg = [%lu, %lu, %lu, %lu]\n, shutting down client\n",nrecv,enet->portNum,(unsigned long)enetmsg[0],(unsigned long)enetmsg[1],(unsigned long)enetmsg[2],(unsigned long)enetmsg[3]);
	                        RUN_MAIN = 0;
	                    }
					}
				}
                //~ if( n == nready )
                    //~ break;
				enet = enet->prev; 
			}
		}
	}
    
    while( ENET != NULL ){
        enet = ENET;
        ENET = enet->next;
        free(enet);
    }
    FPGAclose(&FPGA);
	sleep(1);
    return( 0 );
}

 

 









 






