
//~ #include <sys/types.h> # unused
//~ #include <sys/socket.h> # unused
#include <sys/un.h>
//~ #include <sys/mman.h> # unused
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
//~ #include <stdint.h> # unused
//~ #include <string.h> # unused
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
//~ #include <zlib.h>
//~ #include <errno.h> # unused
#include <sys/time.h> // unused
//~ #include <time.h> // unused
//~ #include <math.h> # unused


#define INIT_PORT 3400
#define MAX_FPGAS 64
#define MAX_PORTS 17
#define MAX_SOCKETS ( MAX_FPGAS * MAX_PORTS )
#define PACKET_WIDTH 1024
#define DATA_FIFO "data_pipe"
#define IPCSOCK "./lithium_ipc"
#define MAX_RECLEN 8192

#define CASE_SET_TRIGGER_DELAY 0
#define CASE_SET_RECORD_LENGTH 1
#define CASE_SET_CSERVER_DATA_ARRAY_SIZE 4
#define CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM 5
#define CASE_TOGGLE_DATA_ACQUISITION 6
#define CASE_DECLARE_CSERVER_DATA_ARRAY_INDEX 7
#define CASE_CLOSE_PROGRAM 8
#define CASE_RESET_GLOBAL_VARIABLES 9
#define CASE_SAVE_CSERVER_DATA 10
#define CASE_SEND_CSERVER_DATA_IPC 12
#define CASE_GET_BOARD_COUNT 14
#define CASE_GET_BOARD_NUMS 15
#define CASE_QUERY_DATA 16
#define CASE_SHUTDOWN_SERVER 17

const int ONE = 1;  /* need to have a variable that can be pointed to that always equals '1' for some of the socket options */
const int ZERO = 0;  /* need to have a variable that can be pointed to that always equals '1' for some of the socket options */
/* global variables to keep track of runtime things, all global variables are prepended with 'g_'*/
unsigned long g_trigDelay;
unsigned long g_recLen;
unsigned long g_idx1len, g_idx2len, g_idx3len;
unsigned long g_id1, g_id2, g_id3;
unsigned long g_numBoards;

struct ENETsock{ /* structure to store variables associated with ethernet connection */
    int sockfd[MAX_PORTS];                 /* file descriptor (fd) of the server socket listening for new connections */
    int clifd[MAX_SOCKETS];       /* array to store the fd's of the connected client socs */
    int p_idx[MAX_SOCKETS];       /* used to keep track of how much data has been recv'd from each soc and update array indices when saving data */
    int board[MAX_SOCKETS];       /* array to store the board number of each connected soc */
    int portNum[MAX_SOCKETS];
    int boardIdx[MAX_FPGAS];
    const char *serverIP;       /* IP address of the server */
    struct sockaddr_in server[MAX_PORTS];  /* socket variable */
};


struct IPCsock{ /* structure to store variables for interprocess communication (ipc) socket (for communicating with python) */
    int ipcfd;                  /* fd of the listening ipc socket in the cServer */
    int clifd;                  /* fd of the socket after connecting with the user space process (python server) */
    struct sockaddr_un local;   /* ipc socket variable */
    int len;                    /* variable to store length of the string in IPCSOCK */
};


struct FIFOmsg{ /* structure to store variables for communications between cServer and (python) via FIFO instead of ipc socket */
    uint32_t msg[4];
    char buff[100]; 
};


void setupIPCsock(struct IPCsock *IPC){ /* function to open listening ipc socket for connections from other processes (python server) */
    IPC->ipcfd = socket(AF_UNIX, SOCK_STREAM, 0);
    IPC->local.sun_family = AF_UNIX;
    strcpy(IPC->local.sun_path,IPCSOCK);
    unlink(IPC->local.sun_path);
    IPC->len = strlen(IPC->local.sun_path)+sizeof(IPC->local.sun_family);
    if( bind( IPC->ipcfd, (struct sockaddr *)& IPC->local, IPC->len) < 0){
        perror("ERROR binding IPCsock");
        exit(1);
    }
    if( listen(IPC->ipcfd,5)){
        perror("ERROR listening IPCsock");
        exit(1);
    }
}


