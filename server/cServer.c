
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
#include <sys/ipc.h>
#include <sys/shm.h>
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
#define IPC_COMMPORT 0
#define ENET_COMMPORT INIT_PORT
#define IPC_SHARED_MEM_KEY 1234

#define CASE_SET_TRIGGER_DELAY 0
#define CASE_SET_RECORD_LENGTH 1
#define CASE_SET_INTERLEAVE_DEPTH_AND_TIMER 2
#define CASE_SET_PACKETSIZE_NPORTS 3 // might delete this all together
#define CASE_SET_CSERVER_DATA_ARRAY_SIZE 4
#define CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM 5
#define CASE_TOGGLE_DATA_ACQUISITION 6
#define CASE_DECLARE_CSERVER_DATA_ARRAY_INDEX 7
#define CASE_CLOSE_PROGRAM 8
#define CASE_RESET_GLOBAL_VARIABLES 9
#define CASE_SAVE_CSERVER_DATA 10
#define CASE_SEND_CSERVER_DATA_IPC 11
#define CASE_QUERY_BOARD_INFO 12
#define CASE_GET_BOARD_INFO_IPC 13
#define CASE_SET_QUERY_DATA_TIMEOUT 15
#define CASE_QUERY_DATA 16
#define CASE_SHUTDOWN_SERVER 17


const int ONE = 1;  /* need to have a variable that can be pointed to that always equals '1' for some of the socket options */
const int ZERO = 0;  /* need to have a variable that can be pointed to that always equals '0' for some of the socket options */
/* global variables to keep track of runtime things, all global variables are prepended with 'g_'*/
unsigned long g_trigDelay;
unsigned long g_recLen, g_packetsize;
unsigned long g_idx1len, g_idx2len, g_idx3len;
unsigned long g_id1, g_id2, g_id3;
unsigned long g_maxDataIdx;

uint32_t g_connectedBoards[MAX_FPGAS+1];
uint32_t g_numBoards, g_portMax, g_numPorts;
uint32_t g_queryTimeout;

int g_ipcCommFd;
int g_enetCommFd[MAX_FPGAS+1] = {0};
int g_enetBoardIdx[MAX_FPGAS+1] = {0};

key_t g_shmkey = IPC_SHARED_MEM_KEY;

int epfd;
struct epoll_event ev;
struct epoll_event events[MAX_SOCKETS];


struct FIFOmsg{ /* structure to store variables for communications between cServer and (python) via FIFO instead of ipc socket */
    uint32_t msg[4];
    char buff[100]; 
};


struct POLLsock{
    int clifd;
    int is_enet;
    int is_listener;
    int boardNum;
    int ipAddr;
    int portNum;
    int p_idx;
    int dataLen;
    uint8_t *data_addr;
    struct POLLsock *next;
    struct POLLsock *prev;
};


void setnonblocking(int sock){
    int opts;
    if((opts=fcntl(sock,F_GETFL))<0) perror("GETFL nonblocking failed");
    
    opts = opts | O_NONBLOCK;
    if(fcntl(sock,F_SETFL,opts)<0) perror("SETFL nonblocking failed");
}


void addPollSock(struct POLLsock **psock){
    struct POLLsock* ps;
    ps = (struct POLLsock *)malloc(sizeof(struct POLLsock));
    ps->next = *psock;
    ps->prev = NULL;
    if(*psock != NULL)
        (*psock)->prev = ps;
    *psock = ps;
}


void deletePollSock(struct POLLsock **psock, int clifd){ 
    struct POLLsock *ps, *prev, *next;
    ps = (*psock);
    while(ps != NULL){
        next = ps->next;
        prev = ps->prev;
        if(ps->clifd == clifd){
            if(next != NULL){
                next->prev = prev;
            }
            if(prev != NULL){
                prev->next = next;
            }
            if(prev == NULL){
                (*psock) = next;
            }
            free(ps);
            break;
        }
        ps = ps->next;
    }
    epoll_ctl(epfd, EPOLL_CTL_DEL, clifd, &ev);
    close(clifd);
}


void setupIPCserver(struct POLLsock **psock){ /* function to open listening ipc socket for connections from other processes (python server) */
    struct POLLsock *ps;
    struct sockaddr_un local;
    
    addPollSock(psock);
    ps = (*psock);
    ps->portNum = -1;
    ps->is_enet = 0;
    ps->is_listener = 1;
    ps->clifd = socket(AF_UNIX, SOCK_STREAM, 0);
    setnonblocking(ps->clifd);

    ev.data.ptr = ps;
    ev.events = EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);
    
    memset(&local,0,sizeof(struct sockaddr_un));
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path,IPCSOCK);
    unlink(local.sun_path);
    
    if( bind( ps->clifd, (struct sockaddr *)&local, sizeof(struct sockaddr_un)) < 0){
        perror("ERROR binding IPCsock");
        exit(1);
    }
    if( listen(ps->clifd,MAX_IPC)){
        perror("ERROR listening IPCsock");
        exit(1);
    }

}


