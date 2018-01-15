

struct ENETsock{ // structure to store ethernet variables
    const char *serverIP;
    const int boardNum;
    int commfd;
    int sockfd[MAX_DATA_PORTS];
    int maxfd;
	const char *server_addr;
	struct sockaddr_in server_sockaddr;
};


struct FPGAvars{ // structure to hold variables that are mapped to the FPGA hardware registers
	void *virtual_base;
	void *axi_virtual_base;
	int fd;
	uint32_t volatile* read_addr;
	uint32_t volatile* gpio0_addr;
	uint32_t volatile* gpio1_addr;
	uint32_t volatile* transReady;
	uint32_t volatile* trigDelay;
	uint32_t volatile* recLen;
	uint32_t volatile* stateReset;
	uint32_t volatile* trigCnt;
	uint32_t volatile* stateVal;
	
	uint32_t volatile* onchip0;
	uint8_t volatile* onchip1;
};


void FPGAclose(struct FPGAvars *FPGA){ // closes the memory mapped file with the FPGA hardware registers
	
	if( munmap( FPGA->virtual_base, HW_REGS_SPAN ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( FPGA->fd );
	}
	if( munmap( FPGA->axi_virtual_base, HW_FPGA_AXI_SPAN ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( FPGA->fd );
	}

	close( FPGA->fd );
}


int FPGA_init(struct FPGAvars *FPGA){ // maps the FPGA hardware registers to the variables in the FPGAvars struct
	
	if( ( FPGA->fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
		printf( "ERROR: could not open \"/dev/mem\"...\n" );
		return( 0 );
	}
	
	FPGA->virtual_base = mmap( NULL, HW_REGS_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, FPGA->fd, HW_REGS_BASE );
	
	if( FPGA->virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap() failed...\n" );
		close( FPGA->fd );
		return( 0 );
	}
	
	FPGA->axi_virtual_base = mmap( NULL, HW_FPGA_AXI_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, FPGA->fd,ALT_AXI_FPGASLVS_OFST );

	if( FPGA->axi_virtual_base == MAP_FAILED ) {
		printf( "ERROR: axi mmap() failed...\n" );
		close( FPGA->fd );
		return( 0 );
	}
	

	FPGA->read_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_READ_ADDR_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->gpio0_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_GPIO0_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->gpio1_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_GPIO1_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->transReady = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_TRANSMIT_READY_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->trigDelay = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_TRIG_DELAY_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->trigCnt = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_TRIG_COUNT_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->recLen = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_RECORD_LENGTH_BASE ) & ( uint32_t )( HW_REGS_MASK ) );

	FPGA->stateReset = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_FPGA_STATE_RESET_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->stateVal = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_FPGA_STATE_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->onchip0 = FPGA->axi_virtual_base + ( ( uint32_t  )( ONCHIP_MEMORY2_0_BASE ) & ( uint32_t)( HW_FPGA_AXI_MASK ) );

	FPGA->onchip1 = FPGA->axi_virtual_base + ( ( uint32_t  )( ONCHIP_MEMORY2_1_BASE ) & ( uint32_t)( HW_FPGA_AXI_MASK ) );
	
	DREF(FPGA->transReady) = 0;
	DREF(FPGA->trigDelay) = 0;
	DREF(FPGA->trigCnt) = 0;
	DREF(FPGA->recLen) = 2048;
	
	DREF(FPGA->stateReset)=1;
	usleep(10);
	DREF(FPGA->stateReset)=0;
	DREF(FPGA->read_addr) = 0;
	return(1);
}


void getBoardData(int argc, char *argv[], int *boardData){ // load the boards specific data from files stored on SoC
		
	char const* const fileName = "boardData";
    FILE* file = fopen(fileName, "r");
    char line[256];
	int n=0;
	
    while(fgets(line, sizeof(line), file)){
        boardData[n] = atoi(line);
        n++;    
    }  
    fclose(file);
}


void setupCOMMsock(struct ENETsock *ENET){ // connect to the cServer through ethernet
	struct timeval t0,t1;
	int diff;
    
    ENET->server_addr = ENET->serverIP;	
    ENET->commfd = socket(AF_INET, SOCK_STREAM, 0);
    ENET->server_sockaddr.sin_port = htons(INIT_PORT);
    ENET->server_sockaddr.sin_family = AF_INET;
    ENET->server_sockaddr.sin_addr.s_addr = inet_addr(ENET->server_addr);
    
    gettimeofday(&t0,NULL);
    
    if(connect(ENET->commfd, (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){		
        while(connect(ENET->commfd, (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){
            gettimeofday(&t1,NULL);
            diff = (t1.tv_sec-t0.tv_sec);
            if(diff>(600)){
                printf("NO CONNECT!!!!\n");
                break;
            }	
        }
    }
    
    setsockopt(ENET->commfd,IPPROTO_TCP,TCP_NODELAY,&ONE,sizeof(int));
    int tmpmsg[4] = {0};
    tmpmsg[0] = ENET->boardNum;
    tmpmsg[1] = 0;
    send(ENET->commfd, &tmpmsg, 4*sizeof(int), 0);
    setsockopt(ENET->commfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
    
    ENET->maxfd = ENET->commfd;
    for(tmpmsg[0]=0;ENET->sockfd[tmpmsg[0]]!=0;tmpmsg[0]++){
        ENET->maxfd = (ENET->maxfd > ENET->sockfd[tmpmsg[0]]) ? ENET->maxfd : ENET->sockfd[tmpmsg[0]];
    }
    
}


void setupDATAsock(struct ENETsock *ENET, int portNum){ // connect to the cServer through ethernet
	struct timeval t0,t1;
	int diff;
    ENET->server_addr = ENET->serverIP;	
    ENET->sockfd[portNum-1] = socket(AF_INET, SOCK_STREAM, 0);
    ENET->server_sockaddr.sin_port = htons(INIT_PORT+portNum);
    ENET->server_sockaddr.sin_family = AF_INET;
    ENET->server_sockaddr.sin_addr.s_addr = inet_addr(ENET->server_addr);
    
    gettimeofday(&t0,NULL);
    
    if(connect(ENET->sockfd[portNum-1], (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){		
        while(connect(ENET->sockfd[portNum-1], (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){
            gettimeofday(&t1,NULL);
            diff = (t1.tv_sec-t0.tv_sec);
            if(diff>(600)){
                printf("NO CONNECT!!!!\n");
                break;
            }	
        }
    }
    
    setsockopt(ENET->sockfd[portNum-1],IPPROTO_TCP,TCP_NODELAY,&ONE,sizeof(int));
    int tmpmsg[4] = {0};
    tmpmsg[0] = ENET->boardNum;
    tmpmsg[1] = portNum;
    send(ENET->sockfd[portNum-1], &tmpmsg, 4*sizeof(int), 0);
    setsockopt(ENET->sockfd[portNum-1],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
    
    ENET->maxfd = ENET->commfd;
    for(tmpmsg[0]=0;ENET->sockfd[tmpmsg[0]]!=0;tmpmsg[0]++){
        ENET->maxfd = (ENET->maxfd > ENET->sockfd[tmpmsg[0]]) ? ENET->maxfd : ENET->sockfd[tmpmsg[0]];
    }
    
}


void FPGA_dataAcqController(uint32_t *pipemsg, struct FPGAvars *FPGA, struct ENETsock *ENET){ // process that talks to the FPGA and transmits data to the SoCs

	uint8_t *dtmp;

	int tmp = 0;
	int n;
    int nsent;	
				
	switch(pipemsg[0]){

		case(CASE_TRIGDELAY):{ // change trig delay
			DREF(FPGA->trigDelay) = 20*pipemsg[1];
			printf("trig Delay set to %.2f\n",(float)DREF(FPGA->trigDelay)/20.0);
			break;
		}

		case(CASE_RECLEN):{ // change record length
			DREF(FPGA->transReady) = 0;
			if(pipemsg[1]>=MIN_PACKETSIZE && pipemsg[1]<=MAX_RECLEN){
				DREF(FPGA->recLen) = ( pipemsg[1] == MAX_RECLEN ) ? pipemsg[1]-1 : pipemsg[1];
				g_recLen = pipemsg[1];
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("recLen set to %zu\n",DREF(FPGA->recLen));
			} else {
				DREF(FPGA->recLen) = 2048;
				g_recLen = DREF(FPGA->recLen);
                g_packetsize = 512;
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("invalid recLen, defaulting to 2048\n");
			}
            for(n=0;n<MAX_DATA_PORTS;n++){
                if(n<g_nDataPorts && ENET->sockfd[n] == 0){
                    setupDATAsock(ENET,n+1);
                    g_updateFDSet = 1;
                } else if (n>=g_nDataPorts && ENET->sockfd[n]!=0 ){
                    close(ENET->sockfd[n]);
                    ENET->sockfd[n] = 0;
                    g_updateFDSet = 1;
                }
            }
			break;
		}

        case(CASE_SET_PACKETSIZE):{
			DREF(FPGA->transReady) = 0;
			if( pipemsg[1] >= MIN_PACKETSIZE && pipemsg[1] <= g_recLen ){
                g_packetsize = pipemsg[1];
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("packetsize set to %u\n",g_packetsize);
			} else {
                g_packetsize = g_recLen;
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
            }
            for(n=0;n<MAX_DATA_PORTS;n++){
                if(n<g_nDataPorts && ENET->sockfd[n] == 0){
                    setupDATAsock(ENET,n+1);
                    g_updateFDSet = 1;
                } else if (n>=g_nDataPorts && ENET->sockfd[n]!=0 ){
                    close(ENET->sockfd[n]);
                    ENET->sockfd[n] = 0;
                    g_updateFDSet = 1;
                }
            }
            break;
        }

		case(CASE_CLOSE_PROGRAM):{ // close process fork
			printf("closing program\n");
			FPGAclose(FPGA);
			break;
		}

		case(CASE_DATAGO):{ // if dataGo is zero it won't transmit data to server, basically a wait state
			if(pipemsg[1] == 1){
				printf("dataAcqGo set to 1\n");
			} else {
				printf("dataAcqGo set to 0\n");
			}
			DREF(FPGA->stateReset) = 1; 
			DREF(FPGA->read_addr) = 0;
			DREF(FPGA->stateReset) = 0;
			break;
		}

		case(CASE_QUERY_DATA):{
			while( !DREF(FPGA->transReady) && ++tmp<1000 ){
				usleep(10);
			}
			
			if( DREF(FPGA->transReady) ){
				dtmp = DREFP(FPGA->onchip1);
                nsent = 0;
				for(n=0;ENET->sockfd[n]!=0;n++){
					if( ( n == (g_nDataPorts-1) ) && ( g_recLen%g_packetsize > 0 ) ){
						nsent = send(ENET->sockfd[n],&dtmp[8*n*g_packetsize],(g_recLen%g_packetsize)*8*sizeof(uint8_t),0);
                        //printf("sending data mod,%d,%d\n",nsent,ENET->sockfd[n]);
					} else {
                        nsent = send(ENET->sockfd[n],&dtmp[8*n*g_packetsize],g_packetsize*8*sizeof(uint8_t),0);
                        //printf("sending data no mod,%d,%d\n",nsent,ENET->sockfd[n]);
					}
                    if ( nsent == -1 ) perror("error sending data:");
					setsockopt(ENET->sockfd[n],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
				}
			}

			DREF(FPGA->stateReset) = 1; 
			usleep(10);
			DREF(FPGA->read_addr) = g_recLen;
			DREF(FPGA->read_addr) = 0;
			DREF(FPGA->stateReset) = 0;
			tmp = 0;
			break;	
		}
	
		default:{
			printf("default case, shutting down\n");
			FPGAclose(FPGA);
            close(ENET->commfd);
            for(n=0;ENET->sockfd[n]!=0;n++){
                close(ENET->sockfd[n]);
            }
			break;
		}
	}
}














