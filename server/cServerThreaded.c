
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
//#include <arpa/inet.h>
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
#define CASE_IMGMODE 13
#define CASE_SHUTDOWNSERVER 17

unsigned long g_dataAcqMode, g_trigDelay, g_pollTime, g_cnt;
unsigned long g_imgMode, g_elIdx;
unsigned long g_recLen, g_packetsize;
unsigned long g_idx1len,g_idx2len,g_idx3len;
unsigned long g_id1,g_id2,g_id3;


struct ENETsock{
    int sockfd;
    int clifd[MAX_FPGAS];
    int p_idx[MAX_FPGAS];
    int board[MAX_FPGAS];
    const char *serverIP;
    struct sockaddr_in server;
};


struct IPCsock{
    int ipcfd;
    int clifd;
    struct sockaddr_un local;
    int len; 
};


struct FIFOmsg{
    int msg[4];
    char buff[100]; 
};


void setupIPCsock(struct IPCsock *IPC){
    
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


void acceptENETconnection(struct ENETsock *ENET){
    int tmpfd,clilen;
    int enetmsg[4] = {0};
    struct sockaddr_in client;
    int n;
    int one = 1;
    clilen = sizeof(client);

    tmpfd = accept(ENET->sockfd, (struct sockaddr *)&client, &clilen);
    //setsockopt(tmpfd,IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int));
    recv(tmpfd, &enetmsg, 4*sizeof(int), MSG_WAITALL);
    
    for(n=0;n<MAX_FPGAS;n++){
        if(ENET->clifd[n] == 0){
            ENET->clifd[n] = tmpfd;
            ENET->board[n] = enetmsg[0];
            printf("connected to board %d\n",enetmsg[0]);
            break;
        }
    }
}


void acceptIPCconnection(struct IPCsock *IPC){
    int t;
    struct sockaddr_un remote;
    t = sizeof(remote);
    IPC->clifd = accept(IPC->ipcfd, (struct sockaddr *)&remote, &t);
    printf("IPC socket accepted %d\n",IPC->clifd);
}


void setupENETserver(struct ENETsock *ENET){
    int one = 1;
    ENET->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ENET->server.sin_family = AF_INET;
    ENET->server.sin_addr.s_addr = INADDR_ANY;
    ENET->server.sin_port = htons(INIT_PORT);

    if( bind(ENET->sockfd, (struct sockaddr *)&ENET->server, sizeof(ENET->server)) < 0 ){
        perror("ERROR binding socket");
        exit(1);
    }

    //setsockopt(ENET->sockfd,IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int));
    listen(ENET->sockfd,MAX_FPGAS);
}


void resetGlobalVars(){
    g_dataAcqMode = 0; g_cnt = 0; 
    g_imgMode = 0; g_elIdx = 0;
    g_recLen = 2048; g_packetsize = 2048; g_trigDelay = 0; g_pollTime = 500;
    g_idx1len = 1; g_idx2len = 1; g_idx3len = 1;
    g_id1 = 0; g_id2 = 0; g_id3 = 0;
}


void removeClient(struct ENETsock *ENET, int n){
    if( close(ENET->clifd[n]) == 0 ){
        ENET->clifd[n] = 0;
        ENET->board[n] = 0;
    } else {
        ENET->clifd[n] = 0;
        ENET->board[n] = 0;
    }
}


void sendENETmsg(struct ENETsock *ENET, int *msg, int maxboard){
    int n;
    for(n=0;n<maxboard;n++)
        send(ENET->clifd[n],msg,4*sizeof(int),MSG_CONFIRM);
}


void resetFPGAdataAcqParams(struct ENETsock *ENET, unsigned long maxboard){
    int fmsg[4] ={0};

    fmsg[0] = CASE_DATAGO; fmsg[1] = 0;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_TRIGDELAY; fmsg[1] = g_trigDelay;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_RECLEN; fmsg[1] = g_recLen;
    sendENETmsg(ENET,fmsg,maxboard);
    
    fmsg[0] = CASE_TRANSREADY_TIMEOUT; fmsg[1] = g_pollTime;
    sendENETmsg(ENET,fmsg,maxboard);
}


struct threadData{	
	struct ENETsock *esock;
	struct IPCsock *isock;
	size_t dataLen;
	uint32_t *data;
	int n;
	int *k;
	int maxboard;
};


void *threadReceiver(void *dtmp){
	
	nrecv = read(dtmp.ENET.clifd[dtmp.n],dtmp.data,dtmp.dataLen);
                    
	if (nrecv == 0){
		removeClient(&dtmp.ENET,n);
	} else {
		dtmp.ENET.p_idx[dtmp.n] += nrecv/sizeof(uint32_t);
		if(dtmp.ENET.p_idx[dtmp.n] == dtmp.dataLen/sizeof(uint32_t)){
			dtmp.k++;
			dtmp.ENET.p_idx[n] = 0;
		}
		if(dtmp.k==dtmp.maxboard){
			if(send(IPC.clifd,&n,sizeof(int),MSG_CONFIRM) == -1){
				perror("IPC send failed\n");
				exit(1);
			}
			k = 0;
		}
	}
	
	
}


int main(int argc, char *argv[]) { printf("into main!\n");
	
	pthread_t tid[MAX_FPGAS];
	struct threadData THREADS[MAX_FPGAS];
	
    struct ENETsock ENET = {.clifd={0},.board={0},.p_idx={0}};
    setupENETserver(&ENET);

    struct IPCsock IPC = {.clifd=0};
    setupIPCsock(&IPC); 

    int fifofd;
    struct FIFOmsg fmsg;
    mkfifo( DATA_FIFO, 0666 );
    fifofd = open( DATA_FIFO, O_RDONLY | O_NONBLOCK );
    
    int maxfd;
    maxfd = ( fifofd > ENET.sockfd ) ? fifofd : ENET.sockfd;
    maxfd = ( maxfd > IPC.ipcfd ) ? maxfd : IPC.ipcfd;    

    resetGlobalVars();
    uint32_t *data;
    uint32_t datatmp[2*8191];
    data = (uint32_t *)malloc(2*MAX_FPGAS*g_recLen*sizeof(uint32_t));
    unsigned long data_idx;

    int n,m,k;
    unsigned long maxboard,maxipc;
    maxboard = 0; maxipc = 0;
    int dataAcqGo = 0; 
    int nready,nrecv; 
    fd_set readfds;
    struct timeval tv;
    
    int runner = 1;
    while(runner == 1){
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(ENET.sockfd,&readfds);
        FD_SET(fifofd,&readfds);
        FD_SET(IPC.ipcfd,&readfds);
        for(n=0;n<maxboard;n++){
            FD_SET(ENET.clifd[n],&readfds);
        }
        for(n=0;n<maxipc;n++){
            FD_SET(IPC.clifd,&readfds);
        }
        
        nready = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if( nready > 0 ){

            if( FD_ISSET(fifofd, &readfds) ){
                fmsg.msg[0]=0; fmsg.msg[1]=0; fmsg.msg[2]=0; fmsg.msg[3]=0;
                nrecv = read(fifofd,&fmsg,sizeof(struct FIFOmsg));
                if ( nrecv < 0 ){
                    printf("FIFO read error, shutting down\n");
                    break;
                } else if ( nrecv == 0 ){
                    close(fifofd);
                    fifofd = open( DATA_FIFO , O_RDONLY | O_NONBLOCK );
                } else {
                    switch(fmsg.msg[0]){
                        case(CASE_TRIGDELAY):{ // set trigDelay
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
                            g_pollTime = fmsg.msg[1];
                            sendENETmsg(&ENET,fmsg.msg,maxboard);
                            printf("polltime set to: %lu\n\n",g_pollTime);
                            break;
                        }
                        case(CASE_DATAACQMODE):{ // set dataAcq mode
                            g_dataAcqMode = fmsg.msg[1];
                            printf("data Acq Mode set to: %lu\n\n",g_dataAcqMode);
                            break;
                        }
                        case(CASE_ARRAYSIZE):{ // set variables needed to allocate memory for data
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
                            //sendENETmsg(&ENET,fmsg.msg,maxboard);
                            break;
                        }
                        case(CASE_DATAGO):{ // start/stop acquiring data
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
                            if(fmsg.msg[1]>=0 && fmsg.msg[2]>=0 && fmsg.msg[3]>=0){
                                g_id1 = fmsg.msg[1];
                                g_id2 = fmsg.msg[2];
                                g_id3 = fmsg.msg[3];
                                //printf("data index set to [%lu, %lu, %lu]\n\n", g_id1, g_id2, g_id3);
                            } else {
                                g_id1=0;g_id2=0;g_id3=0;
                                printf("indices must be >=0, defualting to [0,0,0]. recvd vals = [%d, %d, %d]\n\n", fmsg.msg[1],fmsg.msg[2],fmsg.msg[3]);
                            }
                            break;
                        }
                        case(CASE_CLOSE_PROGRAM):{ // close software on FPGA
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
                        case(CASE_SAVEDATA):{ // save data binary
                            FILE *datafile = fopen(fmsg.buff,"wb"); 
                            fwrite(data,sizeof(uint32_t),maxboard*g_idx1len*g_idx2len*g_idx3len*2*g_recLen,datafile);
                            fclose(datafile);
                            printf("data saved to file %s\n\n",fmsg.buff);
                            break;
                            
                        }
                        case(CASE_SETPACKETSIZE):{
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
                        case(CASE_IMGMODE):{
                            if(fmsg.msg[1] == 1 && fmsg.msg[2] >= 0 && fmsg.msg[2] < 8){
                                g_imgMode = 1;
                                g_elIdx = fmsg.msg[2];
                            } else {
                                g_imgMode = 0;
                                g_elIdx = 0;
                            }
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
                        default:{
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
        
    
            for(n=0;n<maxboard;n++){
                if(FD_ISSET(ENET.clifd[n], &readfds)){
					
                    if(g_dataAcqMode == 0){
                        data_idx = (ENET.board[n]-1)*g_idx1len*g_idx2len*g_idx3len*2*g_recLen;
                        data_idx += g_id1*g_idx2len*g_idx3len*2*g_recLen;
                        data_idx += g_id2*g_idx3len*2*g_recLen;
                        data_idx += g_id3*2*g_recLen;
                    }
                    
                    THREADS[n].esock = &ENET;
                    THREADS[n].isock = &IPC;
                    THREADS[n].data = &data[data_idx+ENET.p_idx[n]];
                    THREADS[n].dataLen = 2*g_recLen*sizeof(uint32_t);
                    
                    pthread_create(&tid[n], NULL, threadReceiver, (void *) &THREADS[N]);
                    //~ nrecv = read(ENET.clifd[n],&data[data_idx+ENET.p_idx[n]],2*g_recLen*sizeof(uint32_t));
                    
                    //~ if (nrecv == 0){
                        //~ removeClient(&ENET,n);
                    //~ } else {
                        //~ ENET.p_idx[n] += nrecv/sizeof(uint32_t);
                        //~ if(ENET.p_idx[n] == 2*g_recLen){
                            //~ k++;
                            //~ ENET.p_idx[n] = 0;
                        //~ }
                        //~ if(k==maxboard){
                            //~ if(send(IPC.clifd,&n,sizeof(int),MSG_CONFIRM) == -1){
                                //~ perror("IPC send failed\n");
                                //~ exit(1);
                            //~ }
                            //~ k = 0;
                        //~ }
                    //~ }
                    
                }
            }
           
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
     
            if( FD_ISSET(ENET.sockfd, &readfds) ){
                acceptENETconnection(&ENET);
                for(n=0;n<MAX_FPGAS;n++){
                    if(ENET.clifd[n] > 0){
                        maxboard = n+1;
                        maxfd = (maxfd > ENET.clifd[n]) ? maxfd : ENET.clifd[n];
                    }   
                }
            }
 
            if( FD_ISSET(IPC.ipcfd, &readfds) ){
                acceptIPCconnection(&IPC);
                maxipc = 1;
                maxfd = (maxfd > IPC.clifd) ? maxfd : IPC.clifd;
            }
        }
    }
    printf("out select loop\n");
    free(data);
    close(fifofd);
    close(IPC.ipcfd);
    close(ENET.sockfd);
    for(n=0;n<maxboard;n++)
        close(ENET.clifd[n]);
    printf("successfully exited!\n");

}