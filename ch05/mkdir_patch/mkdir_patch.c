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
 */

#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/module.h>

/* Kernel memory allocation (kmalloc) function code. */
unsigned char kmalloc[] =
        "\x55"                          /* push   %ebp                  */
        "\xb9\x01\x00\x00\x00"          /* mov    $0x1,%ecx             */
        "\x89\xe5"                      /* mov    %esp,%ebp             */
        "\x53"                          /* push   %ebx                  */
        "\xba\x00\x00\x00\x00"          /* mov    $0x0,%edx             */
        "\x83\xec\x10"                  /* sub    $0x10,%esp            */
        "\x89\x4c\x24\x08"              /* mov    %ecx,0x8(%esp)        */
        "\x8b\x5d\x0c"                  /* mov    0xc(%ebp),%ebx        */
        "\x89\x54\x24\x04"              /* mov    %edx,0x4(%esp)        */
        "\x8b\x03"                      /* mov    (%ebx),%eax           */
        "\x89\x04\x24"                  /* mov    %eax,(%esp)           */
        "\xe8\xfc\xff\xff\xff"          /* call   4e2 <kmalloc+0x22>    */
        "\x89\x45\xf8"                  /* mov    %eax,0xfffffff8(%ebp) */
        "\xb8\x04\x00\x00\x00"          /* mov    $0x4,%eax             */
        "\x89\x44\x24\x08"              /* mov    %eax,0x8(%esp)        */
        "\x8b\x43\x04"                  /* mov    0x4(%ebx),%eax        */
        "\x89\x44\x24\x04"              /* mov    %eax,0x4(%esp)        */
        "\x8d\x45\xf8"                  /* lea    0xfffffff8(%ebp),%eax */
        "\x89\x04\x24"                  /* mov    %eax,(%esp)           */
        "\xe8\xfc\xff\xff\xff"          /* call   500 <kmalloc+0x40>    */
        "\x83\xc4\x10"                  /* add    $0x10,%esp            */
        "\x5b"                          /* pop    %ebx                  */
        "\x5d"                          /* pop    %ebp                  */
        "\xc3"                          /* ret                          */
        "\x8d\xb6\x00\x00\x00\x00";     /* lea    0x0(%esi),%esi        */

/*
 * The relative address of the instructions following the call statements
 * within kmalloc.
 */
#define K_OFFSET_1	0x26
#define K_OFFSET_2	0x44

/* "Hello, world!\n" function code. */
unsigned char hello[] =
	"\x48"				/* H				*/
	"\x65"				/* e				*/
	"\x6c"				/* l				*/
	"\x6c"				/* l				*/
	"\x6f"				/* o				*/
	"\x2c"				/* ,				*/
	"\x20"				/*				*/
	"\x77"				/* w				*/
	"\x6f"				/* o				*/
	"\x72"				/* r				*/
	"\x6c"				/* l				*/
	"\x64"				/* d				*/
	"\x21"				/* !				*/
	"\x0a"				/* \n				*/
	"\x00"				/* NULL				*/
	"\x55"				/* push   %ebp			*/
	"\x89\xe5"			/* mov    %esp,%ebp		*/
	"\x83\xec\x04"			/* sub    $0x4,%esp		*/
	"\xc7\x04\x24\x00\x00\x00\x00"	/* movl   $0x0,(%esp)		*/
	"\xe8\xfc\xff\xff\xff"		/* call   uprintf		*/
	"\x31\xc0"			/* xor    %eax,%eax		*/
	"\x83\xc4\x04"			/* add    $0x4,%esp		*/
	"\x5d";				/* pop    %ebp			*/

/*
 * The relative address of the instruction following the call uprintf
 * statement within hello.
 */
#define H_OFFSET_1	0x21

/* Unconditional jump code. */
unsigned char jump[] =
	"\xb8\x00\x00\x00\x00"		/* movl   $0x0,%eax		*/
	"\xff\xe0";			/* jmp    *%eax			*/

