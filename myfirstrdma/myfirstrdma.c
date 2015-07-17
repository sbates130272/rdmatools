////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 PMC-Sierra, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0 Unless required by
// applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for
// the specific language governing permissions and limitations under the
// License.
//
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
//   Author: Stephen Bates
//
//   Date:   July 2015
//
//   Description:
//     A super simple RDMA program. This program runs as either a
//     server (with no command line args) or as a client (with a
//     single argument, the address of the server) and ping-pongs a MR
//     between them using a incremental data pattern. We do some
//     simple performance measurements as we go along.
//
////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <string.h>

#include <infiniband/verbs.h>

/* Enumeration for the error types we may enounter.
 */

enum errors {
    NO_RDMA_DEV    = -1,
    NO_CONTEXT     = -2,
    NO_PROT_DOMAIN = -3,
    NO_BUFFER      = -4,
    NO_MR          = -5,
};

/* Define a container structure that stores all the relevant
 * information for this very simple RDMA program.
 */

struct myfirstrdma {
    char                *server;
	struct ibv_device   **dev_list;
	struct ibv_context	*ctx;
    struct ibv_pd       *pd;
    void                *buf;
    struct ibv_mr       *mr;
    int                 mr_flags;
    size_t              size;
};


int main(int argc, char *argv[])
{

    struct myfirstrdma cfg;

    /* Process the command line. The only argument allowed is in
       client mode and it points to the server. */

	if (argc>2)
		return 1;
    else if (argc==2)
        cfg.server = strdup(argv[1]);

    /* Now establish a RDMA connection between the client and
     * server. If we are the server we start and wait for the
     * connction to occur. If we are client we either find the server
     * or we quit. We work with the very first rdma port detected on
     * the machine.
     */

	cfg.dev_list = ibv_get_device_list(NULL);
	if (!cfg.dev_list)
		return NO_RDMA_DEV;

    /* Now that we have found a RDMA port we set that up for a
     * connection by creates a context, a Protection Domain (PD), a
     * Memory-Region (MR) and queues. Note we also have to allocate a
     * memory region for the MR via a call to malloc.
     */

	cfg.ctx = ibv_open_device(cfg.dev_list[0]);
	if (!cfg.ctx)
        return NO_CONTEXT;

	cfg.pd = ibv_alloc_pd(cfg.ctx);
	if (!cfg.pd)
		return NO_PROT_DOMAIN;

    cfg.buf = malloc(cfg.size);
	if (!cfg.buf)
		return NO_BUFFER;

	cfg.mr = ibv_reg_mr(cfg.pd, cfg.buf, cfg.size, cfg.mr_flags);
	if (!cfg.mr)
		return NO_MR;



    /* Tear everything down in reverse order to how it was constucted
     * so we leave the program in a clean state.
     */

    free(cfg.buf);
    ibv_dereg_mr(cfg.mr);
    ibv_dealloc_pd(cfg.pd);
    ibv_close_device(cfg.ctx);

    return 0;
}
