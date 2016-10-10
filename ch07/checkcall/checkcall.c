/*-
 * Copyright (c) 2007 Joseph Kong.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2001, Stephanie Wehner <atrak@itsx.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysent.h>

void usage();

int
main(int argc, char *argv[])
{
	char errbuf[_POSIX2_LINE_MAX];
	kvm_t *kd;
	struct nlist nl[] = { { NULL }, { NULL }, { NULL }, };

	unsigned long addr;
	int callnum;
	struct sysent call;

	/* Check arguments. */
	if (argc < 3) {
		usage();
		exit(-1);
	}

	nl[0].n_name = "sysent";
	nl[1].n_name = argv[1];
	callnum = (int)strtol(argv[2], (char **)NULL, 10);

	printf("Checking system call %d: %s\n\n", callnum, argv[1]);

	kd = kvm_openfiles(NULL, NULL, NULL, O_RDWR, errbuf);
	if (!kd) {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(-1);
	}

	/* Find the address of sysent[] and argv[1]. */
	if (kvm_nlist(kd, nl) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	if (nl[0].n_value)
		printf("%s[] is 0x%x at 0x%lx\n", nl[0].n_name, nl[0].n_type,
		    nl[0].n_value);
	else {
		fprintf(stderr, "ERROR: %s not found (very weird...)\n",
		    nl[0].n_name);
		exit(-1);
	}

	if (!nl[1].n_value) {
		fprintf(stderr, "ERROR: %s not found\n", nl[1].n_name);
		exit(-1);
	}

	/* Determine the address of sysent[callnum]. */
	addr = nl[0].n_value + callnum * sizeof(struct sysent);

	/* Copy sysent[callnum]. */
	if (kvm_read(kd, addr, &call, sizeof(struct sysent)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/* Where does sysent[callnum].sy_call point to? */
	printf("sysent[%d] is at 0x%lx and its sy_call member points to "
	    "%p\n", callnum, addr, call.sy_call);

	/* Check if that's correct. */
	if ((uintptr_t)call.sy_call != nl[1].n_value) {
		printf("ALERT! It should point to 0x%lx instead\n",
		    nl[1].n_value);

		/* Should this be fixed? */
		if (argv[3] && strncmp(argv[3], "fix", 3) == 0) {
			printf("Fixing it... ");

			call.sy_call = (sy_call_t *)(uintptr_t)nl[1].n_value;
			if (kvm_write(kd, addr, &call, sizeof(struct sysent))
			    < 0) {
				fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
				exit(-1);
			}

			printf("Done.\n");
		}
	}

	if (kvm_close(kd) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	exit(0);
}

void
usage()
{
	fprintf(stderr,"Usage:\ncheckcall [system call function] "
	    "[call number] <fix>\n\n");
	fprintf(stderr, "For a list of system call numbers see "
	    "/sys/sys/syscall.h\n");
}
