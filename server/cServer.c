
#include <sys/socket.h> // unused
#include <sys/epoll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // unused
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h> // unused
#include <sys/time.h> // unused
extern int errno;

//TESTING GIT PUSHES

#define INIT_PORT 3400
#define MAX_IPC 5
#define MAX_FPGAS 64
#define MAX_DATA_PORTS 64
#define MAX_PORTS ( MAX_DATA_PORTS + 1 )
#define MAX_SOCKETS ( MAX_FPGAS * MAX_PORTS + MAX_IPC )
#define IPCSOCK "./lithium_ipc"
#define MAX_RECLEN 8192
#define MIN_PACKETSIZE 128
#define LISTEN_PORT -1
#define IPC_COMMPORT 0
#define ENET_COMMPORT INIT_PORT

#define CASE_SET_TRIGGER_DELAY 0
#define CASE_SET_RECORD_LENGTH 1
#define CASE_SET_PACKETSIZE 2
#define CASE_UPDATE_FDSET 3
#define CASE_SET_CSERVER_DATA_ARRAY_SIZE 4
#define CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM 5
#define CASE_TOGGLE_DATA_ACQUISITION 6
#define CASE_DECLARE_CSERVER_DATA_ARRAY_INDEX 7
#define CASE_CLOSE_PROGRAM 8
#define CASE_RESET_GLOBAL_VARIABLES 9
#define CASE_SAVE_CSERVER_DATA 10
#define CASE_SEND_CSERVER_DATA_IPC 12
#define CASE_SET_NUM_CONCURRENT_SENDERS 13
#define CASE_GET_BOARD_COUNT 14
#define CASE_GET_BOARD_NUMS 15
#define CASE_QUERY_DATA 16
#define CASE_SHUTDOWN_SERVER 17


const int ONE = 1;  /* need to have a variable that can be pointed to that always equals '1' for some of the socket options */
const int ZERO = 0;  /* need to have a variable that can be pointed to that always equals '0' for some of the socket options */
/* global variables to keep track of runtime things, all global variables are prepended with 'g_'*/
unsigned long g_trigDelay;
unsigned long g_recLen, g_packetsize;
unsigned long g_idx1len, g_idx2len, g_idx3len;
unsigned long g_id1, g_id2, g_id3;
uint32_t g_numBoards, g_portMax;
uint32_t g_connectedBoards[MAX_FPGAS];
unsigned long g_concurrentSenders;

int epfd;
struct epoll_event ev;
struct epoll_event events[MAX_SOCKETS];


struct POLLsock{
    int clifd;
    int p_idx;
    int boardNum;
    int portNum;
    struct POLLsock *next;
    struct POLLsock *prev;
};


void setnonblocking(int sock){
    int opts;
    if((opts=fcntl(sock,F_GETFL))<0) errexit("GETFL %d failed",sock);
    
    opts = opts | O_NONBLOCK;
    if(fcntl(sock,F_SETFL,opts)<0) errexit("SETFL %d failed",sock);
}


struct FIFOmsg{ /* structure to store variables for communications between cServer and (python) via FIFO instead of ipc socket */
    uint32_t msg[4];
    char buff[100]; 
};


void addPollSock(struct POLLsock **psock){
    struct POLLsock* ps;
    ps = (struct POLLsock *)malloc(sizeof(struct POLLsock));
    ps->next = *psock;
    ps->prev = NULL;
    (*psock)->prev = ps;
    *psock = ps;
}


void setupIPCserver(struct POLLsock **psock, struct SERVERsock **ssock){ /* function to open listening ipc socket for connections from other processes (python server) */
    int len;
    struct sockaddr_un local;

    addPollSock(psock);
    struct POLLsock *ps;
    ps = (*psock);
    ps->portNum = LISTEN_PORT;
    
    ps->clifd = socket(AF_UNIX, SOCK_STREAM, 0);
    setnonblocking(ps->clifd);
    ev.data.fd = ps->clifd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path,IPCSOCK);
    unlink(local.sun_path);
    len = strlen(local.sun_path)+sizeof(local.sun_family);
    if( bind( ps->clifd, (struct sockaddr *)& local, len) < 0){
        perror("ERROR binding IPCsock");
        exit(1);
    }
    if( listen(ps->clifd,MAX_IPC)){
        perror("ERROR listening IPCsock");
        exit(1);
    }

}


