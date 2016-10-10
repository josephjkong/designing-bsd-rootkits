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

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <fs/devfs/devfs_int.h>

extern TAILQ_HEAD(,cdev_priv) cdevp_list;

d_read_t	read_hook;
d_read_t	*read;

/* read entry point hook. */
int
read_hook(struct cdev *dev, struct uio *uio, int ioflag)
{
	uprintf("You ever dance with the devil in the pale moonlight?\n");

	return((*read)(dev, uio, ioflag));
}

/* The function called at load/unload. */
static int
load(struct module *module, int cmd, void *arg)
{
	int error = 0;
	struct cdev_priv *cdp;

	switch (cmd) {
	case MOD_LOAD:
		mtx_lock(&devmtx);

		/* Replace cd_example's read entry point with read_hook. */
		TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {
			if (strcmp(cdp->cdp_c.si_name, "cd_example") == 0) {
				read = cdp->cdp_c.si_devsw->d_read;
				cdp->cdp_c.si_devsw->d_read = read_hook;
				break;
			}
		}

		mtx_unlock(&devmtx);
		break;

	case MOD_UNLOAD:
		mtx_lock(&devmtx);

		/* Change everything back to normal. */
		TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {
			if (strcmp(cdp->cdp_c.si_name, "cd_example") == 0) {
				cdp->cdp_c.si_devsw->d_read = read;
				break;
			}
		}

		mtx_unlock(&devmtx);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return(error);
}

static moduledata_t cd_example_hook_mod = {
	"cd_example_hook",	/* module name */
	load,			/* event handler */
	NULL			/* extra data */
};

DECLARE_MODULE(cd_example_hook, cd_example_hook_mod, SI_SUB_DRIVERS,
    SI_ORDER_MIDDLE);
