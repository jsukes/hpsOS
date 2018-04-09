
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
#include <pthread.h>
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
unsigned long g_concurrentSenders;

uint32_t g_connectedBoards[MAX_FPGAS+1];
uint32_t g_numBoards, g_portMax, g_numPorts;

int g_enetCommFd[MAX_FPGAS+1] = {0};
int g_enetBoardIdx[MAX_FPGAS+1] = {0};
int g_recvCount = 0;


struct task{
    epoll_data_t data;
    struct task* next;
};


int epfd;
struct epoll_event ev;
struct epoll_event events[MAX_SOCKETS];
pthread_mutex_t r_mutex;
pthread_cond_t r_condl;
struct task *readhead = NULL, *readtail = NULL;


struct FIFOmsg{ /* structure to store variables for communications between cServer and (python) via FIFO instead of ipc socket */
    uint32_t msg[4];
    char buff[100]; 
};


struct POLLsock{
    int clifd;
    int is_enet;
    int is_listener;
    int boardNum;
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
    struct POLLsock *ps, *next;
    ps = (struct POLLsock *)malloc(sizeof(struct POLLsock));
    if(*psock == NULL){
        ps->prev = NULL;
        ps->next = NULL;
        ps->is_enet = 0;
        ps->clifd = 0;
        ps->is_listener = 0;
        *psock = ps;
    } else {
        ps->prev = *psock;
        ps->next = (*psock)->next;
        if(ps->next != NULL)
            (ps->next)->prev = ps;
        (*psock)->next = ps;
    }
}


void deletePollSock(struct POLLsock **psock){ 
    struct POLLsock *ps, *prev, *next;
    ps = (*psock);
    next = (*psock)->next;
    prev = (*psock)->prev;
    printf("prevfd %d, nextfd %d\n",prev->clifd,next->clifd);
    prev->next = next;
    if(next != NULL){
        next->prev = prev;
    } else {

        printf("NULLLLLLLLL\n");
    }
    printf("prevfd %d, curfd %d, nextfd %d\n",prev->clifd,ps->clifd,next->clifd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, ps->clifd, &ev);
    close(ps->clifd);
    free(ps);
}


void setupIPCserver(struct POLLsock **psock){ /* function to open listening ipc socket for connections from other processes (python server) */
    struct POLLsock *ps;
    struct sockaddr_un local;
    
    addPollSock(psock);
    ps = (*psock)->next;
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
    ps = (*psock)->next;
    ps->portNum = IPC_COMMPORT;
    ps->is_enet = 0;
    ps->is_listener = 0;

    ps->clifd = accept(tmp->clifd, (struct sockaddr *)&remote, &clilen);
    setnonblocking(ps->clifd);
    ev.data.ptr = ps;
    ev.events = EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,ps->clifd,&ev);
    printf("IPC socket accepted %d\n",ps->clifd);
}