void sortENETconnections(struct ENETsock *ENET){ /* sorts ENET connections logically by board number */
	int n,m,k,l;
	int clifd,bn,pn,bnmax;
	bnmax=0;
    g_numBoards = 0;
    for(n=0;ENET->clifd[n+1]!=0;n++){
		clifd = ENET->clifd[n];
		bn = ENET->board[n];
		pn = ENET->portNum[n];
		k=n;
		for(m=n+1;ENET->clifd[m]!=0;m++){
			if( ENET->board[m] < ENET->board[n] && ENET->board[m] > 0 ) k = m;
		}
		
		ENET->board[n] = ENET->board[k];
		ENET->portNum[n] = ENET->portNum[k];
		ENET->clifd[n] = ENET->clifd[k];
		
		ENET->board[k] = bn;
		ENET->portNum[k] = pn;
		ENET->clifd[k] = clifd;
		bnmax = ( bn > bnmax ) ? bn : bnmax;
        if(ENET->portNum[n] == 0) g_numBoards++;
	}
	
    l=0;
    for(n=0;ENET->clifd[n]!=0;n++){
        ENET->boardIdx[ENET->board[n]] = l;
        if(ENET->board[n+1] > ENET->board[n]) l++;
    }
}


void acceptENETconnection(struct ENETsock *ENET,int pn){ /* function to accept incoming ethernet connections from the socs */
    /*  This function accepts new ethernet connections from the SoC's after the listening socket gets a connection request.
        - After receiving the connection request, the function loops through the array of file descriptors for the connected devices (ENET->clifd) and
          accepts the new connection into the first empty spot.
        - TCP_NODELAY is then set on the socket to allow the server to send small packets to the SoC's quickly.
        - The first thing the clients do after connecting to the server is send a message containing the number identifying the board.
        - The socket recv's this message into 'enetmsg' and stores the value in the array of board numbers (ENET->board)
    */
    
    int clilen, n;
    int enetmsg[4] = {0};
    struct sockaddr_in client;
    clilen = sizeof(client);
    
    for(n=0;n<MAX_FPGAS;n++){
        if(ENET->clifd[n] == 0){
            ENET->clifd[n] = accept(ENET->sockfd[pn], (struct sockaddr *)&client, &clilen);
            setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(int));
            recv(ENET->clifd[n], &enetmsg, 4*sizeof(int), MSG_WAITALL);
            ENET->board[n] = enetmsg[0];    /* stores the recv'd board number in the ENETsock struct */
            ENET->portNum[n] = enetmsg[1];
            printf("connected to board %d, port %d\n",enetmsg[0],enetmsg[1]);
            break;
        }
    }
    
    sortENETconnections(ENET);
}


void acceptIPCconnection(struct IPCsock *IPC){ /* function to accept incoming ipc connections (from python server) */
    int t;
    struct sockaddr_un remote;
    t = sizeof(remote);
    IPC->clifd = accept(IPC->ipcfd, (struct sockaddr *)&remote, &t);
    printf("IPC socket accepted %d\n",IPC->clifd);
}


void setupENETserver(struct ENETsock *ENET){ /* function to set up ethernet socket to listen for incoming connections */
    int n,pn;
    for(n=0;n<MAX_PORTS;n++){
		pn = INIT_PORT+n;
		ENET->sockfd[n] = socket(AF_INET, SOCK_STREAM, 0);
		ENET->server[n].sin_family = AF_INET;
		ENET->server[n].sin_addr.s_addr = INADDR_ANY;
		ENET->server[n].sin_port = htons(pn);
		setsockopt(ENET->sockfd[n],SOL_SOCKET, SO_REUSEADDR, &ONE, sizeof(int));

		if( bind(ENET->sockfd[n], (struct sockaddr *)&ENET->server[n], sizeof(ENET->server[n])) < 0 ){
			perror("ERROR binding socket");
			exit(1);
		}
		listen(ENET->sockfd[n],MAX_FPGAS);
	}
}


void resetGlobalVars(){ /* function to reset all global variables, global variables are prepended with 'g_' */
    g_recLen = 2048; g_trigDelay = 0;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
}


void removeClients(struct ENETsock *ENET){ /* function to remove closed ethernet connections from ENETsock struct */
    
    int n,m;
    for(n=0;ENET->clifd[n]!=0;n++){
        if(ENET->clifd[n] == -1){
            printf("board %d,%d disconnected\n",ENET->board[n],ENET->portNum[n]);
            for(m=n+1;ENET->clifd[m-1]!=0;m++){
                ENET->clifd[m-1] = ENET->clifd[m];
                ENET->board[m-1] = ENET->board[m];
                ENET->portNum[m-1] = ENET->portNum[m];
                ENET->p_idx[m-1] = ENET->p_idx[m];
            }
            n--;
        }
    }
    g_numBoards = 0;
    for(n=0;n<MAX_SOCKETS;n++){
        if(ENET->portNum[n] == 0){
            g_numBoards++;
        }
    }
}


