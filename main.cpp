#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "fmt_no.h"

#ifdef NO_COLORS
# define RED ""
# define AMBER ""
# define YELLOW ""
# define GREEN ""
# define BOLD ""
# define INV ""
# define NORM ""
#else
#ifndef RED
# define RED "\x1b[0;31m"
# define AMBER "\x1b[0;33m"
# define YELLOW "\x1b[1;33m"
# define GREEN "\x1b[0;32m"
# define BOLD "\x1b[0;1m"
# define ULINE "\x1b[0;4m"
# define INV "\x1b[0;7m"
# define NORM "\x1b[0;0m"
#endif
#endif


#define min(a,b) (a < b ? a : b)
const char *bold = BOLD, *norm = NORM;
double starttime=0;
size_t compareSize=0;

int originalErrors=0;
int targetErrors=0;
int diffBlocks=0;
int diffBytes=0;

/**
 * @return different bytes
 */
int compareAreas(const void *original, const void *target, int len) {
	int ret=0;
	for(int i=0; i < len; i++) {
		if(*((char *)original+i) != *((char *)target+i)) {
			ret++;
		}
	}
	return ret;
}

void printProgress(size_t position) {
	struct timespec spec;
	clock_gettime(CLOCK_REALTIME, &spec);
	double now=spec.tv_sec + (spec.tv_nsec / 1.0e9);
	double percentDone=((double)(position) / compareSize );
	static double last=0;
	if(last == 0) {
		last=starttime;
	}
	static size_t lastPosition=0;

	char progress[51];
	for(int i=0; i < sizeof(progress)-1; i++) {
		if(i <= (percentDone * 100 / 2)) {
			progress[i]='-';
		} else {
			progress[i]='.';
		}
	}
	progress[50]='\0';


		printf("\x1b[udd_rescue: (info): pos:   %skB\n"
"                   errOriginal:      %d, errTarget: %d, diff blocks: %d, diff bytes: %d    \n"
"                   +curr.rate:   %skB/s, avg.rate:     %skB/s, avg.load:  xx%%    \n"
"                   >%s< %d%%  ETA:  %ds    \n",
			fmt_int(10, 1, 1024, position, bold, norm, 1),
			originalErrors, targetErrors, diffBlocks, diffBytes,
			fmt_int(10, 1, 1024, (position-lastPosition)/(now-last), bold, norm, 1), fmt_int(10, 1, 1024,(position/(now-starttime)), bold, norm, 1),
			progress,(int)(percentDone*100), (int) (percentDone > 0 ? (now-starttime)/percentDone -(now-starttime): -1)
			);
	last=now;
	lastPosition=position;
}

int main(int argc, char *argv[]) {

	if(argc != 3) {
		fprintf(stderr,"dd_verify original target\n");
		exit(1);
	}

	int softbs=128*1024;
	int hardbs=4;
	printf("dd_verify: (info): Using softbs=%dB, hardbs=%dB\n",softbs,hardbs);
	
	struct stat buf;
	int original=open(argv[1],O_RDONLY | O_LARGEFILE | O_DIRECT);
	if(!original) {
		fprintf(stderr, "error opening %s\n", argv[1]);
		exit(1);
	}

	int target=open(argv[2],O_RDONLY | O_LARGEFILE | O_DIRECT);
	if(!target) {
		fprintf(stderr, "error opening %s\n", argv[2]);
		exit(1);
	}

	if(fstat(original, &buf)) {
		fprintf(stderr, "error fstat %s\n", argv[1]);
		exit(1);
	}
	size_t originalSize=buf.st_size;
	if(originalSize == 0) {
		printf("reading device size\n");
		uint64_t size;
		ioctl(original, BLKGETSIZE64, &size);
		originalSize=size;
	}

	if(fstat(target, &buf)) {
		fprintf(stderr, "error fstat %s\n", argv[2]);
		exit(1);
	}
	size_t targetSize=buf.st_size;
	if(targetSize == 0) {
		printf("reading device size\n");
		uint64_t size;
		ioctl(target, BLKGETSIZE64, &size);
		targetSize=size;
	}

	if(originalSize != targetSize) {
		printf("warning: original: %skB target: %skB\n", fmt_int(10, 1, 1024, originalSize, bold, norm, 1), fmt_int(10, 1, 1024, targetSize, bold, norm, 1)	);
	}
	compareSize=min(originalSize, targetSize);

	printf("dd_verify: (info): expect to compare %s from %s and %s\n", fmt_int(10, 1, 1024, compareSize, bold, norm, 1), argv[1], argv[2]);
	void *originalBuffer=NULL;
	posix_memalign(&originalBuffer, softbs, softbs);
	void *targetBuffer=NULL;
	posix_memalign(&targetBuffer, softbs, softbs);

	// save cursor position:
	printf("\x1b[s");
	size_t position=0;
	struct timespec spec;
	clock_gettime(CLOCK_REALTIME, &spec);
	starttime=spec.tv_sec + (spec.tv_nsec / 1.0e9);
	double lastUpdate=0;

	while(position < compareSize) {
		// printf("startloop: position %zd\n", position);
		ssize_t originalRc=read(original, originalBuffer, softbs);
		bool originalSeek=false;
		if(originalRc < 0) {
			perror("");
			originalErrors++;
			originalSeek=true;
		}
		if(originalRc != softbs && ((position+originalRc) != compareSize)) {
			fprintf(stderr, "error reading %dbytes from original (only %d)\n", softbs, originalRc);
			abort();
		}
		if(originalSeek) {
			if(lseek(original, SEEK_SET, position+softbs) < 0) {
				fprintf(stderr, "error seek original to %zd\n", position+softbs);
				perror("");
				abort();
			}
		}

		ssize_t targetRc=read(target, targetBuffer, softbs);
		bool targetSeek=false;
		if(targetRc < 0) {
			perror("");
			targetErrors++;
			targetSeek=true;
		}
		if(targetRc != softbs) {
			printf("error reading %dbytes from target (only %d)\n", softbs, targetRc);
		}
		if(targetSeek) {
			if(lseek(target, SEEK_SET, position+softbs) < 0) {
				fprintf(stderr, "error seek target to %zd\n", position+softbs);
				perror("");
				abort();
			}
		}

		int d=compareAreas(originalBuffer, targetBuffer, min(originalRc,targetRc));
		if(d > 0) {
			diffBlocks++;
			diffBytes+=d;
		}

		// printf("lastUpdate:%f\n",lastUpdate);
		position+=originalRc;
		clock_gettime(CLOCK_REALTIME, &spec);
		double now=spec.tv_sec + (spec.tv_nsec / 1.0e9);
		if(lastUpdate < now - 0.1) {
			printProgress(position);
			lastUpdate=now;
		}
		
	}
	printf("done: position: %s\n", fmt_int(10, 1, 1024, position, bold, norm, 1));
	printProgress(position);
}

