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
  unsigned                iters;
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
  .iters    = 16,
};

int report(struct myfirstrdma *cfg, const char *func, int val)
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

int setup(struct myfirstrdma *cfg)
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
    return report(cfg, "rdma_getaddrinfo", ret);

  /*
   * Now create a communication identifier and (on the client) a
   * queue pair (QP) for processing jobs. We set certain attributes
   * on this link.
   */
  
  cfg->attr.cap.max_send_wr     = cfg->attr.cap.max_recv_wr  = 1;
  cfg->attr.cap.max_send_sge    = cfg->attr.cap.max_recv_sge = 1;
  cfg->attr.cap.max_inline_data = 16;
  cfg->attr.sq_sig_all          = 1;
  
  if (cfg->server){
    cfg->attr.qp_context = cfg->cid;
    ret = rdma_create_ep(&cfg->cid, res, NULL, &cfg->attr);
  } else
    ret = rdma_create_ep(&cfg->lid, res, NULL, &cfg->attr);
  
  if (ret)
    return report(cfg, "rdma_create_ep", ret);

  /*
   * We can now free the temporary variable with the address
   * information for the other end of the link.
   */

  rdma_freeaddrinfo(res);
  
  /* Now either connect to the server or setup a wait for a
   * connection from a client. On the server side we also setup a
   * receive QP entry so we are ready to receive data.
   */
  
  if (cfg->server){
    cfg->mr = rdma_reg_msgs(cfg->cid, cfg->buf, cfg->size);
    if (ret)
      return report(cfg, "rdma_reg_msgs", ret);
    ret = rdma_connect(cfg->cid, NULL);
    if (ret)
      return report(cfg, "rdma_connect", ret);
    if (cfg->verbose)
      fprintf(stdout, "Client established a connection to %s.\n",
	      cfg->server);
  } else {
    ret = rdma_listen(cfg->lid, 0);
    if (ret)
      return report(cfg, "rdma_listen", ret);
    ret = rdma_get_request(cfg->lid, &cfg->cid);
    if (ret)
      return report(cfg, "rdma_get_request", ret);
    cfg->mr = rdma_reg_msgs(cfg->cid, cfg->buf, cfg->size);
    if (ret)
      return report(cfg, "rdma_reg_msgs", ret);
    ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
    if (ret)
      return report(cfg, "rdma_post_recv", ret);
    ret = rdma_accept(cfg->cid, NULL);
    if (ret)
      return report(cfg, "rdma_accept", ret);
    if (cfg->verbose)
      fprintf(stdout, "Server detected a connection on %s from TBD.\n",
	      cfg->cid->verbs->device->name);
  }
    return ret;
}

int compare(char *buf, char val, size_t size)
{
  
  for (unsigned i=0; i<size; i++){
    printf("%d\t%d\t%d\n", i, buf[i], val);
    fflush(stdout);
    if (buf[i]!=val)
      return 0;
  }
  
  return 1;
}

void wait(char *buf, char val, size_t size)
{
  while (compare(buf, val, size)==0)
    __sync_synchronize();
}

int __run(struct myfirstrdma *cfg)
{
  
  int ret;
  struct ibv_wc wc;
  int sval = 0, cval = 1;
  
  memset((void*)cfg->buf, 0x0, cfg->size);
  memset((void*)cfg->buf, 0x7, cfg->size);
  for (unsigned i=0; i<cfg->iters ; i++)
  {
    printf("\n\nHello! cval=%d sval=%d\n\n",cval,sval);
    
    if (cfg->server){
      memset((void*)cfg->buf, cval, cfg->size);
      usleep(1000);
      fprintf(stderr,"0x%x\r", cfg->buf[0]);
      ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr, 0);
      if (ret)
	return report(cfg, "rdma_post_send", ret);
      ret = rdma_get_send_comp(cfg->cid, &wc);
      if (ret != 1)
	return report(cfg, "rdma_get_send_comp", ret);
      ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
      if (ret)
	return report(cfg, "rdma_post_recv", ret);
    }
    else {
      wait(cfg->buf, cval, cfg->size);
      ret = rdma_get_recv_comp(cfg->cid, &wc);
      if (ret != 1)
	return report(cfg, "rdma_get_recv_comp", ret);
    }
    
    sval = cval+1;
    
    if (cfg->server) {
      wait(cfg->buf, sval, cfg->size);
      ret = rdma_get_recv_comp(cfg->cid, &wc);
      if (ret != 1)
	return report(cfg, "rdma_get_recv_comp", ret);

    } else {
      memset(cfg->buf, sval, cfg->size);
      usleep(1000);
      ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr, 0);
      if (ret)
	return report(cfg, "rdma_post_send", ret);
      ret = rdma_get_send_comp(cfg->cid, &wc);
      if (ret != 1)
	return report(cfg, "rdma_get_send_comp", ret);
      ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
      if (ret)
	return report(cfg, "rdma_post_recv", ret);
    }
    cval=sval+1;
    
  }
  
  /*if (cfg->server){
    ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr, 0);
    if (ret)
      return report(cfg, "rdma_post_send", ret);
    ret = rdma_get_send_comp(cfg->cid, &wc);
    if (ret)
      return report(cfg, "rdma_get_send_comp", ret);
  } else {
    unsigned i=0;
    while(1){
      fprintf(stdout,"\n");
      fprintf(stderr,"%d\t0x%x\r",i, cfg->buf[0]);
      usleep(1000000); i++;
    }
    }*/

  return 0;
}

int main(int argc, char *argv[])
{

  struct myfirstrdma cfg = defaults;
  
  if (argc>2)
    return 1;
  else if (argc==2)
    cfg.server = strdup(argv[1]);
  
  cfg.buf = malloc(cfg.size);
  if (!cfg.buf)
    return report(&cfg, "malloc", NO_BUFFER);
  
  if ( setup(&cfg) )
    return report(&cfg, "setup", NO_MR);
  
  if ( __run(&cfg) )
    return report(&cfg, "run", RUN_PROBLEM);
  
  free(cfg.buf);
  ibv_dereg_mr(cfg.mr);

  return 0;
}
