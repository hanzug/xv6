// sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h" // 必须以这个顺序 include，由于三个头文件有依赖关系

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("usage: sleep <ticks>\n");
	}
	sleep(atoi(argv[1]));
	exit(0);
}
