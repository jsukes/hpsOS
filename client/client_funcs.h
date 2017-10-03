

struct ENETsock{
	int sockfd;
	int portNum;
	const char *server_addr;
	struct sockaddr_in server_sockaddr;
};


struct FPGAvars{
	void *virtual_base;
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
};


union AcqdData{
	struct{
		unsigned short c1:8;
		unsigned short c2:8;
		unsigned short c3:8;
		unsigned short c4:8;
	};
	
	uint32_t data;
};
 //heelo

void FPGAclose(struct FPGAvars *FPGA){
	
	if( munmap( FPGA->virtual_base, HW_REGS_SPAN ) != 0 ) {
		printf( "ERROR: munmap() failed...\n" );
		close( FPGA->fd );
	}
	close( FPGA->fd );
}


int FPGA_init(struct FPGAvars *FPGA){
	
	if( ( FPGA->fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
		printf( "ERROR: could not open \"/dev/mem\"...\n" );
		return( 0 );
	}
	
	FPGA->virtual_base = mmap( NULL, HW_REGS_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, FPGA->fd, HW_REGS_BASE );
	
	FPGA->read_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_READ_ADDR_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->gpio0_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_GPIO0_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->gpio1_addr = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_GPIO1_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->transReady = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_TRANSMIT_READY_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->trigDelay = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_TRIG_DELAY_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->trigCnt = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_TRIG_COUNT_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->recLen = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_RECORD_LENGTH_BASE ) & ( uint32_t )( HW_REGS_MASK ) );

	FPGA->stateReset = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_H2F_FPGA_STATE_RESET_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	FPGA->stateVal = FPGA->virtual_base + ( ( uint32_t )( ALT_LWFPGASLVS_OFST + PIO_F2H_FPGA_STATE_BASE ) & ( uint32_t )( HW_REGS_MASK ) );
	
	if( FPGA->virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap() failed...\n" );
		close( FPGA->fd );
		return( 0 );
	}
	
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


