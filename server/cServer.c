
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

#define INIT_PORT 3400
#define MAX_FPGAS 64
#define DATA_FIFO "data_pipe"
#define IPCSOCK "./lithium_ipc"
#define MAX_RECLEN 8192

#define CASE_TRIGDELAY 0
#define CASE_RECLEN 1
#define CASE_TRANSREADY_TIMEOUT 2
#define CASE_DATAACQMODE 3
#define CASE_ARRAYSIZE 4
#define CASE_ALLOCMEM 5
#define CASE_DATAGO 6
#define CASE_SETINDEX 7
#define CASE_CLOSE_PROGRAM 8
#define CASE_RESETGLOBALVARS 9
#define CASE_SAVEDATA 10
#define CASE_SETPACKETSIZE 11
#define CASE_SENDDATAIPC 12
#define CASE_SETCHANNELMASK 13
#define CASE_SHUTDOWNSERVER 17

const int ONE = 1;  // need to have a variable that can be pointed to that always equals '1' for some of the socket options

// global variables to keep track of runtime things
unsigned long g_dataAcqMode, g_trigDelay, g_pollTime;
unsigned long g_recLen, g_packetsize;
unsigned long g_idx1len,g_idx2len,g_idx3len;
unsigned long g_id1,g_id2,g_id3;

struct ENETsock{ // structure to store variables associated with ethernet connection
    int sockfd;                 // file descriptor (fd) of the server socket listening for new connections
    int clifd[MAX_FPGAS];       // array to store the fd's of the connected client socs
    int p_idx[MAX_FPGAS];       // used to keep track of how much data has been recv'd from each soc and update array indices when saving data
    int board[MAX_FPGAS];       // array to store the board number of each connected soc
    const char *serverIP;       // IP address of the server
    struct sockaddr_in server;  // socket variable
};


struct IPCsock{ // structure to store variables for interprocess communication (ipc) socket (for communicating with python)
    int ipcfd;                  // fd of the listening ipc socket in the cServer
    int clifd;                  // fd of the socket after connecting with the user space process (python server)
    struct sockaddr_un local;   // ipc socket variable
    int len;                    // variable to store length of the string in IPCSOCK
};


struct FIFOmsg{ // structure to store variables for communications between cServer and (python) via FIFO instead of ipc socket
    uint32_t msg[4];
    char buff[100]; 
};