void acceptIPCconnection(struct POLLsock **psock, int listenfd){ /* function to accept incoming ipc connections (from python server) */
    int t;
    struct sockaddr_un remote;
    addPollSock(psock);
    struct POLLsock *ps;
    ps = (*psock);

    t = sizeof(remote);
    ps->clifd = accept(listenfd, (struct sockaddr *)&remote, &t);
    setnonblocking(ps->clifd);
    ev.data.fd = ps->clifd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);
    printf("IPC socket accepted %d\n",ps->clifd);
}


void setupENETserver(struct ENETserver *ENET){ /* function to set up ethernet socket to listen for incoming connections */
    int n,pn;
    for(n=0;n<MAX_PORTS;n++){
		pn = INIT_PORT+n;
		ENET->listenfd[n] = socket(AF_INET, SOCK_STREAM, 0);
        setnonblocking(ENET->listenfd[n]);
        ev.data.fd = ENET->listenfd[n];
        ev.events = EPOLLIN;
        epoll_ctl(epfd,EPOLL_CTL_ADD,ENET->listenfd[n],&ev);

		ENET->server[n].sin_family = AF_INET;
		ENET->server[n].sin_addr.s_addr = INADDR_ANY;
		ENET->server[n].sin_port = htons(pn);
		setsockopt(ENET->listenfd[n],SOL_SOCKET, SO_REUSEADDR, &ONE, sizeof(int));

		if( bind(ENET->listenfd[n], (struct sockaddr *)&ENET->server[n], sizeof(ENET->server[n])) < 0 ){
			perror("ERROR binding socket");
			exit(1);
		}
		listen(ENET->listenfd[n],MAX_FPGAS);
	}
}


void acceptENETconnection(int listenfd, struct ENETsock *ENET, struct COMMsock *CSOCK, int pn){ /* function to accept incoming ethernet connections from the socs */
    /*  This function accepts new ethernet connections from the SoC's after the listening socket gets a connection request.
        - After receiving the connection request, the function loops through the array of file descriptors for the connected devices (ENET->clifd) and
          accepts the new connection into the first empty spot.
        - TCP_NODELAY is then set on the socket to allow the server to send small packets to the SoC's quickly.
        - The first thing the clients do after connecting to the server is send a message containing the number identifying the board.
        - The socket recv's this message into 'enetmsg' and stores the value in the array of board numbers (ENET->board)
    */
    
    int clilen, clifd;
    struct sockaddr_in client;
    clilen = sizeof(client);
    
    clifd = accept(listenfd, (struct sockaddr *)&client, &clilen);
    setnonblocking(clifd);
    ENET[clifd].clifd = clifd;
    ev.data.ptr = &ENET[clifd];
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, clifd, &ev);
    setsockopt(clifd, IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(int));
    ENET[clifd].clifd = clifd;
    if(pn==0){
        if(CSOCK->clifd[0]==0){
            CSOCK->clifd[0] = clifd;
        } else {
            while( CSOCK->clifd[pn]!=0 )
                pn++;
            CSOCK->clifd[pn] = clifd;
        }
    }
}


int checkNewENET(int listenfd, struct ENETserver *ENETserv){
    int n;
    for(n=0;n<MAX_FPGAS;n++){
        if( listenfd == ENETserv->listenfd[n] ) return(n);
    }
    return(-1);
}


void sendENETmsg(struct COMMsock *ENET, uint32_t *msg){ /* function to send messages to socs over ethernet */
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs
       connected to the server
        
        - msg is a 4 element array
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3]
        - msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */
    int n;
    for(n=0;ENET->clifd[n]!=0;n++){
        send(ENET->clifd[n],msg,4*sizeof(uint32_t),0);
        setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
    }
}


void sendENETmsgSingle(struct COMMsock *ENET, uint32_t *msg, int boardNum){ /* function to send messages to socs over ethernet */
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs
       connected to the server
        
        - msg is a 4 element array
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3]
        - msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */

    int n,m;
    for(n=0;ENET->clifd[n]!=0;n++){
        if( ENET->boardNum[n] == boardNum ){
            m = send(ENET->clifd[n],msg,4*sizeof(uint32_t),0);
            setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
            break;
        }
    }
}


