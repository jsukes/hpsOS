
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
#define CASE_QUERY_DATA 16
#define CASE_ARDUINO_TRIG_NUM 20
#define CASE_ARDUINO_TRIG_VALS 21
#define CASE_ARDUINO_TRIG_WAITS 22
#define CASE_ARDUINO_TRIG_COMMS 23
#define CASE_ARDUINO_TRIG 24
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

int g_ipcCommFd;
int g_enetCommFd[MAX_FPGAS+1] = {0};
int g_enetBoardIdx[MAX_FPGAS+1] = {0};

key_t g_shmkey = IPC_SHARED_MEM_KEY;

int epfd;
struct epoll_event ev;
struct epoll_event events[MAX_SOCKETS];


pthread_mutex_t sockMutex;
pthread_cond_t sockCond;

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


struct task{
    struct POLLsock *ps;
    struct task *next;
};


struct task *readhead = NULL, *readtail = NULL;


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
            //printf("broadcasting\n");
            nsent = send(ps->clifd,msg,4*sizeof(uint32_t),0);
            //printf("broadcast nsent = %d: %u, %u, %u, %u\n",nsent,msg[0],msg[1],msg[2],msg[3]);
            setsockopt(ps->clifd,IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
        }
        ps = ps->next;
    }
}


void sendENETmsg(uint32_t *msg, int boardNum){ /* function to send messages to socs over ethernet */
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs
       connected to the server
        
        - msg is a 4 element array
        - msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of
          msg[1]-msg[3]
        - msg[1]-msg[3] contain 32-bit numbers with data for the SoCs depending on the function (ie recLen or trigDelay)
    */
    if(!boardNum){
        int n, nsent;
        for(n=0;n<MAX_FPGAS;n++){
            if(g_enetCommFd[n]!=0){
                nsent = send(g_enetCommFd[n],msg,4*sizeof(uint32_t),0);
                //printf("nsent = %d: %u, %u, %u, %u\n",nsent,msg[0],msg[1],msg[2],msg[3]);
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
}


void setDataAddrPointers(struct POLLsock **psock, uint8_t **data){
    struct POLLsock *ps;
    ps = (*psock);
    uint8_t* dtmp;
    unsigned long pulseIdx;
    pulseIdx = (g_idx3len*(g_id1*g_idx2len+g_id2)+g_id3)*g_numBoards*g_recLen;
    int recvbuff;

    ps = (*psock);
    dtmp = (*data);
    //printf("g_portmax = %d\n",g_portMax);
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
    int l,k;
    struct POLLsock *ps,*ps0;
    int enetCommFd[MAX_FPGAS+1] = {0};
    g_portMax = 0;
    g_numBoards = 0;

    ps0 = (*psock);
    ps = (*psock);
    while( ps!=NULL ){
        //printf("sockfd %d, portNum %d\n", ps->clifd, ps->portNum);
        if( ps->is_enet && !ps->is_listener && ps->portNum != ENET_COMMPORT ){
            g_portMax = (ps->portNum > g_portMax) ? ps->portNum : g_portMax;    
        } else if( ps->is_enet && !ps->is_listener && ps->portNum == ENET_COMMPORT ){
            enetCommFd[ps->boardNum] = ps->clifd;
            //printf("commfd = %d, %d\n", enetCommFd[ps->boardNum],ps->boardNum);
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
            //printf("k=%d, l=%d\n",k,l);
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


void *getDataTask(void *args){

    while(1){
        pthread_mutex_lock(&sockMutex);
        while(readhead == NULL)
            pthread_cond_wait(&sockCond,&sockMutex);

        struct task* tmp = readhead;
        readhead = readhead->next;
        free(tmp);
        pthread_mutex_unlock(&sockMutex);

        nrecv = recv(readhead->ps->clifd,(readhead->ps->data_addr+readhead->ps->p_idx),readhead->ps->dataLen*8*sizeof(uint8_t),0);
        if( nrecv > 0 ){
            setsockopt(readhead->ps->clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 
            recvCount += nrecv;
            readhead->ps->p_idx += nrecv; 
        } else {
            if( nrecv == -1 )
                perror("data recv error: ");
        }
        if( recvIndividually ){
            if( ps->p_idx == 8*ps->dataLen ){
                emsg[0] = CASE_QUERY_DATA; emsg[1] = 1; emsg[2] = ps->portNum - INIT_PORT;
                sendENETmsg(emsg,ps->boardNum);
            }
        }

    }   



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

    pthread_t *tid;
    tid = (pthread_t *)malloc(10*sizeof(pthread_t));
    struct task *new_task = NULL;

    pthread_mutex_init(&sockMutex,NULL);
    pthread_cond_init(&sockCond,NULL);
    for(data_idx=0;data_idx<10;data_idx++){
        pthread_create(&tid[data_idx],NULL,getDataTask,NULL);
    }

    int recvIndividually;
    int k,l,m,n;
    int dummy;
    int dataAcqGo;                                                      /* flag to put SoC in state to acquire data or not */
    int nfds,nrecv,recvCount,nsent;                                         /* number of fds ready, number of bytes recv'd per read, sum of nrecv'd */
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

                        } else if(nrecv == 0){
                            deletePollSock(&psock,ps->clifd);

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
                                    /* This sets the length of the array into which data will be stored, NOT the time duration of the acquisition
                                        notes on variables in fmsg:
                                        - msg[1] contains the record length of the data to be transmitted to the SoCs
                                        - msg[2] and buff are not used
                                        - msg[3] contains no data. record lengths cannot be independently set on the boards. any attempts to do so are
                                          overwritten
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
                                        g_maxDataIdx = g_numBoards*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t);
                                    } else {
                                        free(data);
                                        uint8_t *data;
                                        data = (uint8_t *)malloc(MAX_FPGAS*2*g_recLen*sizeof(uint64_t));
                                        printf("allocator error\n");
                                        g_maxDataIdx = MAX_FPGAS*2*g_recLen*sizeof(uint64_t);
                                    }
                                    updateBoardGlobals(&psock);
                                    setDataAddrPointers(&psock,&data);
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
                                    setDataAddrPointers(&psock,&data);
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
                                    sendENETmsg(fmsg.msg,0);
                                    printf("shutting down FPGAs\n");
                                    break;
                                }

                                case(CASE_RESET_GLOBAL_VARIABLES):{
                                    /* This function resets all variables to their defaults. 
                                        all variables in fmsg are unused.    
                                    */
                                    resetGlobalVars();
                                    resetFPGAdataAcqParams();
                                    data = (uint8_t *)realloc(data,MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t));
                                    g_maxDataIdx = MAX_FPGAS*g_idx1len*g_idx2len*g_idx3len*g_recLen*sizeof(uint64_t);
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
                                    printf("Querying SoCs for board info\n");
                                    broadcastENETmsg(&psock,fmsg.msg);
                                    printf("broadcasted\n");
                                    break;
                                }
                                
                                case(CASE_GET_BOARD_INFO_IPC):{
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
                                
                                case(CASE_QUERY_DATA):{
                                    recvIndividually = 0;
                                    if(fmsg.msg[1])
                                        recvIndividually = 1;
                                    sendENETmsg(fmsg.msg,0);
                                    recvCount = 0;
                                    break;
                                }
                                
                                case(CASE_ARDUINO_TRIG_NUM):{
									sendENETmsg(fmsg.msg,0);
									break;
								}
								
								case(CASE_ARDUINO_TRIG_VALS):{
									sendENETmsg(fmsg.msg,0);
									break;
								}
								
								case(CASE_ARDUINO_TRIG_WAITS):{
									sendENETmsg(fmsg.msg,0);
									break;
								}
								
								case(CASE_ARDUINO_TRIG_COMMS):{
									sendENETmsg(fmsg.msg,0);
									break;
								}
								
								case(CASE_ARDUINO_TRIG):{
									sendENETmsg(fmsg.msg,0);
									break;
								}
                                
                                case(CASE_SHUTDOWN_SERVER):{
                                    /* This shuts down the SoCs and cServer 
                                        note on the variables in fmgs:
                                        - no variables are used. if this command is issued it locally sets all the msg variables to what they need to be to
                                          shutdown the sever.
                                    */
                                    fmsg.msg[0]=8;
                                    sendENETmsg(fmsg.msg,0);
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
                                    sendENETmsg(fmsg.msg,0);
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    if( ps->is_listener ){ /* accept new connections from socs */
                        acceptENETconnection(&psock,ps);
                    
                    } else if( ps->portNum == ENET_COMMPORT ) { /* handle incoming communications from socs */
                        printf("trying to recv from commport\n");
                        dummy = recv(ps->clifd,&emsg,4*sizeof(uint32_t),0);
                        if( dummy > 0 ){
                            printf("recvd %d from commport\n",dummy);
                            setsockopt(ps->clifd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
                            ps->boardNum = emsg[0];
                            g_enetCommFd[ps->boardNum] = ps->clifd;
                            printf("connected to board %d, port %d\n",ps->boardNum,ps->portNum);
                            printf("nsent = %lu\n", send(g_ipcCommFd,&ps->boardNum,sizeof(int),0));
                        } else {
                            if( dummy == -1 )
                                perror("data recv error: ");
                            deletePollSock(&psock,ps->clifd);
                        }   
                    
                    } else if( events[n].events & EPOLLIN ){ /* handle incoming data from socs */
                        
                        new_task = malloc(sizeof(struct task));
                        new_task->ps = ps;
                        new_task->next = NULL;

                        pthread_mutex_lock(&sockMutex);
                        if ( readhead == NULL ){
                            readhead = new_task;
                            readtail = new_task;
                        } else {
                            readtail->next = new_task;
                            readtail = new_task;
                        }
                        pthread_cond_broadcast(&sockCond);
                        pthread_mutex_unlock(&sockMutex);

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