void sendENETmsg(struct ENETsock *ENET, uint32_t *msg){ /* function to send messages to socs over ethernet */
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs
       connected to the server
        
        - msg is a 4 element array
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3]
        - msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */

    int n;
    for(n=0;ENET->clifd[n]!=0;n++){
        if( ENET->portNum[n] == 0 ){
            send(ENET->clifd[n],msg,4*sizeof(uint32_t),0);
            setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
        }
    }
}


void resetFPGAdataAcqParams(struct ENETsock *ENET){ /* function to reset data acquisition variables on the SoCs */
    /* this function makes a message variable, populates it with the default data acquisition variables, and sends it to the SoCs*/
    uint32_t fmsg[4] ={0};

    fmsg[0] = CASE_TOGGLE_DATA_ACQUISITION; fmsg[1] = 0;
    sendENETmsg(ENET,fmsg);
    
    fmsg[0] = CASE_SET_TRIGGER_DELAY; fmsg[1] = g_trigDelay;
    sendENETmsg(ENET,fmsg);
    
    fmsg[0] = CASE_SET_RECORD_LENGTH; fmsg[1] = g_recLen;
    sendENETmsg(ENET,fmsg);
   
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
    struct ENETsock ENET = {.sockfd={0},.clifd={0},.board={0},.p_idx={0},.portNum={0},.boardIdx={-1}};          /* declares and initializes ethernet socket variables */
    setupENETserver(&ENET);                                             /* calls function to listen for incoming enet connections from the SoCs */

    struct IPCsock IPC = {.clifd=0};                                    /* declares and initializes ipc socket variables */
    setupIPCsock(&IPC);                                                 /* calls function to listen for incoming ipc connections from (python) */

    struct FIFOmsg fmsg;                                                /* creates messaging variable to carry ipc messages */
    
    int connectedBoards[MAX_FPGAS];    
    int n,m,k,l;   
    int maxfd = -1;                                                     /* variable to store the highest value of a connected fd  */

    for(n=0;n<MAX_PORTS;n++)
		maxfd = ( maxfd > ENET.sockfd[n] ) ? maxfd : ENET.sockfd[n];    /* sets maxfd to be equal to the higher of the two fds */
    maxfd = ( maxfd > IPC.ipcfd ) ? maxfd : IPC.ipcfd;                  /* sets maxfd to be equal to the higher of the two fds */

    resetGlobalVars();                                                  /* sets all the global variables to their defualt values */
    uint32_t *data;                                                     /* array to store acquired data */
    
    data = (uint32_t *)malloc(2*MAX_FPGAS*g_recLen*sizeof(uint32_t));   /* initial memory allocation for 'data' */
    unsigned long data_idx;                                             /* index of where to write incoming data in the 'data' array */

    unsigned long maxipc;                                               /* number of SoCs connected over enet, number of ipc socks */
    maxipc = 0;                                                         /* initializes the number of connected SoCs and ipc to 0 */
    
    g_numBoards = 0;
    int dataAcqGo = 0;                                                  /* flag to put SoC in state to acquire data or not */
    int nready,nrecv;                                                   /* number of fds with incoming data ready, number of bytes recv'd */
    fd_set readfds;                                                     /* array of fds to poll for incoming/readable data */
    struct timeval tv;                                                  /* timer variable used for select loop timeout */
    int runner = 1;                                                     /* flag to run the server, closes program if set to 0 */
    k=0;
    while(runner == 1){
        
        /* 'select' function timeout values. need to be reset before each call to 'select'. timeout = tv.tv_sec + tv.tv_usec */
        tv.tv_sec = 1;                                                  /* units: seconds */
        tv.tv_usec = 0;                                                 /* units: microseconds */

        /* constructs the set of fds the 'select' function should poll for incoming/readable data */
        FD_ZERO(&readfds);                                              /* emptys the set */
        for(n=0;n<MAX_PORTS;n++){
			FD_SET(ENET.sockfd[n],&readfds);                            /* adds the ethernet socket which listens for new incoming connections */
		}
        
        FD_SET(IPC.ipcfd,&readfds);                                     /* adds the ipc socket which listens for new ipc connections */
        
        for(n=0;ENET.clifd[n]!=0;n++){                                  /* loops through all *connected* ethernet sockets to add them to the set */
            FD_SET(ENET.clifd[n],&readfds);
        }
        for(n=0;n<maxipc;n++){                                          /* loops through all *connected* ipc sockets and adds them to the set */
            FD_SET(IPC.clifd,&readfds);     
        }
        
        /* polls the fds in readfds for readable data, returns number of fds with data (nready), returns '0' if it times out. */
        nready = select(maxfd+1, &readfds, NULL, NULL, &tv);            

        if( nready > 0 ){

            /* IPC socket message handler  */
            
            if(FD_ISSET(IPC.clifd, &readfds)){				
                /* The IPC socket is used to handle all messages between the cServer and the user interface (python) 
                    all messages from (python) contain two fields 'msg' and 'buff' ( given in struct FIFOmsg )
                    - 'msg' is a 4-element array of type uint32_t
                    - 'buff' is a character array with 100 elements (primarily/only used to declare the file name when saving data)
                    - msg[0] is a number representing the name of the command given by the 'CASE_...' definitions above
                    - msg[1]-msg[3] are 32-bit numerical arguments/values passed to the given command (eg for msg[0] = CASE_SET_TRIGGER_DELAY,
                      msg[1] = trigDelay)
                */	
                fmsg.msg[0]=0; fmsg.msg[1]=0; fmsg.msg[2]=0; fmsg.msg[3]=0; /* resets all the 'msg' variables before reading new ones */
                
                nrecv = recv(IPC.clifd,&fmsg,sizeof(struct FIFOmsg),MSG_WAITALL);
				if ( nrecv < 0 ){ 
                    printf("IPC read error, shutting down\n");
                    break;  /* error condition 'breaks' out of the while loop and shuts down server */

                } else if(nrecv == 0){
					if(IPC.clifd == maxfd){
						maxfd--;
					}
					close(IPC.clifd);
					maxipc = 0;
					
				} else {
                    switch(fmsg.msg[0]){ /* msg[0] contains the command code for the message */
                        /* switch statement notes:
                            - If msg[0] doesn't equal one of the defined CASE's, the server shuts down ('default' case)
                            - All messages which change data acquisition variables write the recv'd values into the corresponding global variable in
                              the cServer
                            - If recv'd values in msg[1]-msg[3] are illegal (eg trigDelay < 0), they are instead set to their default values
                                - If recLen is less than the enet packetsize, enet packetsize gets overwritten as recLen
                            - After all messages that change the size of data to be acquired you need to call CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM to
                              resize the storage variable in the cServer. No on-the-fly memory reallocation allowed
                            - All cases need to 'break' out of the 'switch' statement when they're done, else the switch statement will execute the
                              'default' case at the end and shut down the server
                        */

                        case(CASE_SET_TRIGGER_DELAY):{ 
                            /* this sets the time delay between when the FPGA gets the trigger, and when the acquisition begins
                                notes on variables in fmsg:
                                - msg[1] contains the trigDelay value to be transmitted to the SoCs
                                - msg[2] and buff are not used
                                - msg[3] contains no data, but can be set to allow independent trigger timings to be sent to each *board*. (see
                                  sendENETmsg function)
                             */
                            if(fmsg.msg[1] >= 0){
                                g_trigDelay = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg);
                                printf("trigDelay set to: %lu us\n\n",g_trigDelay);
                            } else {
                                g_trigDelay = 0;
                                fmsg.msg[1] = 0;
                                sendENETmsg(&ENET,fmsg.msg);
                                printf("invalid trigDelay value, defaulting to 0 us\n\n");
                            }
                            break;
                        }

                        case(CASE_SET_RECORD_LENGTH):{ 
                            /* This sets the length of the array into which data will be stored, NOT the time duration of the acquisition
                                notes on variables in fmsg:
                                - msg[1] contains the record length of the data to be transmitted to the SoCs
                                - msg[2] and buff are not used
                                - msg[3] contains no data. record lengths cannot be independently set on the boards. any attempts to do so are
                                  overwritten
                             */
                            if(fmsg.msg[1] > 0 && fmsg.msg[1] < MAX_RECLEN){
                                g_recLen = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg);
                                printf("recLen set to: %lu\n\n",g_recLen);
                            } else {
                                printf("invalid recLen, reseting global variables\n"); 
                                resetGlobalVars();
                                k = 0;
                                resetFPGAdataAcqParams(&ENET);
                                data = (uint32_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                                printf("global variables reset to defaults\n");
                            }
                            break;
                        }

                        case(CASE_SET_CSERVER_DATA_ARRAY_SIZE):{ 
                            /* 'data' is stored in the cServer in a 5D array with size = [g_idx1len, g_idx2len, g_idx3len, 2*recLen, Nboards]
                                this function/case sets the size of the first 3 dimensions of that array so the cServer can allocate the required
                                memory for data acquisition 
                                notes on variables in fmsg:
                                - msg[1], msg[2], and msg[3] contain the length of the of the first, second, and third dimensions of the array,
                                  respectively
                                - buff is not used
                                - values of 0 are allowed to be set in msg[1], msg[2], and msg[3] when calling this function, but the allocation
                                  requires that they be overwritten with '1', doesn't effect anything on the user side to do this.
                            */
                            g_idx1len = ( 1 > fmsg.msg[1] ) ? 1 : fmsg.msg[1];
                            g_idx2len = ( 1 > fmsg.msg[2] ) ? 1 : fmsg.msg[2];
                            g_idx3len = ( 1 > fmsg.msg[3] ) ? 1 : fmsg.msg[3];
                            printf("Data Array Dimensions set to: [%lu, %lu, %lu, recLen, nElements]\n\n",g_idx1len,g_idx2len,g_idx3len); 
                            break;
                        }

                        case(CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM):{ 
                            /* this allocates/frees memory for data acquisition on the cServer. this is separate from the other functions because it
                               should only be called once, after the size of the data array has been set, to prevent tons of calls to 'realloc'.
                                notes on variables in fmsg:
                                - msg[1] tells the cServer to allocate the memory. 
                                    - the user doesn't have to input a value, it should be set equal to '1' in the calling function from the UI.
                                    - if not though, the data array is deleted and a new one is allocated to the default size.
                                - msg[2], msg[3], and buff are not used.
                            */
                            if(fmsg.msg[1] == 1){
                                
                                data = (uint32_t *)realloc(data,g_numBoards*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                                printf("data realloc'd to size [%lu, %lu, %lu, %lu, %lu]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,g_numBoards);
                            } else {
                                free(data);
                                uint32_t *data;
                                data = (uint32_t *)malloc(MAX_FPGAS*4*g_recLen*sizeof(uint32_t));
                            }
                            break;
                        }

                        case(CASE_TOGGLE_DATA_ACQUISITION):{ 
                            /* tells the SoC whether to be in a data acquisition mode or not 
                                notes about variables in fmsg:
                                - msg[1] is the toggle state, '1' puts the SoC into a state to acquire data. any other value puts it into a state to
                                  NOT acquire data
                                - msg[2,3] and buff are not used
                            */
							g_numBoards = 0;
                            for(n=0;ENET.clifd[n]!=0;n++){
                                if(ENET.portNum[n] == 0) g_numBoards++;
                                printf("connected to board %d, port %d\n",ENET.board[n],ENET.portNum[n]);
                            }
                            if(fmsg.msg[1] == 1){
                                dataAcqGo = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg);
                                printf("data acquisition started\n\n");
                            } else {
                                dataAcqGo = 0;
                                fmsg.msg[1] = 0;
                                sendENETmsg(&ENET,fmsg.msg);
                                printf("data acquisition stopped\n\n");
                            }
                            break;
                        }

                        case(CASE_DECLARE_CSERVER_DATA_ARRAY_INDEX):{ 
                            /* This function is used to tell the cServer where the next round of incoming data should be placed in the data array.
                               Index must be declared prior to every data acquisition event/pulse.
                                notes on variables in fmsg:
                                - msg[1], msg[2], and msg[3] are the index locations where the acquired data will be stored in the data array
                                - buff is unused
                            */ 
                            if(fmsg.msg[1]>=0 && fmsg.msg[2]>=0 && fmsg.msg[3]>=0){
                                g_id1 = fmsg.msg[1];
                                g_id2 = fmsg.msg[2];
                                g_id3 = fmsg.msg[3];
                            } else {
                                g_id1=0;g_id2=0;g_id3=0;
                                printf("idxs must be >=0, defualting to [0,0,0]. recvd vals = [%d, %d, %d]\n\n", fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                            }
                            break;
                        }

                        case(CASE_CLOSE_PROGRAM):{ 
                            /* This function shuts down the SoCs but leaves the cServer running.
                                notes on variables in fmsg:
                                - msg[1], msg[2], msg[3]*, and buff are unused.
                                
                                * as of now (10/13/2017) msg[3] is overwritten as 0 to broadcast this command to all connected boards. The cServer can
                                  handle disconnects and reconnects in the middle of operation pretty efficiently, but the front end programming in
                                  the UI to do it would be kind of messy, and the reconnection process isn't automated on a per board level yet, so i
                                  just disabled the option. may be incorporated in the future though.
                            */
                            fmsg.msg[3] = 0;
                            sendENETmsg(&ENET,fmsg.msg);
                            printf("shutting down FPGAs\n");
                            break;
                        }

                        case(CASE_RESET_GLOBAL_VARIABLES):{
                            /* This function resets all variables to their defaults. 
                                all variables in fmsg are unused.    
                            */
                            resetGlobalVars();
                            k = 0;
                            resetFPGAdataAcqParams(&ENET);
                            data = (uint32_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                            printf("global variables reset to defaults\n");
                            printf("data reset to size [%lu, %lu, %lu, %lu, %d]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,MAX_FPGAS);
                            break;
                        }

                        case(CASE_SAVE_CSERVER_DATA):{
                            /* This function is used to save the acquired data in the 'data' array into a binary file
                                notes on variables in fmsg:
                                - msg[1], msg[2], and msg[3] are unused
                                - buff contains the name of the file to save the data in. (the string in 'buff' is limited to 100 characters)
                            */ 
                            FILE *datafile = fopen(fmsg.buff,"wb"); 
                            fwrite(data,sizeof(uint32_t),MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*2*g_recLen,datafile);
                            fclose(datafile);
                            printf("data saved to file %s\n\n",fmsg.buff);
                            break;                            
                        }

                        case(CASE_SEND_CSERVER_DATA_IPC):{
                            /* This function sends the entire 'data' array directly to the python UI 
                                notes on variables in fmgs:
                                - msg[1], msg[2], msg[3], and buff are unused    
                            */
							if(send(IPC.clifd,data,sizeof(uint32_t)*MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*2*g_recLen,0) == -1){
                                perror("IPC send failed\n");
                                exit(1);
                            }
                            break;
                        }

						case(CASE_GET_BOARD_COUNT):{
							/* This function returns the number of socs connected to the cServer via ethernet to the python UI 
								- msg[1], msg[2], msg[3]*, and buff are unused.*/
							g_numBoards = 0;
                            for(n=0;ENET.clifd[n]!=0;n++){
                                if(ENET.portNum[n] == 0){
                                    connectedBoards[g_numBoards] = ENET.board[n];
                                    g_numBoards++;
                                }
                            }
                            if(send(IPC.clifd,&g_numBoards,sizeof(int),MSG_CONFIRM) == -1){
                                perror("IPC send failed\n");
                                exit(1);
                            }
                            break;
						}
						
						case(CASE_GET_BOARD_NUMS):{
							/* This function returns the board numbers of the connected socs to the python UI 
								- msg[1], msg[2], msg[3]*, and buff are unused.*/	
							
                            if(send(IPC.clifd,connectedBoards,g_numBoards*sizeof(int),MSG_CONFIRM) == -1){
								perror("IPC send failed\n");
								exit(1);
							}
                            break;
						}
						
						case(CASE_QUERY_DATA):{
							sendENETmsg(&ENET,fmsg.msg);
							//printf("waiting for data\n\n");                
                            break;
						}
						
                        case(CASE_SHUTDOWN_SERVER):{
                            /* This shuts down the SoCs and cServer 
                                note on the variables in fmgs:
                                - no variables are used. if this command is issued it locally sets all the msg variables to what they need to be to
                                  shutdown the sever.
                            */
                            fmsg.msg[0]=8; fmsg.msg[3] = 0;
                            sendENETmsg(&ENET,fmsg.msg);
                            runner = 0;
                            printf("shutting server down\n\n");
                            break;
                        }

                        default:{
                            /* The default action if an undefined CASE value is detected is to and shut down the server
                                - all variables in fmsg are set locally to shutdown and exit the server
                             */
                             
                            printf("invalid message, shutting down server,%d, %d, %d, %d\n",fmsg.msg[0],fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                            fmsg.msg[0]=8; fmsg.msg[3] = 0;
                            sendENETmsg(&ENET,fmsg.msg);
                            break;
                        }
                    }
                }   
            }
        
            /* SoC message/data handler  */
            l=0;
            for(n=0;ENET.clifd[n]!=0;n++){ /* loops through all connected SoCs */
                if(ENET.portNum[n]!=0){
                    if(FD_ISSET(ENET.clifd[n], &readfds)){ 
                        
                        /* sets the array index where the incoming data will be stored */
                        data_idx = ( ENET.boardIdx[ENET.board[n]] )*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
                        data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
                        data_idx += g_id2*g_idx3len*2*g_recLen;
                        data_idx += g_id3*2*g_recLen;
                        data_idx += (ENET.portNum[n]-1)*2*PACKET_WIDTH;
                        
                        if( (ENET.portNum[n]-1)%MAX_PORTS == (g_recLen-1)/PACKET_WIDTH && g_recLen%PACKET_WIDTH > 0 ){
                            nrecv = recv(ENET.clifd[n],&data[data_idx+ENET.p_idx[n]],2*(g_recLen%PACKET_WIDTH)*sizeof(uint32_t),0);
                        } else {
                            nrecv = recv(ENET.clifd[n],&data[data_idx+ENET.p_idx[n]],2*PACKET_WIDTH*sizeof(uint32_t),0);
                        }
                        
                        setsockopt(ENET.clifd[n],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 

                        if (nrecv == 0){
                            close(ENET.clifd[n]);
                            ENET.clifd[n]=-1;
                            l=1;
                        } else {
                            /* keeps track of how much data each 'read' brought in to update the array index of where to store the next set of
                               incoming data */
                            ENET.p_idx[n] += nrecv/sizeof(uint32_t); 
                            
                            /* after each channel has read in all the data its supposed to for each pulse, p_idx is reset to 0. 'k' is a counter to keep track of how many boards have completed data transfer */
                            if(ENET.p_idx[n] == 2*PACKET_WIDTH){
                                k++;
                                ENET.p_idx[n] = 0;
                            } else if ( ((ENET.portNum[n]-1)%MAX_PORTS == (g_recLen-1)/PACKET_WIDTH) && (g_recLen%PACKET_WIDTH > 0 )){
                                if(ENET.p_idx[n] == 2*(g_recLen % PACKET_WIDTH)){
                                    k++;
                                    ENET.p_idx[n] = 0;
                                }
                            }
                            
                        }
                        if( k == g_numBoards*((g_recLen-1)/PACKET_WIDTH+1) ){ 
                            /* if all data from all SoCs has been collected and transfered to the cServer, let the python UI know so it can move on
                               with the program */
                            if(send(IPC.clifd,&n,sizeof(int),0) == -1){
                                perror("IPC send failed\n");
                                exit(1);
                            }
                            k = 0;
                        }
                    }
                }
            }
            if(l>0) removeClients(&ENET);
          
    
			for(l=0;l<MAX_PORTS;l++){
				/* Handles/accepts new ethernet connections waiting to be established with the cServer */
				if( FD_ISSET(ENET.sockfd[l], &readfds) ){
					acceptENETconnection(&ENET,l);
					for(n=0;ENET.clifd[n]!=0;n++){
                        maxfd = (maxfd > ENET.clifd[n]) ? maxfd : ENET.clifd[n];
					}
				}
			}
            
            /* Handles/accepts new ipc socket connection waiting to be established with the cServer */
            if( FD_ISSET(IPC.ipcfd, &readfds) ){
                acceptIPCconnection(&IPC);
                maxipc = 1;
                maxfd = (maxfd > IPC.clifd) ? maxfd : IPC.clifd;
            }
    
        }
    }
    
    /* closes everything and shuts down the cServer. */
    printf("out select loop\n");
    free(data);
    close(IPC.ipcfd);
    for(n=0;n<MAX_PORTS;n++)
		close(ENET.sockfd[n]);
    for(n=0;ENET.clifd[n]!=0;n++)
        close(ENET.clifd[n]);
    printf("successfully exited!\n");
    exit(0);
}