void setupENETserver(struct POLLsock **psock){ /* function to set up ethernet socket to listen for incoming connections */
    struct POLLsock *ps;
    struct sockaddr_in server[MAX_PORTS];

    int n;
    for(n=0;n<MAX_PORTS;n++){
        addPollSock(psock);
        ps = (*psock)->next;
        ps->is_enet = 1;
        ps->is_listener = 1;
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
    struct POLLsock *ps;
    struct sockaddr_in client;
    socklen_t clilen;
    
    memset(&client,0,sizeof(struct sockaddr_in));
    clilen = sizeof(client);
    
    addPollSock(psock);
    ps = (*psock)->next;
    ps->clifd = accept(tmp->clifd, (struct sockaddr *)&client, &clilen);
    ps->portNum = tmp->portNum;
    ps->is_enet = 1;
    ps->is_listener = 0;
    setnonblocking(ps->clifd);
    
    ev.data.ptr = ps;
    if(ps->portNum == ENET_COMMPORT){
        ev.events = EPOLLIN;
    } else {
        ev.events = EPOLLIN | EPOLLET;
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, ps->clifd, &ev);
    setsockopt(ps->clifd, IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(int));
    
    int n;
    int msg[4];
    n = recv(ps->clifd,&msg,4*sizeof(int),MSG_WAITALL);
    ps->boardNum = msg[0];
    if(ps->portNum == ENET_COMMPORT){
        g_enetCommFd[ps->boardNum] = ps->clifd;
    }
    printf("connected to board %d, port %d\n",ps->boardNum,ps->portNum);
}


void sendENETmsg(uint32_t *msg){ /* function to send messages to socs over ethernet */
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs
       connected to the server
        
        - msg is a 4 element array
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3]
        - msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */
    int n;
    for(n=0;n<MAX_FPGAS;n++){
        if(g_enetCommFd[n]!=0){
            send(g_enetCommFd[n],msg,4*sizeof(uint32_t),0);
            setsockopt(g_enetCommFd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
        }
    }
}


void sendENETmsgSingle(uint32_t *msg, int boardNum){ /* function to send messages to socs over ethernet */
    send(g_enetCommFd[boardNum],msg,4*sizeof(uint32_t),0);
    setsockopt(g_enetCommFd[boardNum],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
}


void resetGlobalVars(){ /* function to reset all global variables, global variables are prepended with 'g_' */
    g_recLen = 2048; g_trigDelay = 0; g_packetsize = 512;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
    g_concurrentSenders = 0;
}


void setDataAddrPointers(struct POLLsock **psock, uint8_t **data){
    g_numBoards = 0;
    struct POLLsock* ps;
    ps = (*psock);
    uint8_t* dtmp;

    ps = (*psock);
    dtmp = (*data);
    while(ps!=NULL){
        if( ps->is_enet && !ps->is_listener && ps->portNum != ENET_COMMPORT ){
            ps->data_addr = &dtmp[8*(g_enetBoardIdx[ps->boardNum]*g_recLen+(ps->portNum-ENET_COMMPORT-1)*g_packetsize)];
            if(g_recLen % g_packetsize){
                if(ps->portNum == g_portMax){
                    ps->dataLen = g_recLen%g_packetsize;
                } else {
                    ps->dataLen = g_packetsize;
                }                
            } else {
                ps->dataLen = g_packetsize;
            }
        }
        ps = ps->next;
    }
}


void updateBoardInfo(struct POLLsock **psock){
    int l,k;
    struct POLLsock *ps;
    ps = (*psock);
    int enetCommFd[MAX_FPGAS+1] = {0};
    g_portMax = 0;
    g_numBoards = 0;

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
}


void resetFPGAdataAcqParams(){ /* function to reset data acquisition variables on the SoCs */
    /* this function makes a message variable, populates it with the default data acquisition variables, and sends it to the SoCs*/
    uint32_t fmsg[4] ={0};

    fmsg[0] = CASE_TOGGLE_DATA_ACQUISITION; fmsg[1] = 0;
    sendENETmsg(fmsg);
    
    fmsg[0] = CASE_SET_TRIGGER_DELAY; fmsg[1] = g_trigDelay;
    sendENETmsg(fmsg);
    
    fmsg[0] = CASE_SET_RECORD_LENGTH; fmsg[1] = g_recLen;
    sendENETmsg(fmsg);

    fmsg[0] = CASE_SET_PACKETSIZE; fmsg[1] = g_packetsize;
    sendENETmsg(fmsg);
}


void *readtask(void *args){
    int nrecv;
    struct POLLsock *ps;

    while(1){
        pthread_mutex_lock(&r_mutex);
        while(readhead == NULL)
            pthread_cond_wait(&r_condl,&r_mutex);

        ps = (struct POLLsock *)readhead->data.ptr;

        struct task* tmp = readhead;
        readhead = readhead->next;
        free(tmp);

        pthread_mutex_unlock(&r_mutex);

        nrecv = recv(ps->clifd,(ps->data_addr+ps->p_idx),g_packetsize*8*sizeof(uint8_t),0);
        
        if (nrecv > 0){
            setsockopt(ps->clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 
            ps->p_idx += nrecv; 
        } else {
            if (nrecv == -1) perror("data recv error: ");
            printf("deleting,%d,%d\n",ps->clifd,ps->boardNum);
        pthread_mutex_lock(&r_mutex);
            deletePollSock(&ps);
        pthread_mutex_unlock(&r_mutex);
            printf("passed\n");
        }

        pthread_mutex_lock(&r_mutex);
        g_recvCount+=nrecv;
        pthread_mutex_unlock(&r_mutex);
    }
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
    resetGlobalVars();                                                  /* sets all the global variables to their defualt values */
    g_numBoards = 0;
    g_portMax = 0;
    g_numPorts = 0;

    epfd = epoll_create(MAX_SOCKETS);

    struct POLLsock *psock = NULL;
    addPollSock(&psock);
    setupIPCserver(&psock);                                                 /* calls function to listen for incoming ipc connections from (python) */
    setupENETserver(&psock);
    struct POLLsock *ps;

    pthread_t tid1,tid2;
    struct task *new_task = NULL;
    pthread_mutex_init(&r_mutex,NULL);
    pthread_cond_init(&r_condl,NULL);
    pthread_create(&tid1, NULL, readtask, NULL);
    pthread_create(&tid2, NULL, readtask, NULL);

    int ipcCommFd = 0;
    struct FIFOmsg fmsg;                                                /* creates messaging variable to carry ipc messages */

    uint8_t *data;                                                      /* array to store acquired data */
    unsigned long data_idx;                                             /* index of where to write incoming data in the 'data' array */
    data = (uint8_t *)malloc(8*MAX_FPGAS*g_recLen*sizeof(uint8_t));     /* initial memory allocation for 'data' */

    int k,l,m,n;
    int dataAcqGo;                                                      /* flag to put SoC in state to acquire data or not */
    int nfds,nrecv,recvCount;                                         /* number of fds ready, number of bytes recv'd per read, sum of nrecv'd */
    int runner;                                                         /* flag to run the server, closes program if set to 0 */
    int timeout_ms;
    
    g_concurrentSenders = 0;
    k=0;
    dataAcqGo = 1;
    runner = 1;
    timeout_ms = 1000;
    while(runner == 1){
        /* polls the fds in readfds for readable data, returns number of fds with data (nready), returns '0' if it times out. */
        ps = psock;
        while(ps->next!=NULL){
            //printf("clifd %d, is_enet %d, is_listener %d, boardNum %d, portNum %d\n",ps->clifd,ps->is_enet,ps->is_listener,ps->boardNum,ps->portNum);
            ps = ps->next;
        }
        while(ps!=NULL){
            printf("clifd %d, is_enet %d, is_listener %d, boardNum %d, portNum %d\n",ps->clifd,ps->is_enet,ps->is_listener,ps->boardNum,ps->portNum);
            ps = ps->prev;
        }
        printf("psock clifd %d, is_enet %d, is_listener %d, boardNum %d, portNum %d\n",psock->clifd,psock->is_enet,psock->is_listener,psock->boardNum,psock->portNum);
        nfds = epoll_wait(epfd, events, MAX_SOCKETS, timeout_ms);          
		
        if( nfds > 0 ){
            for(n = 0; n < nfds; n++){
                ps = (struct POLLsock *)events[n].data.ptr;
                //printf("clifd %d, is_enet %d, is_listener %d, boardNum %d, portNum %d\n",ps->clifd,ps->is_enet,ps->is_listener,ps->boardNum,ps->portNum);
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

                        } else if(nrecv == 0){
                            deletePollSock(&ps);

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
                                        sendENETmsg(fmsg.msg);
                                        printf("trigDelay set to: %lu us\n\n",g_trigDelay);
                                    } else {
                                        g_trigDelay = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(fmsg.msg);
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
                                        sendENETmsg(fmsg.msg);
                                        printf("recLen set to: %lu\n\n",g_recLen);
                                        if(g_packetsize>g_recLen){
                                            printf("previous packetsize (%lu) too large, setting equal to recLen (%lu)\n",g_packetsize,g_recLen);
                                            g_packetsize = g_recLen;
                                            fmsg.msg[0] = CASE_SET_PACKETSIZE; fmsg.msg[1] = g_packetsize;
                                            sendENETmsg(fmsg.msg);
                                        }
                                    } else {
                                        printf("invalid recLen, reseting global variables\n"); 
                                        resetGlobalVars();
                                        k = 0;
                                        resetFPGAdataAcqParams();
                                        data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                        printf("global variables reset to defaults\n");
                                    }
                                    setDataAddrPointers(&psock,&data);
                                    break;
                                }

                                case(CASE_SET_PACKETSIZE):{
                                    if(fmsg.msg[1] <= g_recLen && fmsg.msg[1] >= MIN_PACKETSIZE){
                                        g_packetsize = fmsg.msg[1];
                                        sendENETmsg(fmsg.msg);
                                        printf("packetsize set to: %lu\n\n",g_packetsize);
                                    } else {
                                        printf("invalid packetsize (%lu), setting equal to recLen (%lu)\n",(unsigned long)fmsg.msg[1],g_recLen); 
                                        g_packetsize = g_recLen;
                                        fmsg.msg[1] = g_recLen;
                                        sendENETmsg(fmsg.msg);
                                    }
                                    setDataAddrPointers(&psock,&data);
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
                                        sendENETmsg(fmsg.msg);
                                        printf("data acquisition started\n\n");
                                    } else {
                                        dataAcqGo = 0;
                                        fmsg.msg[1] = 0;
                                        sendENETmsg(fmsg.msg);
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
                                    sendENETmsg(fmsg.msg);
                                    printf("shutting down FPGAs\n");
                                    break;
                                }

                                case(CASE_RESET_GLOBAL_VARIABLES):{
                                    /* This function resets all variables to their defaults. 
                                        all variables in fmsg are unused.    
                                    */
                                    resetGlobalVars();
                                    k = 0;
                                    resetFPGAdataAcqParams();
                                    data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                    printf("global variables reset to defaults\n");
                                    printf("data reset to size [%lu, %lu, %lu, %lu, %d]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,MAX_FPGAS);
                                    setDataAddrPointers(&psock,&data);
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
                                    if(send(ps->clifd,data,sizeof(uint8_t)*g_numBoards*g_idx1len*g_idx2len*g_idx3len*8*g_recLen,0) == -1){
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
                                    printf("trying to get boardCount\n");
                                    updateBoardInfo(&psock);
                                    if(send(ps->clifd,&g_numBoards,sizeof(uint32_t),MSG_CONFIRM) == -1){
                                        perror("IPC send failed\n");
                                        exit(1);
                                    }
                                    break;
                                }
                                
                                case(CASE_GET_BOARD_NUMS):{
                                    /* This function returns the board numbers of the connected socs to the python UI 
                                        - msg[1], msg[2], msg[3]*, and buff are unused.*/	
                                    printf("trying to get boardNums\n");
                                    updateBoardInfo(&psock);
                                    if(send(ps->clifd,g_connectedBoards,g_numBoards*sizeof(uint32_t),MSG_CONFIRM) == -1){
                                        perror("IPC send failed\n");
                                        exit(1);
                                    }
                                    break;
                                }
                                
                                case(CASE_QUERY_DATA):{
                                    if( g_concurrentSenders == 0 ){
                                        sendENETmsg(fmsg.msg);
                                        recvCount = 0;
                                        //printf("waiting for data\n\n");
                                    } else {
                                        for(n=0;n<g_concurrentSenders;n++){
                                            sendENETmsgSingle(fmsg.msg,n);
                                        }
                                        recvCount = 0;
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
                                    sendENETmsg(fmsg.msg);
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
                                    sendENETmsg(fmsg.msg);
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    if( ps->is_listener ){
                        acceptENETconnection(&psock,ps);
                        updateBoardInfo(&psock);
                    
                    } else if ( events[n].events & EPOLLIN ){
                        new_task = malloc(sizeof(struct task));
                        new_task->data.ptr = ps;
                        new_task->next = NULL;
                        pthread_mutex_lock(&r_mutex);
                        if(readhead == NULL){
                            readhead = new_task;
                            readtail = new_task;
                        } else {
                            readtail->next = new_task;
                            readtail = new_task;
                        }
                        pthread_cond_broadcast(&r_condl);
                        pthread_mutex_unlock(&r_mutex);

                        /* if all data has been collected by cServer, let the python UI know so it can move on with the program */
                        if( g_recvCount == g_recLen*8*sizeof(uint8_t)*g_numBoards ){
							printf("send\n");
                            if(send(ipcCommFd,&n,sizeof(int),0) == -1){
                                perror("IPC send failed, recvCount notification: ");
                                exit(1);
                            }
                            /* reset the sockets partial index variables */
                            ps = psock;
                            while(psock->next!=NULL){
                                if((psock->is_enet) && (!psock->is_listener) && (psock->portNum != ENET_COMMPORT) ){
                                    psock->p_idx=0;
                                    psock->data_addr+=g_numBoards*8*g_recLen;
                                }
                                psock = psock->next;
                            }
                            psock = ps;
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
