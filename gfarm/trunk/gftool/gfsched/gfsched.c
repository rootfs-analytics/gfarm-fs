#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "gfarm.h"

/*
 *  Create a hostfile.
 *
 *  gfsched <gfarm_url> [<hostfile>]
 *  gfsched -f <gfarm_file> [<hostfile>]
 */

char *program_name = "gfsched";

void
usage()
{
    fprintf(stderr, "usage: %s [-f] gfarm_file [hostfile]\n",
	    program_name);
    fflush(stderr);
}

int
main(int argc, char * argv[])
{
    char * gfarm_url = (char *)NULL, * gfarm_file = (char *)NULL;
    char * e = (char *)NULL;
    int filep = 0;
    FILE * outp = stdout;
    int errflg = 0;
    extern int optind;
    int nhosts;
    char **hosts;
    int c, i;

    while ((c = getopt(argc, argv, "f")) != EOF) {
	switch (c) {
	case 'f':
	    filep = 1;
	    break;
	case '?':
	default:
	    ++errflg;
	}
    }
    if (errflg) {
	usage();
	exit(2);
    }

    if (optind < argc) {
	if (filep)
	    gfarm_file = argv[optind];
	else {
	    gfarm_url = argv[optind];
	    e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	    if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	    }
	}
    }
    else {
	usage();
	exit(2);
    }
    ++optind;
    if (optind < argc) {
	outp = fopen(argv[optind], "w");
	if (outp == (FILE *)NULL) {
	    perror(argv[optind]);
	    exit(1);
	}
    }

    e = gfarm_initialize();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    e = gfarm_hosts_schedule(gfarm_file, (char *)NULL,
			     &nhosts, &hosts);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    for (i = 0; i < nhosts; ++i)
	fprintf(outp, "%s\n", hosts[i]);
    fclose(outp);

    e = gfarm_terminate();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    exit(0);
}