void resetGlobalVars(){ /* function to reset all global variables, global variables are prepended with 'g_' */
    g_recLen = 2048; g_trigDelay = 0; g_packetsize = 512;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
    g_concurrentSenders = 0;
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

    fmsg[0] = CASE_SET_PACKETSIZE; fmsg[1] = g_packetsize;
    sendENETmsg(ENET,fmsg);
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
    resetGlobalVars();                                                  /* sets all the global variables to their defualt values */
    g_numBoards = 0;
    g_portMax = 0;
    
    epfd = epoll_create(MAX_SOCKETS);

    struct ENETserver ENETserv = {.listenfd={0}};
    struct COMMsock ENETcomm = {.clifd={0},.boardNum={0}};

    struct ENETsock *ENET;
    ENET = (struct ENETsock *)malloc(MAX_SOCKETS*sizeof(struct ENETsock));
    struct ENETsock *enet;

    struct IPCserver IPC_SERV = {.listenfd=0};                                    /* declares and initializes ipc socket variables */
    struct IPCsock IPC = {.clifd=0};
    setupIPCserver(&IPC_SERV);                                                 /* calls function to listen for incoming ipc connections from (python) */

    struct FIFOmsg fmsg;                                                /* creates messaging variable to carry ipc messages */
    struct FIFOmsg qmsg = {.msg=0};                                     /* special message variable to query data from socs */
    qmsg.msg[0]=CASE_QUERY_DATA;

    int n,m,k,l,concSendCnt;
    int enet_listenfd;

    uint8_t *data;                                                      /* array to store acquired data */
    unsigned long data_idx;                                             /* index of where to write incoming data in the 'data' array */
    data = (uint8_t *)malloc(8*MAX_FPGAS*g_recLen*sizeof(uint8_t));     /* initial memory allocation for 'data' */

    int dataAcqGo;                                                      /* flag to put SoC in state to acquire data or not */
    int nfds,nrecv,recvCount;                                         /* number of fds ready, number of bytes recv'd per read, sum of nrecv'd */
    int runner;                                                         /* flag to run the server, closes program if set to 0 */
    int timeout_ms;

    k=0;
    maxipc = 0;                                                         /* initializes the number of connected SoCs and ipc to 0 */
    dataAcqGo = 1;
    runner = 1;
    timeout_ms = 1000;
    while(runner == 1){
        
        /* polls the fds in readfds for readable data, returns number of fds with data (nready), returns '0' if it times out. */
        nfds = epoll_wait(epfd, events, MAX_SOCKETS, timeout_ms);          
		
        if( nfds > 0 ){

            for(n = 0; n < nfds; n++){

                enet = (struct ENETsock *)events[n].data.ptr;

                if(events[n].data.fd == IPCserv.listenfd){ /* new IPC connection */
                    acceptIPCconnection(&IPCserv,&IPC);
                
                } else if ( (enet_listenfd = checkNewENET(event[n].data.fd,&ENETserv)) != -1){ /* new ENET connection */
                    acceptENETconnection(ENETserv.listenfd[enet_listenfd], ENET, &COMMsock, enet_listenfd);
                
                } else if ( events[n].events & EPOLLIN ){
                    if( events[n].data.fd == IPC.clifd ){ /* message incoming from python */

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
                            close(IPC.clifd);

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
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        printf("trigDelay set to: %lu us\n\n",g_trigDelay);
                                    } else {
                                        g_trigDelay = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(&ENETcomm,fmsg.msg);
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
                                    if(fmsg.msg[1] >= MIN_PACKETSIZE && fmsg.msg[1] <= MAX_RECLEN){
                                        g_recLen = fmsg.msg[1];
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        printf("recLen set to: %lu\n\n",g_recLen);
                                        if(g_packetsize>g_recLen){
                                            printf("previous packetsize (%lu) too large, setting equal to recLen (%lu)\n",g_packetsize,g_recLen);
                                            g_packetsize = g_recLen;
                                            fmsg.msg[0] = CASE_SET_PACKETSIZE; fmsg.msg[1] = g_packetsize;
                                            sendENETmsg(&ENETcomm,fmsg.msg);
                                        }
                                    } else {
                                        printf("invalid recLen, reseting global variables\n"); 
                                        resetGlobalVars();
                                        k = 0;
                                        resetFPGAdataAcqParams(&ENETcomm);
                                        data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                        printf("global variables reset to defaults\n");
                                    }
                                    break;
                                }

                                case(CASE_SET_PACKETSIZE):{
                                    if(fmsg.msg[1] <= g_recLen && fmsg.msg[1] >= MIN_PACKETSIZE){
                                        g_packetsize = fmsg.msg[1];
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        printf("packetsize set to: %lu\n\n",g_packetsize);
                                    } else {
                                        printf("invalid packetsize (%lu), setting equal to recLen (%lu)\n",(unsigned long)fmsg.msg[1],g_recLen); 
                                        g_packetsize = g_recLen;
                                        fmsg.msg[1] = g_recLen;
                                        sendENETmsg(&ENETcomm,fmsg.msg);
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
                                        data = (uint8_t *)realloc(data,g_numBoards*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                        printf("data realloc'd to size [%lu, %lu, %lu, %lu, %u], %lu\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,g_numBoards,g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t)*g_numBoards);
                                    } else {
                                        free(data);
                                        uint8_t *data;
                                        data = (uint8_t *)malloc(MAX_FPGAS*2*g_recLen*sizeof(uint64_t));
                                        printf("allocator error\n");
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
                                    if(fmsg.msg[1] == 1){
                                        dataAcqGo = fmsg.msg[1];
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        printf("data acquisition started\n\n");
                                    } else {
                                        dataAcqGo = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        printf("data acquisition stopped\n\n");
                                    }
                                    updateFDSet = 1;
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
                                    sendENETmsg(&ENETcomm,fmsg.msg);
                                    printf("shutting down FPGAs\n");
                                    break;
                                }

                                case(CASE_RESET_GLOBAL_VARIABLES):{
                                    /* This function resets all variables to their defaults. 
                                        all variables in fmsg are unused.    
                                    */
                                    resetGlobalVars();
                                    k = 0;
                                    resetFPGAdataAcqParams(&ENETcomm);
                                    data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                    printf("global variables reset to defaults\n");
                                    printf("data reset to size [%lu, %lu, %lu, %lu, %d]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,MAX_FPGAS);
                                    updateFDSet = 1;
                                    break;
                                }

                                case(CASE_SAVE_CSERVER_DATA):{
                                    /* This function is used to save the acquired data in the 'data' array into a binary file
                                        notes on variables in fmsg:
                                        - msg[1], msg[2], and msg[3] are unused
                                        - buff contains the name of the file to save the data in. (the string in 'buff' is limited to 100 characters)
                                    */ 
                                    FILE *datafile = fopen(fmsg.buff,"wb"); 
                                    fwrite(data,sizeof(uint8_t),g_numBoards*g_idx1len*g_idx2len*g_idx3len*8*g_recLen,datafile);
                                    fclose(datafile);
                                    printf("data saved to file %s\n\n",fmsg.buff);
                                    break;                            
                                }

                                case(CASE_SEND_CSERVER_DATA_IPC):{
                                    /* This function sends the entire 'data' array directly to the python UI 
                                        notes on variables in fmgs:
                                        - msg[1], msg[2], msg[3], and buff are unused    
                                    */
                                    if(send(IPC.clifd,data,sizeof(uint8_t)*g_numBoards*g_idx1len*g_idx2len*g_idx3len*8*g_recLen,0) == -1){
                                        perror("IPC send failed\n");
                                        exit(1);
                                    }
                                    break;
                                }
                                
                                case(CASE_SET_NUM_CONCURRENT_SENDERS):{
                                    /* sets how many socs can transfer their data to the cServer at once
                                        - default value of 0 means they all send their data at the same time
                                    */
                                    if( fmsg.msg[1] <= g_numBoards ){
                                        g_concurrentSenders = ( fmsg.msg[1] == g_numBoards ) ? 0 : fmsg.msg[1];
                                        printf("concurrentSenders set to %lu\n",g_concurrentSenders);
                                    } else {
                                        printf("invalid number of concurrent senders, setting to 0\n"); 
                                        g_concurrentSenders = 0;
                                    }
                                    break;
                                }

                                case(CASE_GET_BOARD_COUNT):{
                                    /* This function returns the number of socs connected to the cServer via ethernet to the python UI 
                                        - msg[1], msg[2], msg[3]*, and buff are unused.*/
                                    if(send(IPC.clifd,&g_numBoards,sizeof(uint32_t),MSG_CONFIRM) == -1){
                                        perror("IPC send failed\n");
                                        exit(1);
                                    }
                                    break;
                                }
                                
                                case(CASE_GET_BOARD_NUMS):{
                                    /* This function returns the board numbers of the connected socs to the python UI 
                                        - msg[1], msg[2], msg[3]*, and buff are unused.*/	
                                    if(send(IPC.clifd,g_connectedBoards,g_numBoards*sizeof(uint32_t),MSG_CONFIRM) == -1){
                                        perror("IPC send failed\n");
                                        exit(1);
                                    }
                                    break;
                                }
                                
                                case(CASE_QUERY_DATA):{
                                    if( g_concurrentSenders == 0 ){
                                        sendENETmsg(&ENETcomm,fmsg.msg);
                                        recvCount = 0;
                                        //printf("waiting for data\n\n");
                                    } else {
                                        for(n=0;n<g_concurrentSenders;n++){
                                            sendENETmsgSingle(&ENETcomm,qmsg.msg,n);
                                        }
                                        recvCount = 0;
                                        concSendCnt = g_concurrentSenders;
                                    }
                                    break;
                                }
                                
                                case(CASE_SHUTDOWN_SERVER):{
                                    /* This shuts down the SoCs and cServer 
                                        note on the variables in fmgs:
                                        - no variables are used. if this command is issued it locally sets all the msg variables to what they need to be to
                                          shutdown the sever.
                                    */
                                    fmsg.msg[0]=8;
                                    sendENETmsg(&ENETcomm,fmsg.msg);
                                    runner = 0;
                                    printf("shutting server down\n\n");
                                    break;
                                }

                                default:{
                                    /* The default action if an undefined CASE value is detected is to and shut down the server
                                        - all variables in fmsg are set locally to shutdown and exit the server
                                     */
                                     
                                    printf("invalid message, shutting down server,%d, %d, %d, %d\n",fmsg.msg[0],fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                                    fmsg.msg[0]=8; 
                                    sendENETmsg(&ENETcomm,fmsg.msg);
                                    break;
                                }
                            }
                        }   
                    
                    } else {
                        enet = (struct ENETsock *)events[n].data.ptr;

                        /* sets the array index where the incoming data will be stored */
                        data_idx = ( enet.boardIdx )*g_idx1len*g_idx2len*g_idx3len*8*g_recLen;
                        data_idx += g_id1*g_idx2len*g_idx3len*8*g_recLen;
                        data_idx += g_id2*g_idx3len*8*g_recLen;
                        data_idx += g_id3*8*g_recLen;
                        data_idx += (enet.portNum-1)*8*g_packetsize;
                       
                        /* recv data */
                        if( ( enet.portNum == g_portMax ) && ( g_recLen%g_packetsize > 0 ) ){
                            nrecv = recv(enet.clifd,&data[data_idx+enet.p_idx],(g_recLen%g_packetsize)*8*sizeof(uint8_t),0);
                        } else {
                            nrecv = recv(enet.clifd,&data[data_idx+enet.p_idx],g_packetsize*8*sizeof(uint8_t),0);
                        }
						
                        if (nrecv > 0){
                            setsockopt(enet.clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 
                            recvCount += nrecv;
                            enet.p_idx += nrecv; 
                        } else {
                            if (nrecv == -1) perror("data recv error: ");
                            close(enet.clifd);
                        }
                        
                        if( g_concurrentSenders != 0 ){
                            if( ( (enet.p_idx%(g_packetsize*8*sizeof(uint8_t))) == 0 ) && (enet.p_idx>0) && ( concSendCnt < g_numBoards ) ){
                                sendENETmsgSingle(&ENETcomm,qmsg.msg,concSendCnt);
                                concSendCnt++;
                            }
                        }
                        
                        /* if all data has been collected by cServer, let the python UI know so it can move on with the program */
                        if( recvCount == g_recLen*8*sizeof(uint8_t)*g_numBoards ){
							printf("send\n");
                            if(send(IPC.clifd,&n,sizeof(int),0) == -1){
                                perror("IPC send failed, recvCount notification: ");
                                exit(1);
                            }
                            /* reset the sockets partial index variables */
                            for(k=0;k<MAX_SOCKETS;k++){
                                ENET.p_idx[k] = 0;
                            }
                            recvCount = 0;
                        }
                    }
                }
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
