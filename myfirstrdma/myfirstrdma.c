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
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

/* Enumeration for the error types we may enounter. We avoid using
 * perror just to keep the code simple.
 */

enum errors {
    NO_RDMA_DEV    = 1,
    NO_CONTEXT     = 2,
    NO_PROT_DOMAIN = 3,
    NO_BUFFER      = 4,
    NO_MR          = 5,
    NO_CONNECTION  = 6,
    RUN_PROBLEM    = 7,
};

/* 
 * Define a container structure that stores all the relevant
 * information for this very simple RDMA program. After that, assign
 * some defaults to this container.
 */

struct myfirstrdma {
  char                    *server;
  struct ibv_device       **dev_list;
  struct ibv_context	  *ctx;
  struct ibv_pd           *pd;
  char                    *buf;
  struct ibv_mr           *mr;
  int                     mr_flags;
  size_t                  size;
  char                    *port;
  struct rdma_addrinfo    hints;
  struct rdma_cm_id       *lid;
  struct rdma_cm_id       *cid;
  struct ibv_qp_init_attr attr;
  unsigned                debug;
  unsigned                verbose;
};

static const struct myfirstrdma defaults = {
    .server   = NULL,
    .mr_flags = IBV_ACCESS_LOCAL_WRITE,
    .size     = 16,
    .port     = "12345",
    .hints    = { 0 },
    .attr     = { 0 },
    .debug    = 1,
    .verbose  = 1,
};

/* 
 * A helper function to assist in debug when function calls return
 * with an exit status.
 */

int __report(struct myfirstrdma *cfg, const char *func, int val)
{
  if (cfg->debug)
    fprintf(stderr,"%s: %d = %s.\n", func, errno, strerror(errno));

  return val;
}

/* 
 * A static (local) function to establish the rdma connection. If
 * server is not NULL we know we are the client. This affects how
 * rdmacm is used. Return 0 on success.
 */

int __setup(struct myfirstrdma *cfg)
{
    int ret = 0;
    struct rdma_addrinfo *res;

    /* 
     * Use rdma_getaddrinfo to determine if there is a path to the
     * other end. The server runs in passive mode so it waits for a
     * connection. The client uses this function call to determine if
     * a path exists based on the server name, port and the hints
     * provided. The result comes back in res.
     */

    cfg->hints.ai_port_space = RDMA_PS_TCP;
    if (cfg->server)
        ret = rdma_getaddrinfo(cfg->server, cfg->port, &cfg->hints, &res);
    else {
        cfg->hints.ai_flags = RAI_PASSIVE;
        ret = rdma_getaddrinfo(NULL, cfg->port, &cfg->hints, &res);
    }
    if (ret)
      return __report(cfg, "rdma_getaddrinfo", ret);

    /* 
     * Now create a communication identifier and (on the client) a
     * queue pair (QP) for processing jobs. We set certain attributes
     * on this link.
     */

    cfg->attr.cap.max_send_wr     = cfg->attr.cap.max_recv_wr  = 1;
    cfg->attr.cap.max_send_sge    = cfg->attr.cap.max_recv_sge = 1;
    cfg->attr.cap.max_inline_data = cfg->size;
    cfg->attr.sq_sig_all          = 1;

    if (cfg->server){
      cfg->attr.qp_context = cfg->cid;
      ret = rdma_create_ep(&cfg->cid, res, NULL, &cfg->attr);
    } else
      ret = rdma_create_ep(&cfg->lid, res, NULL, &cfg->attr);
    
    if (ret)
        return __report(cfg, "rdma_create_ep", ret);

    /* 
     * We can now free the temporary variable with the address
     * information for the other end of the link.
     */
    
    rdma_freeaddrinfo(res);

    /* Now either connect to the server or setup a wait for a
     *  connection from a client.
     */

    if (cfg->server){
      cfg->mr = rdma_reg_msgs(cfg->cid, cfg->buf, cfg->size);
      if (ret)
	return __report(cfg, "rdma_reg_msgs", ret);
      ret = rdma_connect(cfg->cid, NULL);
      if (ret)
	return __report(cfg, "rdma_connect", ret);
      if (cfg->verbose)
	fprintf(stdout, "Client established a connection to %s.\n",
		cfg->server);
    } else {
      ret = rdma_listen(cfg->lid, 0);
      if (ret)
	return __report(cfg, "rdma_listen", ret);
      ret = rdma_get_request(cfg->lid, &cfg->cid);
      if (ret)
	return __report(cfg, "rdma_get_request", ret);
      cfg->mr = rdma_reg_msgs(cfg->cid, cfg->buf, cfg->size);
      if (ret)
	return __report(cfg, "rdma_reg_msgs", ret);
      ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
      if (ret)
	return __report(cfg, "rdma_post_recv", ret);
      ret = rdma_accept(cfg->cid, NULL);
      if (ret)
	return __report(cfg, "rdma_accept", ret);
      if (cfg->verbose)
	fprintf(stdout, "Server detected a connection on %s from TBD.\n",
		cfg->cid->verbs->device->name);
    }


    return ret;
}

int __run(struct myfirstrdma *cfg)
{

  int ret = 0;
  unsigned i = 0;
  struct ibv_wc wc;

  if (cfg->server){
    memset(cfg->buf, 0xa, cfg->size);
    ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, NULL, IBV_SEND_INLINE);
    if (ret)
      return __report(cfg, "rdma_post_send", ret);
  } else {
    while(1){
      fprintf(stdout,"\n");
      fprintf(stdout,"%d\t0x%x\r",i, cfg->buf[0]);
      usleep(1000000); i++;
    }
  }
    
  return ret;
}

int main(int argc, char *argv[])
{

    struct myfirstrdma cfg = defaults;

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
      return __report(&cfg, "ibv_open_device", NO_CONTEXT);
    
    /*    cfg.pd = ibv_alloc_pd(cfg.ctx);
    if (!cfg.pd)
    return __report(&cfg, "ibv_alloc_pd", NO_PROT_DOMAIN);*/
    
    cfg.buf = malloc(cfg.size);
    if (!cfg.buf)
      return __report(&cfg, "malloc", NO_BUFFER);
    
    /*    cfg.mr = ibv_reg_mr(cfg.pd, cfg.buf, cfg.size, cfg.mr_flags);
    if (!cfg.mr)
    return __report(&cfg, "ibv_reg_mr", NO_MR);*/

    /* Now we get to some client/server specifc code. We use the rdma
     * connection manager (rdmacm) library to establish a
     * connection. This is only one of several ways of doing this. The
     * server enters a passive listening mode while the client
     * actively connects to the server provided via the command line.
     */

    if ( __setup(&cfg) )
      return __report(&cfg, "setup", NO_MR);

    if ( __run(&cfg) )
      return __report(&cfg, "run", RUN_PROBLEM);


    /* Tear everything down in reverse order to how it was constucted
     * so we leave the program in a clean state.
     */

    free(cfg.buf);
    ibv_dereg_mr(cfg.mr);
    ibv_dealloc_pd(cfg.pd);
    ibv_close_device(cfg.ctx);

    return 0;
}
