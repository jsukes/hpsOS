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
 

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <math.h> 

#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"       
#include "hps_0h.h"     

#define HW_REGS_BASE ( ALT_STM_OFST )  
#define HW_REGS_SPAN ( 0x04000000 )   
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

#define DREF(X) ( *(uint32_t *) X )
#define MAX_DATA_LEN 8192
#define INIT_PORT 3400

// case flags for switch statement in FPGA_dataAcqController
#define CASE_TRIGDELAY 0
#define CASE_RECLEN 1
#define CASE_TRANSREADY_TIMEOUT 2
#define CASE_CLOSE_PROGRAM 8
#define CASE_DATAGO 6
#define CASE_SETPACKETSIZE 11
#define CASE_MASKCHANNELS 13
#define CASE_KILLPROGRAM 17

int RUN_MAIN = 1;

#include "client_funcs.h"

int main(int argc, char *argv[]) { printf("into main!\n");
	
    uint32_t dtmp[2*MAX_DATA_LEN];
	int c2p[2]; int p2c[2];
    
    if( pipe(c2p) == -1 ){ perror("c2p pipe"); exit(1); }
    if( pipe(p2c) == -1 ){ perror("p2c pipe"); exit(1); }

    int sv[2]; socketpair(AF_UNIX,SOCK_STREAM,0,sv);
	pid_t pid;
	pid = fork();
	if( pid == 0 ){
		close(c2p[0]); close(p2c[1]);
		FPGA_dataAcqController(p2c[0],c2p[1],sv[1]);
	} else {
		close(c2p[1]); close(p2c[0]);
	}
	
	uint32_t enetmsg[4] = {0};
    
	// loads board-specific data from onboard file
    int boardData[3];
    int recLen = 2048;
    int rdcnt;
	getBoardData(argc,argv,boardData); 
	
	// create ethernet socket to communicate with server and establish connection
	struct ENETsock ENET;
	setupENETsock(&ENET,argv[1],boardData[0]);
    const int one = 1; //const int zero = 0;	

	// declare and initialize variables for the select loop
	int maxfd;
	maxfd = (ENET.sockfd > c2p[0]) ? ENET.sockfd : c2p[0];
    maxfd = (maxfd > sv[0]) ? maxfd : sv[0];
	int nready,nrecv;
	fd_set readfds;
	
	struct timeval tv;
	
	while(RUN_MAIN == 1){
	    tv.tv_sec = 0;
	    tv.tv_usec = 10000;
		FD_ZERO(&readfds);
		FD_SET(ENET.sockfd,&readfds);
        FD_SET(sv[0],&readfds);
		
		nready = select(maxfd+1, &readfds, (fd_set *) 0, (fd_set *) 0, &tv);
		
		if( nready > 0 ){
			if( FD_ISSET( ENET.sockfd, &readfds ) ){ // message from server
				nrecv = recv(ENET.sockfd,&enetmsg,4*sizeof(uint32_t),MSG_WAITALL);	
                setsockopt(ENET.sockfd,IPPROTO_TCP,TCP_QUICKACK,&one,sizeof(int));	

                if( nrecv == 0 ){
					RUN_MAIN = 0;
					enetmsg[0] = 3;
				}
				
                if( enetmsg[0]<CASE_KILLPROGRAM ){
                    if( enetmsg[0] == CASE_RECLEN ){ recLen = enetmsg[1]; }
                    write(p2c[1],&enetmsg,4*sizeof(uint32_t));
				} else {
					kill(pid,SIGKILL);
					RUN_MAIN = 0;
				}

			}
 
			if( FD_ISSET(sv[0],&readfds) ){ // message from child
                rdcnt = read(sv[0],&dtmp,2*recLen*sizeof(uint32_t));
                write(ENET.sockfd,dtmp,rdcnt);
                setsockopt(ENET.sockfd,IPPROTO_TCP,TCP_QUICKACK,&one,sizeof(int));	
			}
		}
	} 
	//munmap(data, 2*MAX_DATA_LEN*sizeof(uint32_t));
	close(c2p[0]); close(p2c[1]); close(sv[0]);
	close(ENET.sockfd);
	sleep(1);
    return( 0 );
}

 

 









 