void getBoardData(int argc, char *argv[], int *boardData){
		
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


void setupENETsock(struct ENETsock *ENET, const char* serverIP, int boardNum){
	struct timeval t0,t1;
	int diff;
    int zero = 0;
	ENET->server_addr = serverIP;	
	ENET->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    //setsockopt(ENET->sockfd,IPPROTO_TCP,TCP_NODELAY,&zero,sizeof(int));	
	ENET->server_sockaddr.sin_port = htons(INIT_PORT);
	ENET->server_sockaddr.sin_family = AF_INET;
	ENET->server_sockaddr.sin_addr.s_addr = inet_addr(ENET->server_addr);
	
	gettimeofday(&t0,NULL);
	
	if(connect(ENET->sockfd, (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){		
		while(connect(ENET->sockfd, (struct sockaddr *)&ENET->server_sockaddr, sizeof(ENET->server_sockaddr))  == -1){
			gettimeofday(&t1,NULL);
			diff = (t1.tv_sec-t0.tv_sec);
			if(diff>(600)){
				printf("NO CONNECT!!!!\n");
				break;
			}	
		}
	}
	
    int tmpmsg[4] = {0};
	tmpmsg[0] = boardNum;
	send(ENET->sockfd, &tmpmsg, 4*sizeof(int), 0);
}


void FPGA_dataAcqController(int inPipe, int outPipe, int sv){//uint32_t *data){
    
    struct FPGAvars FPGA;	
    
	uint32_t pipemsg[4] = {0};
	int nready,m,k,l; m = 0;
    uint32_t n;
    uint32_t datatmp[2*MAX_DATA_LEN];
    if( FPGA_init(&FPGA) == 1 ){
		pipemsg[0] = 0; pipemsg[1] = 1;
	} else {
		pipemsg[0] = 0; pipemsg[1] = 0;
	}

    // local version of runtime variables 
	uint32_t recLen = 2048;
    uint32_t packetsize = 512;
    
    // variables for select loop
	int maxfd, sec, usec;
	struct timeval tv;
	fd_set readfds;
	maxfd = inPipe;
	sec = 0; usec = 1000;

	int dataRunner = 1;
	int dataGo = 0;
	int cnt = 0;

	while(dataRunner == 1){
		tv.tv_sec = sec;
		tv.tv_usec = usec;
		FD_ZERO(&readfds);
		FD_SET(inPipe,&readfds);
		
		nready = select(maxfd+1,&readfds,NULL,NULL,&tv);

		if( nready > 0 ){
			if(FD_ISSET(inPipe, &readfds)){

				n = read(inPipe,&pipemsg,4*sizeof(uint32_t));
				if(n == 0){ // connection close or parent terminated
                    dataRunner = 0;
                    break;
                }
				
                switch(pipemsg[0]){
					case(CASE_TRIGDELAY):{ // change trig delay
						DREF(FPGA.trigDelay) = 20*pipemsg[1];
						printf("trig Delay set to %.2f\n",(float)DREF(FPGA.trigDelay)/20.0);
						break;
					}

					case(CASE_RECLEN):{ // change record length
						DREF(FPGA.transReady) = 0;
						if(pipemsg[1]<MAX_DATA_LEN){
							DREF(FPGA.recLen) = pipemsg[1];
							recLen = DREF(FPGA.recLen);
							printf("recLen set to %zu\n",DREF(FPGA.recLen));
                            if(pipemsg[2]>0 && pipemsg[2] <= recLen && pipemsg[2]%recLen == 0){
                                packetsize = pipemsg[2];
                                printf("packetsize set to: %zu\n",pipemsg[2]);
                            }
						} else {
							DREF(FPGA.recLen) = 2048;
							recLen = DREF(FPGA.recLen);
                            packetsize = 512;
                            printf("invalid recLen, defaulting to 2048, packetsize 512\n");
						}
						break;
					}

					case(CASE_TRANSREADY_TIMEOUT):{ // change select loop timeout/how often transReady is checked (min 100us)
						if(pipemsg[1] > 99 && pipemsg[1] <10000000){
							sec = pipemsg[1]/1000000;
							usec = pipemsg[1]%1000000; 
							tv.tv_sec = sec;
							tv.tv_usec = usec;
						} else {
							tv.tv_sec = 0;
							tv.tv_usec = 1000;
						}
                        printf("timeout set to %lus + %luus\n",tv.tv_sec,tv.tv_usec);
						break;
					}

					case(CASE_CLOSE_PROGRAM):{ // close process fork
                        printf("closing program\n");
						FPGAclose(&FPGA);
						dataGo = 0;
						dataRunner = 0;
						break;
					}

					case(CASE_DATAGO):{ // if dataGo is zero it won't transmit data to server, basically a wait state
                        if(pipemsg[1] == 1){
                            printf("dataAcqGo set to 1\n");
							dataGo = 1;
                            m = 0;
						} else {
                            printf("dataAcqGo set to 0\n");
							dataGo = 0;
						}
						break;
					}
    
                    case(CASE_SETPACKETSIZE):{
                        printf("packetsize set to: %zu\n",pipemsg[1]);
                        packetsize = pipemsg[1];
                        break;
                    }
 
					default:{
                        printf("default case, shutting down\n");
						FPGAclose(&FPGA);
						dataRunner = 0;
						dataGo = 0;
						break;
					}
				}
			}	
		} else if ( nready == 0 && dataGo == 1 ){ // if select loop times out, check for data
			if(DREF(FPGA.transReady) == 1){
                m = 0;	
                for(n=0;n<recLen;n++){
                    DREF(FPGA.read_addr) = n;
                    datatmp[2*m] = DREF(FPGA.gpio0_addr);
                    datatmp[2*m+1] = DREF(FPGA.gpio1_addr);
                    m++;
                    if((n%packetsize) == (packetsize-1)){
                        write(sv,datatmp,2*packetsize*sizeof(uint32_t));
                        m=0;
                    }
                }
                DREF(FPGA.stateReset) = 1; 
                DREF(FPGA.read_addr) = 0;
                DREF(FPGA.stateReset) = 0;
                cnt++;
            }
	    }
    }
	printf("dataAcq loop broken\n");
	close(inPipe); close(outPipe);
    FPGAclose(&FPGA);
	_exit(0);
}














