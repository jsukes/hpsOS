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
#include "hps_0l.h"


#define HW_REGS_BASE ( ALT_STM_OFST )  
#define HW_REGS_SPAN ( 0x04000000 )   
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

//setting for the HPS2FPGA AXI Bridge
#define ALT_AXI_FPGASLVS_OFST (0xC0000000) // axi_master
#define HW_FPGA_AXI_SPAN (0x40000000) // Bridge span 1GB
#define HW_FPGA_AXI_MASK ( HW_FPGA_AXI_SPAN - 1 )


#define DREF(X) ( *(uint32_t *) X )
#define DREFP(X) ( (uint32_t *) X )
#define MAX_DATA_LEN 8192
#define INIT_PORT 3400

// case flags for switch statement in FPGA_dataAcqController
#define CASE_TRIGDELAY 0
#define CASE_RECLEN 1
#define CASE_CLOSE_PROGRAM 8
#define CASE_DATAGO 6
#define CASE_QUERY_DATA 16
#define CASE_KILLPROGRAM 17

#define N_PORTS 17
#define PACKET_WIDTH 1024

int RUN_MAIN = 1;
uint32_t g_recLen;
const int ONE = 1;
const int ZERO = 0;	

// load user defined functions 
#include "client_funcs.h"


int main(int argc, char *argv[]) { printf("into main!\n");
	g_recLen = 2048;
	
	struct FPGAvars FPGA;
	FPGA_init(&FPGA);
	
	uint32_t enetmsg[4] = {0}; // messaging variable to handle messages from cServer
    
	// loads board-specific data from onboard file
    int boardData[3];
	getBoardData(argc,argv,boardData); 
	
	// create ethernet socket to communicate with server and establish connection
	struct ENETsock ENET = {.serverIP=argv[1],.boardNum=boardData[0]};
	setupENETsock(&ENET,0);
    setupENETsock(&ENET,1);
    setupENETsock(&ENET,2);
    

	// declare and initialize variables for the select loop
	int n;
	
	int nready,nrecv;
	fd_set readfds;
	
	struct timeval tv;
	
	while(RUN_MAIN == 1){
	    tv.tv_sec = 0;
	    tv.tv_usec = 10000;
		FD_ZERO(&readfds);
        for(n=0;n<N_PORTS;n++){
            if(ENET.sockfd[n]!=0){
		        FD_SET(ENET.sockfd[n],&readfds);
            }
		}

		nready = select(ENET.maxfd+1, &readfds, (fd_set *) 0, (fd_set *) 0, &tv);
		
		if( nready > 0 ){
			if( FD_ISSET( ENET.sockfd[0], &readfds ) ){ // incoming message from cServer
				nrecv = recv(ENET.sockfd[0], &enetmsg,4*sizeof(uint32_t),0);	
                setsockopt(ENET.sockfd[0],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));	
                if( nrecv == 0 ){
					RUN_MAIN = 0;
					enetmsg[0] = 8;
				}
				
                if( enetmsg[0]<CASE_KILLPROGRAM ){ // if message doesn't shutdown the SoC, forward message to child process
                    FPGA_dataAcqController(enetmsg,&FPGA,&ENET);
				} else { 
					RUN_MAIN = 0;
				}

			}
            for(n=1;n<N_PORTS;n++){
                if(ENET.sockfd[n]!=0 && FD_ISSET(ENET.sockfd[n],&readfds)){
                    nrecv = recv(ENET.sockfd[n], &enetmsg,4*sizeof(uint32_t),0);
                    if(nrecv == 0){
                        close(ENET.sockfd[n]);
                        if(ENET.sockfd[n] == ENET.maxfd){
                            ENET.maxfd = ENET.sockfd[0];
                            for(ENET.sockfd[n] = 1; ENET.sockfd[n]<N_PORTS; ENET.sockfd[n]++){
                                if( ENET.sockfd[n] != n )
                                    ENET.maxfd = (ENET.maxfd > ENET.sockfd[ENET.sockfd[n]]) ? ENET.maxfd : ENET.sockfd[ENET.sockfd[n]];
                            }
                        }
                        ENET.sockfd[n] = 0;
                    } else {
                        printf("illegal recv on port %d\n, shutting down client\n",n);
                        RUN_MAIN = 0;
                    }
                }
            }
		}
	}
	for(n=0;n<N_PORTS;n++)
		close(ENET.sockfd[n]); 
	FPGAclose(&FPGA);
	sleep(1);
    return( 0 );
}

 

 









 






