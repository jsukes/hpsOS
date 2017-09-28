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
#define DREF(X) ( *(volatile uint32_t *) X )
#define MAX_DATA_LEN 8192
#define INIT_PORT 3400

// case flags for switch statement in FPGA_dataAcqController
#define CASE_TRIGDELAY 0
#define CASE_RECLEN 1
#define CASE_TRANSREADY_TIMEOUT 2
#define CASE_CLOSE_PROGRAM 8
#define CASE_DATAGO 6
#define CASE_IMGMODE 13 // not implemented yet
#define CASE_SETPACKETSIZE 11

int runMain = 1;

#include "client_funcs.h"

int main(int argc, char *argv[]) { printf("into main!\n");
	
    uint32_t dtmp[2*MAX_DATA_LEN];
	int c2p[2]; pipe(c2p);
	int p2c[2];	pipe(p2c);	
    int sv[2]; socketpair(AF_UNIX,SOCK_STREAM,0,sv);
	
	pid_t pid;
	pid = fork();
	if( pid == 0 ){
		close(c2p[0]); close(p2c[1]);
		FPGA_dataAcqController(p2c[0],c2p[1],sv[1]);
	} else {
		close(c2p[1]); close(p2c[0]);
	}
	
    uint32_t pipemsg[4] = {0};
	int enetmsg[4] = {0};
    
	// loads board-specific data from onboard file
    int boardData[3];
    int recLen = 2048;
    int packetsize = 512;
    int rdcnt;
    int imgmode = 0;
	getBoardData(argc,argv,boardData); 
	
	// create ethernet socket to communicate with server and establish connection
	struct ENETsock ENET;
	setupENETsock(&ENET,argv[1],boardData[0]);

	 // declare and initialize variables for the select loop
	int maxfd;
	maxfd = (ENET.sockfd > c2p[0]) ? ENET.sockfd : c2p[0];
    maxfd = (maxfd > sv[0]) ? maxfd : sv[0];
	int nready,nrecv,cnt;
	fd_set readfds;
	
	struct timeval tv;
	
	while(runMain == 1){
	    tv.tv_sec = 0;
	    tv.tv_usec = 10000;
		FD_ZERO(&readfds);
		FD_SET(ENET.sockfd,&readfds);
		//FD_SET(c2p[0],&readfds);
        FD_SET(sv[0],&readfds);
		
		nready = select(maxfd+1, &readfds, (fd_set *) 0, (fd_set *) 0, &tv);
		
		if( nready > 0 ){
			if( FD_ISSET( ENET.sockfd, &readfds ) ){ // message from server
				nrecv = recv(ENET.sockfd,&enetmsg,4*sizeof(int),MSG_WAITALL);
				if( nrecv == 0 ){
					runMain = 0;
					enetmsg[0] = 3;
				}
				//printf("enetmsg %d, %d, %d, %d, %d\n",enetmsg[0],enetmsg[1],enetmsg[2],enetmsg[3],nrecv);
				if( enetmsg[0]<17 ){
                    if( enetmsg[0] == 11){
                        packetsize = enetmsg[1];
                    }else if(enetmsg[0] == 1){
                        recLen = enetmsg[1];
                        write(p2c[1],&enetmsg,4*sizeof(int));
                    }else if(enetmsg[0] == 13){
                        imgmode = enetmsg[1];
					    write(p2c[1],&enetmsg,4*sizeof(int));
                    }else{
					    write(p2c[1],&enetmsg,4*sizeof(int));
                    }
				} else {
					kill(pid,SIGKILL);
					runMain = 0;
				}
			}
 
			if( FD_ISSET(sv[0],&readfds) ){ // message from child
                if(imgmode == 0){
                    rdcnt = read(sv[0],&dtmp,2*recLen*sizeof(uint32_t));
                    write(ENET.sockfd,dtmp,rdcnt);
                } else {//send(sv[0],&maxfd,sizeof(int),0);
                    rdcnt = read(sv[0],&cnt,sizeof(uint32_t));
                    write(ENET.sockfd,&cnt,rdcnt);
                    if(cnt == 8){
                        rdcnt = read(sv[0],&dtmp,2*recLen*sizeof(uint32_t));
                        write(ENET.sockfd,dtmp,rdcnt);
                        cnt = 0;
                    }
                }
			}
		}
	} 
	//munmap(data, 2*MAX_DATA_LEN*sizeof(uint32_t));
	close(c2p[0]); close(p2c[1]); close(sv[0]);
	close(ENET.sockfd);
	sleep(1);
    return( 0 );
}

 

 









 






