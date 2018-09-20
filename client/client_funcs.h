

struct ENETsock{
	int sockfd;
	int is_commsock;
	int portNum;
	volatile uint8_t *data_addr;
	int dataLen;
    int is_active;
	struct ENETsock *next;
	struct ENETsock *prev;
    struct ENETsock *commsock;
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


void getBoardData(){ // load the boards specific data from files stored on SoC		
	char const* const fileName = "boardData";
    FILE* file = fopen(fileName, "r");
    char line[256];
	int n=0;
	
    while( fgets(line, sizeof(line), file) && n<4 ){
        g_boardData[n] = atoi(line);
        n++;    
    }  
    fclose(file);
    g_boardNum = g_boardData[0];
}


void addEnetSock(struct ENETsock **ENET, int portNum){
	struct ENETsock* enet;	
	enet = (struct ENETsock *)malloc(sizeof(struct ENETsock));
    enet->next = *ENET;
	enet->prev = NULL;
    enet->sockfd = 0;
	enet->portNum = portNum;
    enet->is_active = 0;
	if(portNum == 0){
		enet->is_commsock = 1;
        enet->commsock = enet;
	} else {
		enet->is_commsock = 0;
	}
	if(*ENET != NULL){
		(*ENET)->prev = enet;
        enet->commsock = (*ENET)->commsock;
    }
	*ENET = enet;
}


void connectEnetSock(struct ENETsock **ENET, int portNum, fd_set *masterfds){
	struct sockaddr_in server_sockaddr;	
	struct timeval t0,t1;
	int diff;
	struct ENETsock *enet, *enet0;

    g_maxfd = 0;
	enet = (*ENET)->commsock;
    while(enet->portNum != portNum){
		g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
		enet = enet->prev;
	}		
	
    enet->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_sockaddr.sin_port = htons(enet->portNum+INIT_PORT);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(g_serverIP);
    
    gettimeofday(&t0,NULL); 
    if(connect(enet->sockfd, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr))  == -1){		
        while(connect(enet->sockfd, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr))  == -1){
            gettimeofday(&t1,NULL);
            diff = (t1.tv_sec-t0.tv_sec);
            if(diff>(600)){
                printf("NO CONNECT!!!!\n");
                break;
            }	
        }
    }  
    setsockopt(enet->sockfd,IPPROTO_TCP,TCP_NODELAY,&ONE,sizeof(int));
    setsockopt(enet->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
	FD_SET(enet->sockfd, masterfds);

    enet->is_active = 1;	
	while(enet != NULL){
        if(enet->is_active){
            enet0 = enet;
		    g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
        }
		enet = enet->prev;
	}
	*ENET = enet0;
	printf("board %d, port %d is active\n", (*ENET)->sockfd, (*ENET)->portNum);
}


void disconnectEnetSock(struct ENETsock **ENET, int portNum, fd_set *masterfds){
    struct ENETsock *enet, *enet0;

    enet0 = (*ENET);
	enet = (*ENET)->commsock;
    while(enet->portNum != portNum){
		enet = enet->prev;
	}		
	
    FD_CLR(enet->sockfd, masterfds);
    close(enet->sockfd);
    enet->sockfd = 0;
    enet->is_active = 0;	
    
    if( enet->sockfd >= g_maxfd ){
        g_maxfd = 0;
        enet = (*ENET)->commsock;
        while( enet != NULL ){
            if( enet->is_active ){
                enet0 = enet;
                g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
            }
            enet = enet->prev;
        }
    }
    *ENET = enet0;
    printf("board %d, port %d was deactivated\n", g_boardNum, (*ENET)->portNum);
}


void setDataAddrPointers(struct ENETsock **ENET, volatile uint8_t *dataAddr0, fd_set *masterfds){
    struct ENETsock *enet;
    int addrRmndr = g_recLen;
    int dataLen;
    int sendbuff;
    enet = (*ENET)->commsock;

    /* check if sockets exist on the desired ports, if not add them */
    while( enet->portNum < g_numPorts ){
        if( enet->prev == NULL ){
            addEnetSock(ENET,enet->portNum+1);
        }
        enet = enet->prev;
    }

    /* if more sockets exist than the number of active ports, disable them */
    while( enet != NULL ){
        if( enet->portNum > g_numPorts ){
            disconnectEnetSock(ENET,enet->portNum,masterfds);
        }
        enet = enet->prev;
    }
    
    enet = (*ENET)->commsock->prev;
    /*  go through the list of active sockets, connect them if they aren't already.
        set pointers to appropriate addresses in data array where each socket should sent data from.
        set the size of the data the socket is responsible for sending to cServer. */
    while( addrRmndr > 0 ){
        if( !(enet->is_active) )
            connectEnetSock(ENET,enet->portNum,masterfds);
        dataLen = ( g_packetsize < addrRmndr ) ? g_packetsize : addrRmndr;
        enet->data_addr = dataAddr0+(enet->portNum-1)*8*dataLen;
        enet->dataLen = dataLen;
        sendbuff = enet->dataLen*sizeof(uint64_t);
		setsockopt(enet->sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff)); 
        addrRmndr -= dataLen;
        enet = enet->prev;
    }
    
    while(enet != NULL){
		disconnectEnetSock(ENET,enet->portNum,masterfds);
        enet = enet->prev;
	}
}