void acceptIPCconnection(struct POLLsock **psock, struct POLLsock *tmp){ /* function to accept incoming ipc connections (from python server) */
    struct POLLsock *ps;
    struct sockaddr_un remote;
    socklen_t clilen;
    
    clilen = sizeof(remote);
    memset(&remote,0,sizeof(struct sockaddr_un));

    addPollSock(psock);
    ps = (*psock);
    ps->portNum = IPC_COMMPORT;
    ps->is_enet = 0;
    ps->is_listener = 0;

    ps->clifd = accept(tmp->clifd, (struct sockaddr *)&remote, &clilen);
    setnonblocking(ps->clifd);
    ev.data.ptr = ps;
    ev.events = EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);
    printf("IPC socket accepted %d\n",ps->clifd);
    g_ipcCommFd = ps->clifd;
}


void updatePsockField_boardNum(struct POLLsock **psock, int ipAddr){
    int boardNum;
    struct POLLsock *ps;

    ps = (*psock);
    while( ps!=NULL ){
        if( ( ps->is_enet ) && ( !ps->is_listener ) && ( ps->portNum == ENET_COMMPORT ) && ( ps->ipAddr == ipAddr ) ){
            boardNum = ps->boardNum;
            break;
        }
        ps = ps->next;
    }
    ps = *psock;
    while( ps!=NULL ){
        if( ( ps->is_enet ) && ( !ps->is_listener ) && ( ps->portNum != ENET_COMMPORT ) && ( ps->ipAddr == ipAddr ) ){
            ps->boardNum = boardNum;
            break;
        }
        ps = ps->next;
    }
}


void setupENETserver(struct POLLsock **psock){ /* function to set up ethernet socket to listen for incoming connections */
    struct POLLsock *ps;
    struct sockaddr_in server[MAX_PORTS];

    int n;
    for(n=0;n<MAX_PORTS;n++){
        addPollSock(psock);
        ps = (*psock);
        ps->is_enet = 1;
        ps->is_listener = 1;
        ps->ipAddr = 0;
        ps->portNum = INIT_PORT + n;
		ps->clifd = socket(AF_INET, SOCK_STREAM, 0);
        setnonblocking(ps->clifd);
        ev.data.ptr = ps;
        ev.events = EPOLLIN;
        epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);

        memset(&server[n],0,sizeof(struct sockaddr_in));
		server[n].sin_family = AF_INET;
		server[n].sin_addr.s_addr = INADDR_ANY;
		server[n].sin_port = htons(ps->portNum);
		setsockopt(ps->clifd,SOL_SOCKET, SO_REUSEADDR, &ONE, sizeof(int));

		if( bind(ps->clifd, (struct sockaddr *)&server[n], sizeof(struct sockaddr_in)) < 0 ){
			perror("ERROR binding socket");
			exit(1);
		}
		if( listen(ps->clifd,MAX_FPGAS) ){
            perror("ERROR listening ENETsock");
            exit(1);
        }
	}
}


void acceptENETconnection(struct POLLsock **psock, struct POLLsock *tmp){ /* function to accept incoming ethernet connections from the socs */
    /*  This function accepts new ethernet connections from the SoC's after the listening socket gets a connection request.
        - After receiving the connection request, the function loops through the array of file descriptors for the connected devices (ENET->clifd) and
          accepts the new connection into the first empty spot.
        - TCP_NODELAY is then set on the socket to allow the server to send small packets to the SoC's quickly.
        - The first thing the clients do after connecting to the server is send a message containing the number identifying the board.
        - The socket recv's this message into 'enetmsg' and stores the value in the array of board numbers (ENET->board)
    */
    char *ipaddr;
    char *ipfields[4];
    struct POLLsock *ps;
    struct sockaddr_in client, peername;
    socklen_t clilen, peerlen;
    int recvbuff;
    recvbuff = MAX_RECLEN*sizeof(uint64_t);
    memset(&client,0,sizeof(struct sockaddr_in));
    clilen = sizeof(client);
    memset(&peername,0,sizeof(struct sockaddr_in));
    peerlen = sizeof(peername);
    
    addPollSock(psock);
    ps = (*psock);
    ps->clifd = accept(tmp->clifd, (struct sockaddr *)&client, &clilen);

    /* get the last digits of the ip address as an identifier for the boards */
    if( getpeername(ps->clifd, (struct sockaddr *)&peername, &peerlen) ){
        perror("ERROR getting peername");
    } else {
        int i;
        ipaddr = strtok(inet_ntoa(peername.sin_addr),".");
        
        for(i=0;i<4;i++){
            ipfields[i] = ipaddr;
            ipaddr = strtok(NULL,".");
        }
    }

    ps->ipAddr = atoi(ipfields[3]);
    ps->portNum = tmp->portNum;
    ps->is_enet = 1;
    ps->is_listener = 0;
    setnonblocking(ps->clifd);
    //printf("ipAddr = %d\n", ps->ipAddr);
    if(ps->portNum != ENET_COMMPORT){
        updatePsockField_boardNum(psock, ps->ipAddr);
        
    } else {
        setsockopt(ps->clifd, IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(int));
    }

    ev.data.ptr = ps;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ps->clifd, &ev);
}


