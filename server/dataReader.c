#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <sys/types.h>
#include <unistd.h>

int main(){
	FILE *tmpfile = fopen("hello","rb");
	uint32_t dummy;
	size_t bytes_read;
	uint32_t nmax = 0;
	int n = 0;
	while(bytes_read = fread(&dummy,sizeof(uint32_t),1,tmpfile)){
		n+=bytes_read;
		nmax = (nmax > dummy) ? nmax : dummy;
	}
	printf("n = %d, %u\n",n,nmax);
	return(0);
}