void setupIPCsock(struct IPCsock *IPC){ // function to open listening ipc socket for connections from other processes (python server)
    
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


void acceptENETconnection(struct ENETsock *ENET){ // function to accept incoming ethernet connections from the socs
    /*  This function accepts new ethernet connections from the SoC's after the listening socket gets a connection request.
        - After receiving the connection request, the function loops through the array of file descriptors for the connected devices (ENET->clifd) and accepts the new connection into the first empty spot.
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
            ENET->clifd[n] = accept(ENET->sockfd, (struct sockaddr *)&client, &clilen);
            setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(int));
            recv(ENET->clifd[n], &enetmsg, 4*sizeof(int), MSG_WAITALL);
            ENET->board[n] = enetmsg[0];    // stores the recv'd board number in the ENETsock struct
            printf("connected to board %d\n",enetmsg[0]);
            break;
        }
    }
}


void acceptIPCconnection(struct IPCsock *IPC){ // function to accept incoming ipc connections (from python server)
    int t;
    struct sockaddr_un remote;
    t = sizeof(remote);
    IPC->clifd = accept(IPC->ipcfd, (struct sockaddr *)&remote, &t);
    printf("IPC socket accepted %d\n",IPC->clifd);
}


void setupENETserver(struct ENETsock *ENET){ // function to set up ethernet socket to listen for incoming connections
    ENET->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ENET->server.sin_family = AF_INET;
    ENET->server.sin_addr.s_addr = INADDR_ANY;
    ENET->server.sin_port = htons(INIT_PORT);
    setsockopt(ENET->sockfd,SOL_SOCKET, SO_REUSEADDR, &ONE, sizeof(int));

    if( bind(ENET->sockfd, (struct sockaddr *)&ENET->server, sizeof(ENET->server)) < 0 ){
        perror("ERROR binding socket");
        exit(1);
    }

    listen(ENET->sockfd,MAX_FPGAS);
}


void resetGlobalVars(){ // function to reset all global variables, global variables are prepended with 'g_'
    g_dataAcqMode = 0;
    g_recLen = 2048; g_packetsize = 2048; g_trigDelay = 0; g_pollTime = 500;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
}


void removeClient(struct ENETsock *ENET, int n){ // function to remove closed ethernet connections from ENETsock struct
    if( close(ENET->clifd[n]) == 0 ){
        ENET->clifd[n] = 0;
        ENET->board[n] = 0;
    } else {
        ENET->clifd[n] = 0;
        ENET->board[n] = 0;
    }
}


void sendENETmsg(struct ENETsock *ENET, uint32_t *msg, int maxboard){ // function to send messages to socs over ethernet
    /* This function takes as inputs the structure containing the ENET connections, the message to be communicated to the SoCs, and the number of SoCs connected to the server
        
        msg is a 4 element array
        msg[0] contains the 'CASE_...' variable which is an identifier to inform the soc what action to be taken based on the contents of msg[1]-msg[3]
        msg[1] and msg[2] are nothing special, they just contain 32-bit numbers with data for the SoCs (ie recLen or trigDelay)
        msg[3] is special. its a 32-bit number, but should be broken up into two parts and viewed as bits[31:24] bits[23:0]
            if bits[31:24] == 0, the message contained in msg gets sent to all SoCs at once, and bits[23:0] contain a single 24-bit number
            if bits[31:24] > 0, the message contained in message gets sent to one soc at a time, and bits[23:0] get broken up further into three 8-bit numbers
                bits[23:16] and bits[7:0] can contain the data you want to send
                bits[15:8] contain the board number of the SoC to which you want to send data
    */

    int n,bn;
    if( ((msg[3] & 0xff000000) >> 24) == 0 ){ // broadcast to all connected boards
        for(n=0;n<maxboard;n++){
            send(ENET->clifd[n],msg,4*sizeof(uint32_t),MSG_CONFIRM);
            setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
        }
    } else { // send to one board at a time
        bn = (msg[3] & 0x0000ff00) >> 8; 
        for(n=0;n<maxboard;n++){
            if( ENET->board[n] == bn+1 ){
                send(ENET->clifd[n],msg,4*sizeof(uint32_t),MSG_CONFIRM);
                setsockopt(ENET->clifd[n],IPPROTO_TCP, TCP_QUICKACK, &ONE, sizeof(int));
                break;
            }
        }
    }
}


void resetFPGAdataAcqParams(struct ENETsock *ENET, unsigned long maxboard){ // function to reset data acquisition variables on the SoCs to what they are on the cServer
    /* function makes a message variable, populates it with the default data acquisition variables, and sends it to the SoCs*/
    uint32_t fmsg[4] ={0};

    fmsg[0] = CASE_DATAGO; fmsg[1] = 0;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_TRIGDELAY; fmsg[1] = g_trigDelay;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_RECLEN; fmsg[1] = g_recLen;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_TRANSREADY_TIMEOUT; fmsg[1] = g_pollTime;
    sendENETmsg(ENET,fmsg,maxboard);
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
    struct ENETsock ENET = {.clifd={0},.board={0},.p_idx={0}};              // declares and initializes the structure the ethernet socket connection information will be stored in
    setupENETserver(&ENET);                                                 // calls function to setup an ethernet socket to listen for incoming connections from the SoCs

    struct IPCsock IPC = {.clifd=0};                                        // declares and initializes the structure the ipc socket connections will be stored in
    setupIPCsock(&IPC);                                                     // calls function to setup the ipc socket to listen for incoming connections from other processes (python)

    mkfifo( DATA_FIFO, 0666 );                                              // creates the file 'DATA_FIFO' for ipc communication between cServer and (python)
    int fifofd = open( DATA_FIFO, O_RDONLY | O_NONBLOCK );                  // opens the 'DATA_FIFO' in a non-blocking, read only mode
    struct FIFOmsg fmsg;                                                    // creates messaging variable to communicate between the cServer and (python)
    
    int maxfd;                                                              // variable to store the highest value of a connected fd 
    maxfd = ( fifofd > ENET.sockfd ) ? fifofd : ENET.sockfd;                // sets maxfd to be equal to the higher of the two fds
    maxfd = ( maxfd > IPC.ipcfd ) ? maxfd : IPC.ipcfd;                      // sets maxfd to be equal to the higher of the two fds

    resetGlobalVars();                                                      // sets all the global variables to their defualt values
    uint32_t *data;                                                         // array to store acquired data
    uint32_t datatmp[2*8191];                                               // small array to store incoming data from single SoC
    data = (uint32_t *)malloc(2*MAX_FPGAS*g_recLen*sizeof(uint32_t));       // initial memory allocation for 'data'
    unsigned long data_idx;                                                 // index of where to write incoming data in the 'data' array
    int dataMaskWait=0;                                                     // flag to let the cServer know whether a mask is in place during a given acquisition event 

    int n,m,k;                                                              // counters
    unsigned long maxboard,maxipc;                                          // number of SoCs connected over enet, number of ipc socks
    maxboard = 0; maxipc = 0;                                               // initializes the number of connected SoCs and ipc to 0
    int dataAcqGo = 0;                                                      // flag to put SoC in state to acquire data or not
    int nready,nrecv;                                                       // number of fds with incoming data ready, number of bytes recv'd
    fd_set readfds;                                                         // array of fds to poll for incoming/readable data
    struct timeval tv;                                                      // timer variable used for select loop timeout
    
    int runner = 1;                                                         // flag to run the server, closes program if set to 0
    while(runner == 1){
        
        // 'select' function timeout values. need to be reset before each call to 'select'. timeout = tv.tv_sec + tv.tv_usec
        tv.tv_sec = 1;                                                      // units: seconds                                                 
        tv.tv_usec = 0;                                                     // units: microseconds

        // constructs the set of fds the 'select' function should poll for incoming/readable data
        FD_ZERO(&readfds);                                                  // emptys the set
        FD_SET(ENET.sockfd,&readfds);                                       // adds the ethernet socket which listens for new incoming connections
        FD_SET(fifofd,&readfds);                                            // adds the FIFO
        FD_SET(IPC.ipcfd,&readfds);                                         // adds the ipc socket which listens for new ipc connections
        for(n=0;n<maxboard;n++){                                            // loops through all *connected* ethernet sockets and adds them to the set
            FD_SET(ENET.clifd[n],&readfds);
        }
        for(n=0;n<maxipc;n++){                                              // loops through all *connected* ipc sockets and adds them to the set
            FD_SET(IPC.clifd,&readfds);     
        }
        
        // polls the fds in readfds for readable data, returns number of fds with data (nready), returns '0' if it times out.
        nready = select(maxfd+1, &readfds, NULL, NULL, &tv);            

        if( nready > 0 ){

            // FIFO message handler 
            if( FD_ISSET(fifofd, &readfds) ){ // checks if 'fifofd' is one of the ready fds in 'readfds'
                /* The FIFO is used to handle all incoming messages from the user interface (python) 
                    all messages from (python) contain two fields 'msg' and 'buff' ( given in struct FIFOmsg )
                    - 'msg' is a 4-element array of type uint32_t
                    - 'buff' is a character array with 100 elements (primarily/only used to declare the file name when saving data)
                    - msg[0] is a number representing the name of the command given by the 'CASE_...' definitions above
                    - msg[1] and msg[2] are 32-bit numerical arguments/values passed to the given command (eg for msg[0] = CASE_TRIGDELAY, msg[1] = trigDelay)
                    - msg[3] is a 32-bit number, but for commands which are forwarded over ethernet to the SoCs it is broken up into four 8-bit fields
                        - msg[3] -> bits[31:24] - tells cServer to send message to all boards, or one board at a time
                        - msg[3] -> bits[23:16] - data to be sent
                        - msg[3] -> bits[15:8]  - 8-bit number telling which board to send message to
                        - msg[3] -> bits[7:0]   - data to be sent
                    Note: the bit fields in msg[3] accessed through bitwise operations in this code, but can be cast into a 4-element uint8_t array if desired. */

                fmsg.msg[0]=0; fmsg.msg[1]=0; fmsg.msg[2]=0; fmsg.msg[3]=0; // resets all the 'msg' variables 
                nrecv = read(fifofd,&fmsg,sizeof(struct FIFOmsg));          // reads message from FIFO into 'fmsg' 
    
                if ( nrecv < 0 ){ 
                    printf("FIFO read error, shutting down\n");
                    break;  // error condition 'breaks' out of the while loop and shuts down server

                } else if ( nrecv == 0 ){ // connection closed on other end. reopens FIFO
                    close(fifofd);
                    fifofd = open( DATA_FIFO , O_RDONLY | O_NONBLOCK );
                    printf("FIFO closed/opened\n");

                } else {
                    switch(fmsg.msg[0]){ // msg[0] contains the command code for the message
                        /* switch statement notes:
                            - If msg[0] doesn't equal one of the defined CASE_...'s, the server shuts down ('default' case)
                            - All messages which change data acquisition variables write the recv'd values into the corresponding global variable in the cServer
                            - If recv'd values in msg[1]-msg[3] are illegal (ie trigDelay < 0), they are instead set to their default values
                                - If recLen is less than packetsize, packetsize get overwritten as recLen
                            - After all messages that change the size of data to be acquired you need to call CASE_ALLOCMEM to resize the storage variable
                            - All cases need to 'break' out of the 'switch' statement when they're done, else the switch statement will execute the 'default' case at the end and shut down the server
                        */

                        case(CASE_TRIGDELAY):{ // sets trigDelay
                            if(fmsg.msg[1] > 0){
                                g_trigDelay = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                                printf("trigDelay set to: %lu us\n\n",g_trigDelay);
                            } else {
                                g_trigDelay = 0;
                                fmsg.msg[1] = 0;
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                                printf("invalid trigDelay value, defaulting to 0 us\n\n");
                            }
                            break;
                        }

                        case(CASE_RECLEN):{ // set recLen
                            if(fmsg.msg[1] > 0 && fmsg.msg[1] < MAX_RECLEN){
                                g_recLen = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                                printf("recLen set to: %lu\n\n",g_recLen);
                                if(g_recLen < g_packetsize){
                                    g_packetsize = g_recLen;
                                    fmsg.msg[0] = CASE_SETPACKETSIZE;
                                    sendENETmsg(&ENET,fmsg.msg,maxboard);
                                    printf("packetsize set to: %lu\n",g_packetsize);
                                }
                            } else {
                                printf("invalid recLen, reseting global variables\n"); 
                                resetGlobalVars();
                                k = 0;
                                resetFPGAdataAcqParams(&ENET,maxboard);
                                data = (uint32_t *)realloc(data,maxboard*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                                printf("global variables reset to defaults\n");
                            }
                            break;
                        }

                        case(CASE_TRANSREADY_TIMEOUT):{ // change FPGA data timeout
                            /* This changes how often the SoC checks to see whether data is ready to transmit to the cServer.
                                - Lower values make the SoC check for data more often, but put it closer to a busy wait state and burn CPU.
                                - Minimum value allowed is 10us
                            */
                            g_pollTime = fmsg.msg[1];
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            printf("polltime set to: %lu\n\n",g_pollTime);
                            break;
                        }

                        case(CASE_DATAACQMODE):{ // set dataAcq mode
                            /* This does nothing for the time being, but may eventually allow to run using dynamic memory allocation */
                            g_dataAcqMode = fmsg.msg[1];
                            printf("data Acq Mode set to: %lu\n\n",g_dataAcqMode);
                            break;
                        }

                        case(CASE_ARRAYSIZE):{ // set variables needed to allocate memory for data
                            /* data is essentially a 5D array with size data = [g_idx1len, g_idx2len, g_idx3len, recLen, elementN]
                                this sets the size of the first 3 dimensions of the array
                            */
                            g_idx1len = ( 1 > fmsg.msg[1] ) ? 1 : fmsg.msg[1];
                            g_idx2len = ( 1 > fmsg.msg[2] ) ? 1 : fmsg.msg[2];
                            g_idx3len = ( 1 > fmsg.msg[3] ) ? 1 : fmsg.msg[3];
                            printf("Data Array Dimensions set to: [%lu, %lu, %lu, recLen, nElements]\n\n",g_idx1len,g_idx2len,g_idx3len); 
                            break;
                        }

                        case(CASE_ALLOCMEM):{ // allocate/free memory for data and start/end data acquisition on FPGAs
                            if(fmsg.msg[1] == 1){
                                for(n=0;n<maxboard;n++)
                                    m = (ENET.clifd[n] > maxboard) ? ENET.clifd[n] : maxboard;
                                data = (uint32_t *)realloc(data,m*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                                printf("data reallocated to size [%lu, %lu, %lu, %lu (recLen), %lu (nBoards)]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,maxboard);
                            } else {
                                free(data);
                                uint32_t *data;
                                data = (uint32_t *)malloc(MAX_FPGAS*4*g_recLen*sizeof(uint32_t));
                            }
                            break;
                        }

                        case(CASE_DATAGO):{ // tells the SoC whether to be in a data acquisition mode or not
                            if(fmsg.msg[1] == 1){
                                dataAcqGo = fmsg.msg[1];
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                                printf("data acquisition started\n\n");
                            } else {
                                dataAcqGo = 0;
                                fmsg.msg[1] = 0;
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                                printf("data acquisition stopped\n\n");
                            }
                            break;
                        }

                        case(CASE_SETINDEX):{ // declare index to place acquired data
                            /* the location in the data array to store the data must be set prior to every data acquisition event/pulse. this is where you set that.*/ 
                            if(fmsg.msg[1]>=0 && fmsg.msg[2]>=0 && fmsg.msg[3]>=0){
                                g_id1 = fmsg.msg[1];
                                g_id2 = fmsg.msg[2];
                                g_id3 = fmsg.msg[3];
                            } else {
                                g_id1=0;g_id2=0;g_id3=0;
                                printf("indices must be >=0, defualting to [0,0,0]. recvd vals = [%d, %d, %d]\n\n", fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                            }
                            break;
                        }

                        case(CASE_CLOSE_PROGRAM):{ // close the SoC
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            printf("shutting down FPGAs\n");
                            break;
                        }

                        case(CASE_RESETGLOBALVARS):{ // reset global variables
                            resetGlobalVars();
                            k = 0;
                            resetFPGAdataAcqParams(&ENET,maxboard);
                            data = (uint32_t *)realloc(data,maxboard*g_idx1len*g_idx2len*g_idx3len*2*g_recLen*sizeof(uint32_t));
                            printf("global variables reset to defaults\n");
                            printf("data reset to size [%lu, %lu, %lu, %lu (recLen), %lu (nBoards)]\n\n", g_idx1len,g_idx2len,g_idx3len,g_recLen,maxboard);
                            break;
                        }

                        case(CASE_SAVEDATA):{ // save data to binary file
                            FILE *datafile = fopen(fmsg.buff,"wb"); 
                            fwrite(data,sizeof(uint32_t),maxboard*g_idx1len*g_idx2len*g_idx3len*2*g_recLen,datafile);
                            fclose(datafile);
                            printf("data saved to file %s\n\n",fmsg.buff);
                            break;                            
                        }

                        case(CASE_SETPACKETSIZE):{ // sets the size of the packets that the SoCs will transmit over ethernet when they have data
                            /* packetsize must be: 
                                - greater than 0
                                - less than or equal to the record length of data
                                - an even divisor of the record length
                                - highly recommended that this be a power of 2
                            */
                            if( ( fmsg.msg[1]>0 ) && ( fmsg.msg[1]<=g_recLen ) && ( g_recLen%fmsg.msg[1] == 0 ) ){
                                g_packetsize = fmsg.msg[1];
                                printf("packet size set to %lu\n",g_packetsize);
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                            } else {
                                g_packetsize = g_recLen;
                                printf("invalid packet size (%d), set to recLen\n",fmsg.msg[1]);
                                fmsg.msg[1] = g_recLen;
                                sendENETmsg(&ENET,fmsg.msg,maxboard);
                            }
                            break;
                        }

                        case(CASE_SENDDATAIPC):{ // send data array to python server
							if(send(IPC.clifd,data,sizeof(uint32_t)*maxboard*g_idx1len*g_idx2len*g_idx3len*2*g_recLen,MSG_CONFIRM) == -1){
                                perror("IPC send failed\n");
                                exit(1);
                            }
                            break;
                        }

                        case(CASE_SETCHANNELMASK):{ // soc only writes data from unmasked channels into memory, ignores received data from other channels. Allows soc to overwrite channel data in memory before sending it to cServer 
                            /* setchannelmask note:
                                the python dataServer mask function requires confirmation from the cServer/client that the mask was written on the SoC before the program can continue. the confirmation message is
                                a single integer returned from the cServer once all the SoCs have reported back to it that they got the message. however, there is no explicit confirmation mechanism built into the 
                                cServer/SoCs yet that would allow this to happen 'naturally'. so far, this is the only function that requires such a confirmation message so it's not a priority to 'fix' the issue,
                                but it does create a 'problem' in the case of the data mask function. the issue is that setting dataMask equal to zero makes the cServer wait until it receives an array of length 
                                'recLen' from each SoC before it can continue instead of just single integers, which means that if you explicitly set the mask to zero, the program will hang. the client needs to 
                                know the mask is set to zero to function properly, though, once the mask is turned off. the work around is to set the mask = 0 from python, then let the cServer forward that to the 
                                clients. once the message is sent to the SoCs, the cServer will then locally set dataMaskWait equal to 2. This puts the cServer into a state where it can listen for the single integer
                                return value without hanging. the flag '2' in the dataMaskWait state means that the last acquistion was the last one under the mask, and so after the cServer receives messages from
                                the SoCs while in dataMaskWait state '2', it sets dataMaskWait equal to zero locally. in this round about way, dataMaskWait is set to zero everywhere without breaking the normal
                                operation of the cServer.*/
                            dataMaskWait = (fmsg.msg[3] & 0x000000ff);
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            if(dataMaskWait == 0) dataMaskWait = 2; 
                            break;
                        } 

                        case(CASE_SHUTDOWNSERVER):{ // close server
                            fmsg.msg[0]=8;
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            for(n=0;n<maxboard;n++){
                                removeClient(&ENET,n);
                            }
                            maxboard = 0;
                            runner = 0;
                            printf("shutting server down\n\n");
                            break;
                        }

                        default:{ // invalid case, shuts down the cServer
                            printf("invalid message, shutting down server\n");
                            fmsg.msg[0]=8;
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            for(n=0;n<maxboard;n++){
                                removeClient(&ENET,n);
                            }
                            maxboard = 0;
                            break;
                        }
                    }
                }   
            }
        
            // SoC message/data handler 
            for(n=0;n<maxboard;n++){ // loops through all connected SoCs
                if(FD_ISSET(ENET.clifd[n], &readfds)){ // checks whether connected SoC is one of the ready ones in 'readfds'
					
                    // sets the array index where the incoming data will be stored
                    data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
                    data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
                    data_idx += g_id2*g_idx3len*2*g_recLen;
                    data_idx += g_id3*2*g_recLen;
                    
                    nrecv = read(ENET.clifd[n],&data[data_idx+ENET.p_idx[n]],2*g_recLen*sizeof(uint32_t));
                    setsockopt(ENET.clifd[n],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 

                    if (nrecv == 0){
                        removeClient(&ENET,n);
                    } else {
                        if( dataMaskWait == 0 ){
                            ENET.p_idx[n] += nrecv/sizeof(uint32_t); // keeps track of how much data each 'read' brought in to update the location index of where to store the next set of incoming data
                            if(ENET.p_idx[n] == 2*g_recLen){ // after each channel has read in all the data its supposed to for each pulse, p_idx is reset. 'k' is a counter to keep track of how many boards have completed data transfer
                                k++;
                                ENET.p_idx[n] = 0;
                            }
                        } else {
                            k++;
                        }
                        if( k == maxboard ){ // if all data from all SoCs has been collected and transfered to the cServer, let the (python) server know so it can move on with the program
                            if(send(IPC.clifd,&n,sizeof(int),MSG_CONFIRM) == -1){
                                perror("IPC send failed\n");
                                exit(1);
                            }
                            k = 0;
                            setsockopt(ENET.clifd[n],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int)); 
                            if(dataMaskWait == 2) dataMaskWait = 0; // see the note under case(CASE_SETCHANNELMASK)
                        }
                    }
                    
                }
            }
          
            // IPC message handler. note: python shouldn't *send* messages to the cServer through the socket, this only monitors whether the python ipc socket has closed, which it needs to know if it wants to send data through it
            for(n=0;n<maxipc;n++){
                if(FD_ISSET(IPC.clifd, &readfds)){
                    nrecv = recv(IPC.clifd,&fmsg,sizeof(struct FIFOmsg),MSG_WAITALL);
                    if(nrecv == 0){
                        if(IPC.clifd == maxfd){
                            maxfd--;
                        }
                        close(IPC.clifd);
                        maxipc = 0;
					}
                }
            }
    
            // Handles new ethernet connections waiting to be established with the cServer
            if( FD_ISSET(ENET.sockfd, &readfds) ){
                acceptENETconnection(&ENET);
                for(n=0;n<MAX_FPGAS;n++){
                    if(ENET.clifd[n] > 0){
                        maxboard = n+1;
                        maxfd = (maxfd > ENET.clifd[n]) ? maxfd : ENET.clifd[n];
                    }   
                }
            }
            
            // Handles new ipc socket connection waiting to be established with the cServer
            if( FD_ISSET(IPC.ipcfd, &readfds) ){
                acceptIPCconnection(&IPC);
                maxipc = 1;
                maxfd = (maxfd > IPC.clifd) ? maxfd : IPC.clifd;
            }
    
        }
    }
    
    // closes everything and shuts down the cServer.
    printf("out select loop\n");
    free(data);
    close(fifofd);
    close(IPC.ipcfd);
    close(ENET.sockfd);
    for(n=0;n<maxboard;n++)
        close(ENET.clifd[n]);
    printf("successfully exited!\n");
    exit(0);
}