int
main(int argc, char *argv[])
{
	int i, call_offset;
	char errbuf[_POSIX2_LINE_MAX];
	kvm_t *kd;
	struct nlist nl[] = { {NULL}, {NULL}, {NULL}, {NULL}, {NULL},
	    {NULL}, };
	unsigned char mkdir_code[sizeof(kmalloc)];
	unsigned long addr, size;

	/* Initialize kernel virtual memory access. */
	kd = kvm_openfiles(NULL, NULL, NULL, O_RDWR, errbuf);
	if (kd == NULL) {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(-1);
	}

	nl[0].n_name = "mkdir";
	nl[1].n_name = "M_TEMP";
	nl[2].n_name = "malloc";
	nl[3].n_name = "copyout";
	nl[4].n_name = "uprintf";

	/*
	 * Find the address of mkdir, M_TEMP, malloc, copyout,
	 * and uprintf.
	 */
	if (kvm_nlist(kd, nl) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	for (i = 0; i < 5; i++) {
		if (!nl[i].n_value) {
			fprintf(stderr, "ERROR: Symbol %s not found\n",
			    nl[i].n_name);
			exit(-1);
		}
	}

	/* Save sizeof(kmalloc) bytes of mkdir. */
	if (kvm_read(kd, nl[0].n_value, mkdir_code, sizeof(kmalloc)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/* Search through mkdir for call kern_mkdir. */
	for (i = 0; i < sizeof(kmalloc); i++) {
		if (mkdir_code[i] == 0xe8) {
			call_offset = i;
			break;
		}
	}

	/* Determine how much memory you need to allocate. */
	size = (unsigned long)sizeof(hello) + (unsigned long)call_offset +
	    (unsigned long)sizeof(jump);

	/*
	 * Patch the kmalloc function code to contain the correct addresses
	 * for M_TEMP, malloc, and copyout.
	 */
	*(unsigned long *)&kmalloc[10] = nl[1].n_value;
	*(unsigned long *)&kmalloc[34] = nl[2].n_value -
	    (nl[0].n_value + K_OFFSET_1);
	*(unsigned long *)&kmalloc[64] = nl[3].n_value -
	    (nl[0].n_value + K_OFFSET_2);

	/* Overwrite mkdir with kmalloc. */
	if (kvm_write(kd, nl[0].n_value, kmalloc, sizeof(kmalloc)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/* Allocate kernel memory. */
	syscall(136, size, &addr);

	/* Restore mkdir. */
	if (kvm_write(kd, nl[0].n_value, mkdir_code, sizeof(kmalloc)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/*
	 * Patch the "Hello, world!\n" function code to contain the
	 * correct addresses for the "Hello, world!\n" string and uprintf.
	 */
	*(unsigned long *)&hello[24] = addr;
	*(unsigned long *)&hello[29] = nl[4].n_value - (addr + H_OFFSET_1);

	/*
	 * Place the "Hello, world!\n" function code into the recently
	 * allocated kernel memory.
	 */
	if (kvm_write(kd, addr, hello, sizeof(hello)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/*
	 * Place all the mkdir code up to but not including call kern_mkdir
	 * after the "Hello, world!\n" function code.
	 */
	if (kvm_write(kd, addr + (unsigned long)sizeof(hello) - 1,
	    mkdir_code, call_offset) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/*
	 * Patch the unconditional jump code to jump back to the call
	 * kern_mkdir statement within mkdir.
	 */
	*(unsigned long *)&jump[1] = nl[0].n_value +
	    (unsigned long)call_offset;

	/*
	 * Place the unconditional jump code into the recently allocated
	 * kernel memory, after the mkdir code.
	 */
	if (kvm_write(kd, addr + (unsigned long)sizeof(hello) - 1 +
	    (unsigned long)call_offset, jump, sizeof(jump)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/*
	 * Patch the unconditional jump code to jump to the start of the
	 * "Hello, world!\n" function code.
	 */
	*(unsigned long *)&jump[1] = addr + 0x0f;

	/*
	 * Overwrite the beginning of mkdir with the unconditional
	 * jump code.
	 */
	if (kvm_write(kd, nl[0].n_value, jump, sizeof(jump)) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	/* Close kd. */
	if (kvm_close(kd) < 0) {
		fprintf(stderr, "ERROR: %s\n", kvm_geterr(kd));
		exit(-1);
	}

	exit(0);
}