void broadcastENETmsg(struct POLLsock **psock, uint32_t *msg){
    int nsent;
    struct POLLsock *ps;
    ps = (*psock);
    while(ps != NULL){
        if( ps->is_enet && !ps->is_listener && ps->portNum == ENET_COMMPORT ){
            nsent = send(ps->clifd,msg,4*sizeof(uint32_t),0);
            setsockopt(ps->clifd,IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
        }
        ps = ps->next;
    }
}


void sendENETmsg(uint32_t *msg, int boardNum){ /* function to send messages to socs over ethernet */
    /* sends msg to board number boardNum
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */
    if(!boardNum){ // boardNum start at 1
        int n, nsent;
        for(n=0;n<MAX_FPGAS;n++){
            if(g_enetCommFd[n]!=0){
                nsent = send(g_enetCommFd[n],msg,4*sizeof(uint32_t),0);
                setsockopt(g_enetCommFd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
            }
        }
    } else {
        send(g_enetCommFd[boardNum],msg,4*sizeof(uint32_t),0);
        setsockopt(g_enetCommFd[boardNum],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
    }
}


void resetGlobalVars(){ /* function to reset all global variables, global variables are prepended with 'g_' */
    g_recLen = 2048; g_trigDelay = 0; g_packetsize = 2048;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
    g_queryTimeout = 1000;
}


void setDataAddrPointers(struct POLLsock **psock, uint8_t **data){
	/* updates the addresses in the 'data' array where the incoming data from each connected socket should be written */
    struct POLLsock *ps;
    ps = (*psock);
    uint8_t* dtmp;
    unsigned long pulseIdx;
    pulseIdx = (g_idx3len*(g_id1*g_idx2len+g_id2)+g_id3)*g_numBoards*g_recLen;
    int recvbuff;

    ps = (*psock);
    dtmp = (*data);
    while(ps!=NULL){
        if( ps->is_enet && !ps->is_listener && ps->portNum != ENET_COMMPORT ){
            ps->data_addr = &dtmp[ 8*( pulseIdx + g_enetBoardIdx[ps->boardNum]*g_recLen + (ps->portNum-ENET_COMMPORT-1)*g_packetsize ) ];
            if((g_recLen % g_packetsize)){
                if(ps->portNum == g_portMax){
                    ps->dataLen = g_recLen%g_packetsize;
                } else {
                    ps->dataLen = g_packetsize;
                }                
            } else {
                ps->dataLen = g_packetsize;
            }
            recvbuff = ps->dataLen*sizeof(uint64_t);
            setsockopt(ps->clifd, SOL_SOCKET, SO_RCVBUF, &recvbuff, sizeof(recvbuff));
        }
        ps = ps->next;
    }
}


void updateBoardGlobals(struct POLLsock **psock){ 
	/* updates the list of connected boards and related variables in the cServer */
	
    int l,k;
    struct POLLsock *ps,*ps0;
    int enetCommFd[MAX_FPGAS+1] = {0};
    g_portMax = 0;
    g_numBoards = 0;

    ps0 = (*psock);
    ps = (*psock);
    while( ps!=NULL ){
        if( ps->is_enet && !ps->is_listener && ps->portNum != ENET_COMMPORT ){
            g_portMax = (ps->portNum > g_portMax) ? ps->portNum : g_portMax;    
        } else if( ps->is_enet && !ps->is_listener && ps->portNum == ENET_COMMPORT ){
            enetCommFd[ps->boardNum] = ps->clifd;
            g_numBoards++;
        }
        ps = ps->next;
    }
    
    g_numPorts = g_portMax-ENET_COMMPORT; 
    l=0;
    for( k=0; k<MAX_FPGAS+1; k++ ){
        g_enetCommFd[k] = enetCommFd[k];
        g_connectedBoards[k] = 0;
        if(enetCommFd[k]!=0){
            g_connectedBoards[l] = k;
            g_enetBoardIdx[k] = l;
            l++;
        }
    }
    (*psock) = ps0;
}


void resetFPGAdataAcqParams(){ /* function to reset data acquisition variables on the SoCs */
    /* this function makes a message variable, populates it with the default data acquisition variables, and sends it to the SoCs*/
    uint32_t fmsg[4] ={0};

    fmsg[0] = CASE_TOGGLE_DATA_ACQUISITION; fmsg[1] = 0;
    sendENETmsg(fmsg,0);
    
    fmsg[0] = CASE_SET_TRIGGER_DELAY; fmsg[1] = g_trigDelay;
    sendENETmsg(fmsg,0);
    
    fmsg[0] = CASE_SET_RECORD_LENGTH; fmsg[1] = g_recLen; fmsg[2] = g_packetsize;
    sendENETmsg(fmsg,0);
    
    fmsg[0] = CASE_SET_QUERY_DATA_TIMEOUT; fmsg[1] = g_queryTimeout;
    sendENETmsg(fmsg,0);
}


int sendDataShm(uint8_t *data, int gettingKey) {
    // Save the size of the data, to be passed with the data to the Python UI
    int data_size = sizeof(uint8_t)*g_numBoards*g_idx1len*g_idx2len*g_idx3len*8*g_recLen;

    // Define objects to hold the shared memory id and key,
    // and a pointer to later access the shared memory
    static int shmid;
    static char *shared_memory;
    printf("gettingKey = %d\n",gettingKey);
    if(gettingKey){
        // Create a shared memory segment of size: data_size and obtain its shared memory id
        if((shmid = shmget(g_shmkey, data_size, IPC_CREAT | 0660)) < 0) {
            printf("Error getting shared memory id\n");
            return(-1);
        }

        // Make shared_memory point to the newly created shared memory segment
        if((shared_memory = shmat(shmid, NULL, 0)) == (char *) -1) {
            printf("Error attaching shared memory\n");
            return(-1);
        }

        // copy the data array into shared memory
        memcpy(shared_memory, data, data_size);

        printf("the memory is shared\n");
        // let python know the data is ready
        if(send(g_ipcCommFd,&shmid,sizeof(int),0) == -1){
            perror("IPC send failed, recvCount notification: ");
            exit(1);
        }
    } else if (!gettingKey){
        //usleep(100);
        //Detach and remove shared memory
        printf("releasing the shared memory\n");
        shmdt(shared_memory);
        shmctl(shmid, IPC_RMID, NULL);
    } else {
        printf("bad shared memory something or other\n");
        return(-1);
    }
    return(0);
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
    resetGlobalVars();                                                  /* sets all the global variables to their defualt values */
    g_numBoards = 0;
    g_portMax = 0;
    g_numPorts = 0;
    g_maxDataIdx = 8*MAX_FPGAS*g_recLen*sizeof(uint8_t);

    epfd = epoll_create(MAX_SOCKETS);

    struct POLLsock *psock = NULL;
    struct POLLsock *ps,*ps_tmp;
    addPollSock(&psock);

    setupIPCserver(&psock);                                                 /* calls function to listen for incoming ipc connections from (python) */
    setupENETserver(&psock);
    
    struct FIFOmsg fmsg;                                                /* creates messaging variable to carry ipc messages */
    uint32_t emsg[4];

    uint8_t *data;                                                      /* array to store acquired data */
    unsigned long data_idx;                                             /* index of where to write incoming data in the 'data' array */
    data = (uint8_t *)malloc(8*MAX_FPGAS*g_recLen*sizeof(uint8_t));     /* initial memory allocation for 'data' */

    int recvIndividually;
    int k,l,m,n;
    int dummy;
    int dataAcqGo;                                                      /* flag to put SoC in state to acquire data or not */
    int nfds,nrecv,nsent;												/* number of fds ready, number of bytes recv'd per read, number of bytes sent */
    int recvCount;                                         				/* sum of recv'd bytes from all SoCs */
    int runner;                                                         /* flag to run the server, closes program if set to 0 */
    int timeout_ms;
    recvIndividually = 0;
    k=0;
    dataAcqGo = 1;
    runner = 1;
    timeout_ms = 1000;
    while(runner == 1){
        /* polls the fds in readfds for readable data, returns number of fds with data (nready), returns '0' if it times out. */
        nfds = epoll_wait(epfd, events, MAX_SOCKETS, timeout_ms);          
        
        if( nfds > 0 ){
            for(n = 0; n < nfds; n++){

                ps = (struct POLLsock *)events[n].data.ptr;
                if(!ps->is_enet){
                    if( ps->is_listener ){
                        acceptIPCconnection(&psock,ps);
                    } else if ( events[n].events & EPOLLIN ) {
                        /* The IPC socket is used to handle all messages between the cServer and the user interface (python) 
                            all messages from (python) contain two fields 'msg' and 'buff' ( given in struct FIFOmsg )
                            - 'msg' is a 4-element array of type uint32_t
                            - 'buff' is a character array with 100 elements (primarily/only used to declare the file name when saving data)
                            - msg[0] is a number representing the name of the command given by the 'CASE_...' definitions above
                            - msg[1]-msg[3] are 32-bit numerical arguments/values passed to the given command (eg for msg[0] = CASE_SET_TRIGGER_DELAY,
                              msg[1] = trigDelay)
                        */	
                        
                        fmsg.msg[0]=0; fmsg.msg[1]=0; fmsg.msg[2]=0; fmsg.msg[3]=0; /* resets all the 'msg' variables before reading new ones */
                        nrecv = recv(ps->clifd,&fmsg,sizeof(struct FIFOmsg),MSG_WAITALL);
                        if ( nrecv < 0 ){
                            printf("IPC read error, shutting down\n");
                            break;  /* error condition 'breaks' out of the while loop and shuts down server */

                        } else if(nrecv == 0){ /* nrecv == 0 if connection closed. closed connections are deleted */
                            deletePollSock(&psock,ps->clifd);

                        } else {
                            switch(fmsg.msg[0]){ /* msg[0] contains the command code for the message */
                                /* switch statement notes:
                                    - msg[0] is the case
                                    - msg[1]-msg[3] are the values to be set depending on the case, ie trigger delay, reclen, etc
                                    - all messages which change the values of data acquisition variables update the corresponding global variables
                                    - illegal values in msg[1]-msg[3] (eg trigDelay < 0) cause the corresponding variables to be set to their defualts
                                */

                                case(CASE_SET_TRIGGER_DELAY):{ 
                                    /* Sets the time delay between when the FPGA receives the trigger and when acquisition begins
                                        - msg[1] contains the trigDelay value to be transmitted to the SoCs
                                        - msg[2],msg[3], and buff are not used
                                     */
                                    if(fmsg.msg[1] >= 0){
                                        g_trigDelay = fmsg.msg[1];
                                        sendENETmsg(fmsg.msg,0);
                                        printf("trigDelay set to: %lu us\n\n",g_trigDelay);
                                    } else {
                                        g_trigDelay = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(fmsg.msg,0);
                                        printf("invalid trigDelay value, defaulting to 0 us\n\n");
                                    }
                                    break;
                                }

                                case(CASE_SET_RECORD_LENGTH):{ 
                                    /* Sets the record length and enet packetsize 
                                        - msg[1] contains the record length [ 0 - MAX_RECLEN ]
											- this is the number of data points to acquire per pulse, NOT the time duration of the acquisition
                                        - msg[2] contains the enet packetsize [ MIN_PACKETSIZE - recLen ]
											- this is the number of data points that get transmitted at a time per ethernet socket
												- eg. for recLen = 4096, packetsize = 1024, the data points are split into 4 segments and 4 separate sockets are opened which concurrently send their respective data segments
												
												* note: this is really helpful for getting around TCP's SYN-ACK latency issue. when sending data through TCP connections, only 1500 bytes get sent at once (even if you specify the size of your send to be larger, it's just how it is and there's very little to be done about it (the FPGAs/SocS don't support jumbo frames)). to ensure that all the data got where it was supposed to, TCP has to do a SYN-ACK handshake after each 1500 byte transmit, which takes time and prevents and more data from being sent until it is complete, which slows down everything. one socket waiting for the SYN-ACK handshake, doesn't affect the actions of the other sockets though, which can send their data while the first socket is waiting. by splitting the data up into smaller packets we can parallelize the delay incurred by all the SYN-ACK handshakes and improve overall transmission speed. it's a balance though, there's a penalty for sending packets that are 'too small' and opening up too many sockets puts a burden on the server, which has to constantly check them all.
											- if the packetsize is larger than the recLen it gets set equal to the recLen
                                        - msg[3] and buff are unused
                                     */
                                    if(fmsg.msg[1] >= MIN_PACKETSIZE && fmsg.msg[1] <= MAX_RECLEN){
                                        g_recLen = fmsg.msg[1];
                                        if( fmsg.msg[2] >= MIN_PACKETSIZE && fmsg.msg[2] <= fmsg.msg[1] ){
                                            g_packetsize = fmsg.msg[2];
                                        } else {
                                            if(fmsg.msg[2] != 0)
                                                printf("invalid packetsize (%d), setting equal to recLen (%d)\n",fmsg.msg[2],fmsg.msg[1]);
                                            g_packetsize = fmsg.msg[1];
                                            fmsg.msg[2] = fmsg.msg[1];
                                        }
                                        sendENETmsg(fmsg.msg,0);
                                        printf("[ recLen, packetsize ] set to: [ %lu, %lu ]\n\n",g_recLen, g_packetsize);
                                    } else {
                                        printf("invalid recLen, reseting global variables\n"); 
                                        resetGlobalVars();
                                        resetFPGAdataAcqParams();
                                        data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                        g_maxDataIdx = MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t);
                                        printf("global variables reset to defaults\n");
                                    }
                                    break;
                                }

								case(CASE_SET_INTERLEAVE_DEPTH_AND_TIMER):{
									/* Breaks up how many FPGAs transmit their data over ethernet at once, and sets the delay between when each group starts transmitting data
										- msg[1]: divides the FPGAs into separate groups to transmit data
											* FPGAs are split into groups based on their boardNumber where group[N] is comprised on all FPGAs where (boardNumber[N] % msg[1]) == N
										- msg[2]: sets the delay between when each group transmits data (units = us)
											* transmit time group[N] = N*msg[2] us
										- msg[3]: if packetsize < recLen, sets the delay between when each packet is sent by the FPGA
											* transmit time packet[N,M] = (transmit time group[N]) + packet[N,M]*msg[3]
											* note: I've not found a case where this is beneficial so it may eventually be removed
                                     */
                                    if( fmsg.msg[1] > 0 && fmsg.msg[2] < 5000 && fmsg.msg[3] < 5000){
                                        printf("boardModulo = %u, moduloTimer = %u, packetWait = %u\n\n",fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                                    } else {
                                        fmsg.msg[1] = 1;
                                        fmsg.msg[2] = 0;
                                        fmsg.msg[3] = 0;
                                        printf("invalid boardModulo, moduloTimer setting to 1, 0\n\n");
                                    }
                                    sendENETmsg(fmsg.msg,0);
                                    break;
                                }

                                case(CASE_SET_CSERVER_DATA_ARRAY_SIZE):{ 
                                    /* sets the variables used to determine the size of the array into which to store the acquired data
										- msg[1], msg[2], msg[3] are all user defined variables that define the number acquisition events, which is used to determine how much memory needs to be allocated to store the data
											* eg. number of: steering locations, pulses-per-point, chargetimes / pulse amplitudes, etc.
										- number of acquisition events = msg[1]*msg[2]*msg[3]
                                    */
                                    g_idx1len = ( 1 > fmsg.msg[1] ) ? 1 : fmsg.msg[1];
                                    g_idx2len = ( 1 > fmsg.msg[2] ) ? 1 : fmsg.msg[2];
                                    g_idx3len = ( 1 > fmsg.msg[3] ) ? 1 : fmsg.msg[3];
                                    printf("Data Array Dimensions set to: [%lu, %lu, %lu, recLen, nElements]\n\n",g_idx1len,g_idx2len,g_idx3len); 
                                    break;
                                }

                                case(CASE_ALLOCATE_CSERVER_DATA_ARRAY_MEM):{ 
                                    /* Allocates memory for the acquired data to be store in. this is a separate call from the other functions because multiple variables can be set to change the size of the data to be acquired. allocating and/or reallocating the memory after each variable is set would be kind of wasteful.
                                        - msg[1], msg[2], msg[3], and buff are not used.
                                    */
									free(data);
									data = (uint8_t *)malloc(g_numBoards*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
									printf("data realloc'd to size [%lu, %lu, %lu, %lu, %u], %lu\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,g_numBoards,g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t)*g_numBoards);
									g_maxDataIdx = g_numBoards*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t);
									
                                    updateBoardGlobals(&psock);
                                    setDataAddrPointers(&psock,&data);
                                    break;
                                }

                                case(CASE_TOGGLE_DATA_ACQUISITION):{ 
                                    /* tells the SoC whether to be in a data acquisition mode or not 
                                        notes about variables in fmsg:
                                        - msg[1]: if msg[1] == 1, puts the SoC into a state to acquire data. msg[1]!=1 puts FPGA into state to wait for ethernet messages from cServer
                                        - msg[2,3] and buff are not used
                                    */
                                    if(fmsg.msg[1] == 1){
                                        dataAcqGo = fmsg.msg[1];
                                        sendENETmsg(fmsg.msg,0);
                                        printf("data acquisition started\n\n");
                                    } else {
                                        dataAcqGo = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(fmsg.msg,0);
                                        printf("data acquisition stopped\n\n");
                                    }
                                    break;
                                }

                                case(CASE_DECLARE_CSERVER_DATA_ARRAY_INDEX):{ 
                                    /* sets the location in data array where incoming waveforms should be stored. needs to be set before pulse is delivered
										- msg[1], msg[2], msg[3]: array indices to store data
											* eg. pulse number, location number, amplitude number
											* idx = msg[1]*idx2max*idx3max + msg[2]*idx3max + msg[3]
										- if msg[1], msg[2], or msg[3] are < 0, idx is set to zero (will overwrite data)
										- if user doesn't declare index before each pulse, it auto-increments as idx += 1
                                    */ 
                                    if(fmsg.msg[1]>=0 && fmsg.msg[2]>=0 && fmsg.msg[3]>=0){
                                        g_id1 = fmsg.msg[1];
                                        g_id2 = fmsg.msg[2];
                                        g_id3 = fmsg.msg[3];
                                    } else {
                                        g_id1=0;g_id2=0;g_id3=0;
                                        printf("idxs must be >=0, defualting to [0,0,0]. recvd vals = [%d, %d, %d]\n\n", fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                                    }
                                    setDataAddrPointers(&psock,&data);
                                    break;
                                }

                                case(CASE_CLOSE_PROGRAM):{ 
                                    /* shuts down the SoCs but leaves the cServer running.
										- msg[1], msg[2], msg[3], and buff are unused.
                                    */
                                    sendENETmsg(fmsg.msg,0);
                                    printf("shutting down FPGAs\n");
                                    break;
                                }

                                case(CASE_RESET_GLOBAL_VARIABLES):{
                                    /* This function resets all variables to their defaults.
										- msg[1], msg[2], msg[3], and buff are unused.
                                    */
                                    resetGlobalVars();
                                    resetFPGAdataAcqParams();
                                    free(data);
                                    data = (uint8_t *)malloc(MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                    g_maxDataIdx = MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t);
                                    printf("global variables reset to defaults\n");
                                    printf("data reset to size [%lu, %lu, %lu, %lu, %d]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,MAX_FPGAS);
                                    setDataAddrPointers(&psock,&data);
                                    break;
                                }

                                case(CASE_SAVE_CSERVER_DATA):{
                                    /* saves the acquired data in the 'data' array into a binary file
                                        - msg[1], msg[2], and msg[3] are unused
                                        - buff contains the name of the file to save the data in
											* the string in 'buff' is limited to 100 characters
                                    */ 
                                    FILE *datafile = fopen(fmsg.buff,"wb"); 
                                    fwrite(data,sizeof(uint8_t),g_numBoards*g_idx1len*g_idx2len*g_idx3len*8*g_recLen,datafile);
                                    fclose(datafile);
                                    printf("data saved to file %s\n\n",fmsg.buff);
                                    break;                            
                                }

                                case(CASE_SEND_CSERVER_DATA_IPC):{
                                    /* copys 'data' into shared memory segment accessible to the python UI
                                        - msg[1], msg[2], msg[3], and buff are unused    
                                    */
                                    if(fmsg.msg[1] == 1){
                                        if(sendDataShm(data, 1) == -1){
                                            perror("IPC send error");
                                            exit(1);
                                        }
                                    } else {
                                        if(sendDataShm(data, 0) == -1){
                                            perror("IPC send error");
                                            exit(1);
                                        }
                                    }
                                    break;
                                }

                                case(CASE_QUERY_BOARD_INFO):{
									/* sends message to connected FPGAs querying their board numbers
                                        - msg[1], msg[2], msg[3], and buff are unused    
                                    */
                                    printf("Querying SoCs for board info\n");
                                    broadcastENETmsg(&psock,fmsg.msg);
                                    break;
                                }
                                
                                case(CASE_GET_BOARD_INFO_IPC):{
									/* sends the list of connected boards to the python UI
                                        - msg[1], msg[2], msg[3], and buff are unused    
                                    */
                                    printf("updating cServer board info\n");
                                    updateBoardGlobals(&psock);
                                    if(fmsg.msg[1]){
                                        printf("sending board info ipc\n");
                                        dummy = send(ps->clifd,g_connectedBoards,( MAX_FPGAS )*sizeof(uint32_t),MSG_CONFIRM);
                                        printf("nsent IPC %d, %lu\n",dummy, MAX_FPGAS*sizeof(uint32_t));
                                        if(dummy == -1){
                                            perror("IPC send failed\n");
                                            exit(1);
                                        }
                                    }
                                    break;
                                }
                                
                                case(CASE_SET_QUERY_DATA_TIMEOUT):{
									/* tells the SoC's how long to stay in a busy wait state while checking whether the FPGA has data ready
                                        - msg[1]: how long to query data (us)
                                        - msg[2], msg[3], and buff are unused    
                                    */
                                    if(fmsg.msg[1] > 999){
										g_queryTimeout = fmsg.msg[1];
									} else {
										g_queryTimeout = 10000;
									}
									sendENETmsg(fmsg.msg,0);
                                    break;
                                }
                                
                                case(CASE_QUERY_DATA):{
									/* sends a message to the SoCs telling them to send their data to the cServer
										- if packetsize < recLen
											- msg[1] == 1: FPGAs send packets one-at-a-time, ie packet[0] must finish sending before FPGA sends packet[1]
											- msg[1] != 1: FPGAs start sending packets at fixed intervals, ie transmit start time[packet[n]] = tpacketwait*n, where tpacketwait can be set in 'CASE_SET_INTERLEAVE_DEPTH_AND_TIMER'
												* this doesn't work as well as sending one-at-a-time from what I've seen
                                        - msg[2], msg[3], and buff are unused    
                                    */
                                    recvIndividually = 0;
                                    if(fmsg.msg[1])
                                        recvIndividually = 1;
                                    sendENETmsg(fmsg.msg,0);
                                    recvCount = 0;
                                    break;
                                }
                                
                                case(CASE_SHUTDOWN_SERVER):{
                                    /* This shuts down the SoCs and cServer 
                                        - msg[1], msg[2], msg[3], buff unused
                                    */
                                    fmsg.msg[0]=8;
                                    sendENETmsg(fmsg.msg,0);
                                    runner = 0;
                                    printf("shutting server down\n\n");
                                    break;
                                }

                                default:{
                                    /* The default action if an undefined CASE value is detected is to and shut down the server
                                     */
                                     
                                    printf("invalid message, shutting down server,%d, %d, %d, %d\n",fmsg.msg[0],fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                                    fmsg.msg[0]=8; 
                                    sendENETmsg(fmsg.msg,0);
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    if( ps->is_listener ){ /* accept new connections from socs */
                        acceptENETconnection( &psock, ps );
                    
                    } else if( ps->portNum == ENET_COMMPORT ) { /* handle incoming communications from socs */
                        printf("Recieving from commport\n");
                        dummy = recv(ps->clifd,&emsg,4*sizeof(uint32_t),0);
                        if( dummy > 0 ){
                            setsockopt(ps->clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
                            ps->boardNum = emsg[0];
                            g_enetCommFd[ps->boardNum] = ps->clifd;
                            printf("Connected to board %d, port %d ",ps->boardNum,ps->portNum);
                            if( send(g_ipcCommFd,&ps->boardNum,sizeof(int),0) ){
								printf("[ communications successful ]\n");
							} else {
								printf("[ communications failed ]\n");
							}
                        } else {
                            if( dummy == -1 ) perror("data recv error: ");
                            deletePollSock(&psock,ps->clifd);
                        }   
                    
                    } else if( events[n].events & EPOLLIN ){ /* handle incoming data from socs */
                        nrecv = recv(ps->clifd,(ps->data_addr+ps->p_idx),ps->dataLen*8*sizeof(uint8_t),0);
                        if( nrecv > 0 ){
                            setsockopt(ps->clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 
                            recvCount += nrecv;
                            ps->p_idx += nrecv; 
                        } else {
                            if( nrecv == -1 )
                                perror("data recv error: ");
                            deletePollSock(&psock,ps->clifd);
                        }
                        
                        /* if all data from port N has been acquired, send message to FPGA to send next packet */
                        if( recvIndividually ){ 
                            if( ps->p_idx == 8*ps->dataLen ){
                                emsg[0] = CASE_QUERY_DATA; emsg[1] = 1; emsg[2] = ps->portNum - INIT_PORT;
                                sendENETmsg(emsg,ps->boardNum);
                            }
                        }
                        
                        /* if all data has been collected by cServer, let the python UI know so it can move on with the program */
                        if( recvCount == g_recLen*8*sizeof(uint8_t)*g_numBoards ){
                            if(send(g_ipcCommFd,&n,sizeof(int),0) == -1){
                                perror("IPC send failed, recvCount notification: ");
                                exit(1);
                            }
                            /* reset the sockets partial index variables */
                            ps_tmp = psock;
                            while( ps_tmp != NULL ){
                                if( ( ps_tmp->is_enet ) && ( !ps_tmp->is_listener ) && ( ps_tmp->portNum != ENET_COMMPORT ) ){
                                    ps_tmp->p_idx=0;
                                    if( ps_tmp->data_addr-&data[0] > g_maxDataIdx ){
                                        printf("can't write to memory location, overwriting previous location, %lu\n",ps_tmp->data_addr-&data[0]);
                                        ps_tmp->data_addr-=g_numBoards*8*g_recLen;
                                    } else {
                                        ps_tmp->data_addr+=g_numBoards*8*g_recLen;
                                    }
                                }
                                ps_tmp = ps_tmp->next;
                            }
                            k++;
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
    while(psock!=NULL){
        ps = psock;
        psock = ps->next;
        free(ps);
    }
    printf("successfully exited!\n");
    exit(0);
}
