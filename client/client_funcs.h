

//~ struct ENETsock{ // structure to store ethernet variables
    //~ const char *serverIP;
    //~ const int boardNum;
    //~ int commfd;
    //~ int sockfd[MAX_DATA_PORTS];
    //~ int maxfd;
	//~ const char *server_addr;
	//~ struct sockaddr_in server_sockaddr;
//~ };

struct ENETsock{
	int sockfd;
	int is_commsock;
	int portNum;
	uint8_t volatile *data_addr;
	int dataLen;
	struct ENETsock *next;
	struct ENETsock *prev;	
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
        emsg[n] = atoi(line);
        n++;    
    }  
    fclose(file);
    g_boardNum = emsg[0];
}


void addEnetSock(struct ENETsock **ENET, int portNum){
	struct ENETsock* enet;	
	enet = (struct ENETsock *)malloc(sizeof(struct ENETsock));
	enet->next = *ENET;
	enet->prev = NULL;
	enet->portNum = portNum;
	if(portNum == 0){
		enet->is_commsock = 1;
	} else {
		enet->is_commsock = 0;
	}
	if(*ENET != NULL)
		(*ENET)->prev = enet;
	*ENET = enet;
}


void connectEnetSock(struct ENETsock **ENET, int portNum, fd_set *masterfds){
	struct sockaddr_in server_sockaddr;	
	struct timeval t0,t1;
	int diff;
	struct ENETsock *enet, *enet0;
	g_maxfd = 0;
	enet0 = *ENET;
	enet = *ENET;
	while(enet->prev != NULL){
		enet = enet->prev;
	}
	while(enet->portNum != portNum){
		g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
		enet = enet->next;
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
	
	while(enet != NULL){
		g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
		enet = enet->next;
	}
	*ENET = enet0;
}


void deleteEnetSock(struct ENETsock **ENET, int portNum, fd_set *masterfds){
	
	struct ENETsock *enet, *next, *prev;
	
	g_maxfd = 0;
	enet = *ENET;
	while(enet->prev != NULL){
		enet = enet->prev;
	}
	while(enet->portNum != portNum){
		g_maxfd = (g_maxfd > enet->sockfd) ? g_maxfd : enet->sockfd;
		enet = enet->next;
	}
	next = enet->next;
	prev = enet->prev;
	if(next != NULL)
		next->prev = prev;
	if(prev != NULL)
		prev->next = next;
	if(prev == NULL)
		(*ENET) = next;
	
	FD_CLR(enet->sockfd, masterfds);
	free(enet);
	
	while(next != NULL){
		g_maxfd = (g_maxfd > next->sockfd) ? g_maxfd : next->sockfd;
		next = next->next;
	}
}


void FPGA_dataAcqController(struct FPGAvars *FPGA, struct ENETsock **ENET, struct ENETsock *commsock){ // process that talks to the FPGA and transmits data to the SoCs

	struct ENETsock *enet0, *enet2;
	enet0 = *ENET;
	
	//~ uint8_t *dtmp;
	int tmp = 0;
	int n;
    int nsent,ntot;	
				
	switch(enetmsg[0]){

		case(CASE_TRIGDELAY):{ // change trig delay
			DREF(FPGA->trigDelay) = 20*enetmsg[1];
			printf("trig Delay set to %.2f\n",(float)DREF(FPGA->trigDelay)/20.0);
			break;
		}

		case(CASE_RECLEN):{ // change record length
			DREF(FPGA->transReady) = 0;
			if(enetmsg[1]>=MIN_PACKETSIZE && enetmsg[1]<=MAX_RECLEN){
				DREF(FPGA->recLen) = ( enetmsg[1] == MAX_RECLEN ) ? enetmsg[1]-1 : enetmsg[1];
				g_recLen = enetmsg[1];
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("recLen set to %zu\n",DREF(FPGA->recLen));
			} else {
				DREF(FPGA->recLen) = 2048;
				g_recLen = DREF(FPGA->recLen);
                g_packetsize = 512;
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("invalid recLen, defaulting to 2048\n");
			}
			break;
		}

        case(CASE_SET_PACKETSIZE):{
			DREF(FPGA->transReady) = 0;
			if( enetmsg[1] >= MIN_PACKETSIZE && enetmsg[1] <= g_recLen ){
                g_packetsize = enetmsg[1];
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
				printf("packetsize set to %u\n",g_packetsize);
			} else {
                g_packetsize = g_recLen;
				g_nDataPorts = (g_recLen-1)/g_packetsize+1;
            }
            break;
        }

		case(CASE_CLOSE_PROGRAM):{ // close process fork
			printf("closing program\n");
			FPGAclose(FPGA);
			break;
		}

		case(CASE_DATAGO):{ // if dataGo is zero it won't transmit data to server, basically a wait state
			if(enetmsg[1] == 1){
				printf("dataAcqGo set to 1\n");
			} else {
				printf("dataAcqGo set to 0\n");
			}
			DREF(FPGA->stateReset) = 1; 
			DREF(FPGA->read_addr) = 0;
			DREF(FPGA->stateReset) = 0;
			break;
		}

		case(CASE_QUERY_BOARD_INFO):{// loads board-specific data from onboard file
			getBoardData();
			send(commsock->sockfd,emsg,4*sizeof(uint32_t),0)
		}
		
		case(CASE_QUERY_DATA):{
			while( !DREF(FPGA->transReady) && ++tmp<1000 ){
				usleep(10);
			}
			
			if( DREF(FPGA->transReady) ){
				enet2 = commsock->prev;
				//~ dtmp = DREFP(FPGA->onchip1);
                //~ nsent = 0;
                while(enet2 != NULL){
					nsent = send(enet2->sockfd,enet2->data_addr,enet2->dataLen*8*sizeof(uint8_t),0);	
					if( nsent < 0 )
						perror("error sending data:")
					setsockopt(enet2->sockfd,IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
					enet2 = enet2->prev;
				}
				//~ for(n=0;ENET->sockfd[n]!=0;n++){
					//~ if( ( n == (g_nDataPorts-1) ) && ( g_recLen%g_packetsize > 0 ) ){
						//~ nsent = send(ENET->sockfd[n],&dtmp[8*n*g_packetsize],(g_recLen%g_packetsize)*8*sizeof(uint8_t),0);
					//~ } else {
                        //~ nsent = send(ENET->sockfd[n],&dtmp[8*n*g_packetsize],g_packetsize*8*sizeof(uint8_t),0);
					//~ }
                    //~ if ( nsent == -1 ) perror("error sending data:");
					//~ setsockopt(ENET->sockfd[n],IPPROTO_TCP,TCP_QUICKACK,&ONE,sizeof(int));
				//~ }
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














