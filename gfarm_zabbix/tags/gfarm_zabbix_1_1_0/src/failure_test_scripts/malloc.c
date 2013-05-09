#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc,char *argv[])
{
    size_t total, used, buffer, cached, size, i;
    int period;
    char *mem;
    if (argc != 6) {
        printf("USAGE: malloc total used buffer cached period \n");
        printf("       malloc `free | grep Mem | awk '{print $2, $3, $6, $7}'` period\n");
        exit(1);
    }
    total = atol(argv[1]);
    printf("total   : %8ld\n", total);
    if (total <= 0) {
        printf("ERROR: total <= 0\n");
        exit(1);
    }
    used = atol(argv[2]);
    printf("used    : %8ld\n", used);
    if (used <= 0) {
        printf("ERROR: used <= 0\n");
        exit(1);
    }
    buffer = atol(argv[3]);
    printf("buffer  : %8ld\n", buffer);
    if (buffer <= 0) {
        printf("ERROR: buffer <= 0\n");
        exit(1);
    }
    cached = atol(argv[4]);
    printf("cached  : %8ld\n", cached);
    if (cached <= 0) {
        printf("ERROR: cached <= 0\n");
        exit(1);
    }
    period = atoi(argv[5]);
    printf("period  : %8d\n", period);
    if (period <= 0) {
        printf("ERROR: period <= 0\n");
        exit(1);
    }
    /* sum size */
    size = (size_t)((double)(total + buffer + cached) * 0.97) - used;
    printf("size    : %8ld\n", size);
    /* allocate */
    mem = (char *)malloc(size);
    if(mem == NULL) {
        printf("ERROR: allocation failed.\n");
        exit(1);
    }
    printf("success to allocation\n");
    /* write something */
    for (i = 0; i < size; i++) {
        mem[i] = 0;
    }
    printf("success to write.\n");
    /* wait for period */
    sleep(period);
    printf("bye!.\n");
    return 0;
}