void FPGA_dataAcqController(struct FPGAvars *FPGA, struct ENETsock **ENET, fd_set *masterfds){ // process that talks to the FPGA and transmits data to the SoCs
	struct ENETsock *enet0, *commsock;
	commsock = (*ENET)->commsock;
	
	//~ enet0 = (*ENET);
	//~ while(enet0 != NULL){
		//~ printf("enet0 %d, %d, %d, %d\n", enet0->sockfd, enet0->is_commsock, enet0->portNum, enet0->is_active);
		//~ enet0 = enet0->next;
	//~ }
	int portNum=0;
	static int tmp;
    int nsent;	
	//~ printf("enetmsg %u, %u, %u, %u\n",enetmsg[0],enetmsg[1],enetmsg[2],enetmsg[3]);	
	switch(enetmsg[0]){
		
		case(CASE_TRIGDELAY):{ // change trig delay
			DREF(FPGA->trigDelay) = 20*enetmsg[1];
			printf("trig Delay set to %.2f\n",(float)DREF(FPGA->trigDelay)/20.0);
			break;
		}

		case(CASE_RECLEN):{ // change record length
            DREF(FPGA->transReady) = 0;
			if( enetmsg[1]>=MIN_PACKETSIZE && enetmsg[1]<=MAX_RECLEN ){
				DREF(FPGA->recLen) = enetmsg[1];
				g_recLen = enetmsg[1];
				if( enetmsg[2]>=MIN_PACKETSIZE && enetmsg[2]<=enetmsg[1] ){
					g_packetsize = enetmsg[2];
				} else {
					g_packetsize = g_recLen;
				}
				printf("[ recLen, packetsize ] set to [ %zu, %zu ]\n",DREF(FPGA->recLen), g_packetsize);
			} else {
				DREF(FPGA->recLen) = 2048;
				g_recLen = DREF(FPGA->recLen);
                g_packetsize = 2048;
				printf("invalid recLen, defaulting to 2048, packetsize to 512\n");
			}
            g_numPorts = (g_recLen-1)/g_packetsize + 1;
            setDataAddrPointers(ENET, FPGA->onchip1, masterfds);
			break;
		}

        case(CASE_SET_INTERLEAVE_DEPTH_AND_TIMER):{
			if( enetmsg[1] > 0 ){
                g_moduloBoardNum = enetmsg[1];
				if( enetmsg[2] < 5000 ){
					g_moduloTimer = enetmsg[2];
				} else {
					g_moduloTimer = 0;
				}
				if( enetmsg[3] < 5000 ){
					g_packetWait = enetmsg[3];
				} else {
					g_packetWait = 0;
				}
			} else {
                g_moduloBoardNum = 1;
                g_moduloTimer = 0;
                g_packetWait = 0;
            }
            break;
        }

		case(CASE_DATAGO):{ // if dataGo is zero it won't transmit data to server, basically a wait state
            g_dataAcqGo = ( enetmsg[1] == 1 || enetmsg[1] == 0 ) ? enetmsg[1] : 0;
            if( g_dataAcqGo )
                setDataAddrPointers(ENET, FPGA->onchip1, masterfds);
			DREF(FPGA->stateReset) = 1; 
			DREF(FPGA->read_addr) = 0;
			DREF(FPGA->stateReset) = 0;
			break;
		}

		case(CASE_QUERY_BOARD_INFO):{// loads board-specific data from onboard file
			getBoardData();
			send(commsock->sockfd,g_boardData,4*sizeof(uint32_t),0);
            setsockopt(commsock->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
            break;
		}
		
		case(CASE_SET_QUERY_DATA_TIMEOUT):{
			if(enetmsg[1] > 99){
				g_queryTimeout = enetmsg[1];
			} else {
				g_queryTimeout = 1000;
			}
			break;
		}
		
		case(CASE_QUERY_DATA):{
			if(enetmsg[1] == 0 || enetmsg[2] == 0){
				usleep((g_boardNum%g_moduloBoardNum)*g_moduloTimer);
				tmp = 0;
			}
			while( !DREF(FPGA->transReady) && ++tmp<g_queryTimeout ){
				usleep(10);
			}
			if( DREF(FPGA->transReady) ){
				if(enetmsg[1] == 0){
					enet0 = commsock->prev;
					while( enet0 != NULL && enet0->is_active ){
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_CORK,&ONE,sizeof(int));
						nsent = send(enet0->sockfd,DREFP(enet0->data_addr),enet0->dataLen*8*sizeof(uint8_t),0);	
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_CORK,&ZERO,sizeof(int));
						if( nsent < 0 )
							perror("error sending data:");
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
						enet0 = enet0->prev;
						usleep(g_packetWait);
					}
					portNum = 0;
				} else {
					portNum = enetmsg[2]+1;
					if(portNum <= g_numPorts){
						enet0 = commsock->prev;
						while( enet0->portNum != portNum )
							enet0 = enet0->prev;
						
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_CORK,&ONE,sizeof(int));
						nsent = send(enet0->sockfd,DREFP(enet0->data_addr),enet0->dataLen*8*sizeof(uint8_t),0);	
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_CORK,&ZERO,sizeof(int));
						if( nsent < 0 )
							perror("error sending data:");
						setsockopt(enet0->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
					}
					if(portNum == g_numPorts){
						portNum = 0;
					}
				}
				
			} else {
                emsg[0] = CASE_QUERY_DATA; emsg[1] = g_boardNum; emsg[2] = MSG_QUERY_TIMEOUT;
                //send(commsock->sockfd,emsg,4*sizeof(uint32_t),0);
                printf("query data timed out (10ms)\n");
            }
			if(portNum == 0){
				DREF(FPGA->stateReset) = 1; 
				usleep(10);
				DREF(FPGA->read_addr) = g_recLen;
				DREF(FPGA->read_addr) = 0;
				DREF(FPGA->stateReset) = 0;
				tmp = 0;
			}
			//~ printf("trans ready = %lu\n",(unsigned long) DREF(FPGA->transReady));
			break;	
		}
	
		default:{
			printf("default case, doing nothing\n");
            break;
		}
	}
}














