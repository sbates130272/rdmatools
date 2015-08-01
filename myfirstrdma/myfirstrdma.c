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
#include <fcntl.h>

#include <sys/time.h>
#include <sys/mman.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "../argconfig/argconfig.h"
#include "../argconfig/report.h"

enum errors {
  BAD_ARGS       = 1,
  NO_OPEN,
  NO_MMAP,
  NO_BUFFER,
  SETUP_PROBLEM,
  RUN_PROBLEM,
};

/*
 * Define a container structure that stores all the relevant
 * information for this very simple RDMA program. After that, assign
 * some defaults to this container.
 */

const char program_desc[] =
    "Simple RDMA test program, acts as both client and server";

struct myfirstrdma {
  char                    *server;
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
  unsigned                wait;
  unsigned                memset;

  unsigned                copymmio;
  unsigned                peerdirect;
  int                     mmiofd;
  void                    *mmio;
  char                    *mmap;

  struct timeval          start_time;
  struct timeval          end_time;

  struct timeval          *latency;
};

static const struct myfirstrdma defaults = {
  .server     = NULL,
  .mr_flags   = IBV_ACCESS_LOCAL_WRITE,
  .size       = 4096,
  .port       = "12345",
  .hints      = { 0 },
  .attr       = { 0 },
  .debug      = 1,
  .verbose    = 1,
  .iters      = 512,
  .wait       = 0,
  .memset     = 0,

  .copymmio   = 0,
  .peerdirect = 0,
  .mmap       = "/sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource4",
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"s",             "NUM", CFG_LONG_SUFFIX, &defaults.size, required_argument, NULL},
    {"size",          "NUM", CFG_LONG_SUFFIX, &defaults.size, required_argument,
            "block size to use"},
    {"i",             "NUM", CFG_LONG_SUFFIX, &defaults.iters, required_argument, NULL},
    {"iters",         "NUM", CFG_LONG_SUFFIX, &defaults.iters, required_argument,
            "number of iterations to perform"},
    {"port",          "PORT", CFG_STRING, &defaults.port, required_argument,
            "port to use"},
    {"w",       "", CFG_NONE, &defaults.wait, no_argument, NULL},
    {"wait",    "", CFG_NONE, &defaults.wait, no_argument,
            "use the in-build wait function which polls the MR"},
    {"m",        "", CFG_NONE, &defaults.memset, no_argument, NULL},
    {"memset",   "", CFG_NONE, &defaults.memset, no_argument,
            "update the MR with data (should be used with --wait)"},
    {"c",          "", CFG_NONE, &defaults.copymmio, no_argument, NULL},
    {"copymmio",   "", CFG_NONE, &defaults.copymmio, no_argument,
            "on server also copy data to a mmap region"},
    {"p",             "", CFG_NONE, &defaults.peerdirect, no_argument, NULL},
    {"peerdirect",    "", CFG_NONE, &defaults.peerdirect, no_argument,
            "use PeerDirect (cannot use -c and must -m must lie within IOMEM)"},
    {"mmap",          "MMAP", CFG_STRING, &defaults.mmap, required_argument,
            "file to mmap, for -p should lie within IOMEM"},
    {"v",             "", CFG_NONE, &defaults.verbose, no_argument, NULL},
    {"verbose",       "", CFG_NONE, &defaults.verbose, no_argument,
            "be verbose"},
    {0}
};


static int report(struct myfirstrdma *cfg, const char *func, int val)
{
  if (cfg->debug)
    fprintf(stderr,"%s: %d = %s.\n", func, errno, strerror(errno));

  return val;
}

int compare(char *buf, char val, size_t size)
{
  
  for (unsigned i=0; i<size; i++)
    if (buf[i]!=val)
      return 0;
  
  return 1;
}

void wait(char *buf, char val, size_t size)
{
  while (compare(buf, val, size)==0)
    __sync_synchronize();
}

static unsigned long long elapsed_utime(struct timeval start_time,
					struct timeval end_time)
{
  unsigned long long ret = (end_time.tv_sec - start_time.tv_sec)*1000000 +
    (end_time.tv_usec - start_time.tv_usec);
  return ret;
}

static int setup(struct myfirstrdma *cfg)
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
  if (cfg->server) {
    ret = rdma_getaddrinfo(cfg->server, cfg->port, &cfg->hints, &res);
  } else {
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

int run(struct myfirstrdma *cfg)
{
  
  int ret;
  struct ibv_wc wc;
  int sval = 0, cval = 1;

  memset((void*)cfg->buf, 0x0, cfg->size);
  
  if (cfg->verbose)
    fprintf(stdout, "%s %d iterations of %zdB chunks...",
	    (cfg->server) ? "Initiating" : "Servicing", cfg->iters, cfg->size);

  gettimeofday(&cfg->start_time, NULL);

  for (unsigned i=0; i<cfg->iters ; i++) {

    if (cfg->server){
      if (cfg->memset)
	memset((void*)cfg->buf, cval, cfg->size);
      __sync_synchronize();
      ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr, 0);
      if (ret)
	return report(cfg, "rdma_post_send", ret);
      ret = rdma_get_send_comp(cfg->cid, &wc);
      gettimeofday(&cfg->latency[2*i], NULL);
      if (ret != 1)
	return report(cfg, "rdma_get_send_comp", ret);
      ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
      if (ret)
	return report(cfg, "rdma_post_recv", ret);
    } else {
      if (cfg->wait)
	wait(cfg->buf, cval, cfg->size);
      ret = rdma_get_recv_comp(cfg->cid, &wc);
      gettimeofday(&cfg->latency[2*i], NULL);
      if (ret != 1)
	return report(cfg, "rdma_get_recv_comp", ret);
      if (cfg->copymmio)
	if ( !memcpy(cfg->mmio, cfg->buf, cfg->size) )
	  return report(cfg, "memcpy", -ENOMEM);
    }
    
    sval = cval+1;
    
    if (cfg->server) {
      if (cfg->wait)
	wait(cfg->buf, sval, cfg->size);
      ret = rdma_get_recv_comp(cfg->cid, &wc);
      gettimeofday(&cfg->latency[2*i+1], NULL);
      if (ret != 1)
	return report(cfg, "rdma_get_recv_comp", ret);

    } else {
      if (cfg->copymmio)
	if ( !memcpy(cfg->buf, cfg->mmio, cfg->size) )
	  return report(cfg, "memcpy", -ENOMEM);
      if (cfg->memset)
	memset(cfg->buf, sval, cfg->size);
      __sync_synchronize();
      ret = rdma_post_send(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr, 0);
      if (ret)
	return report(cfg, "rdma_post_send", ret);
      ret = rdma_get_send_comp(cfg->cid, &wc);
      gettimeofday(&cfg->latency[2*i+1], NULL);
      if (ret != 1)
	return report(cfg, "rdma_get_send_comp", ret);
      ret = rdma_post_recv(cfg->cid, NULL, cfg->buf, cfg->size, cfg->mr);
      if (ret)
	return report(cfg, "rdma_post_recv", ret);
    }
    cval=sval+1;
    
  }

  gettimeofday(&cfg->end_time, NULL);
  fprintf(stdout, "done.\n");

  report_transfer_rate(stdout, &cfg->start_time,
		       &cfg->end_time, cfg->iters*cfg->size*2);

  fprintf(stdout, "\nAverage latency = %llu us.\n", 
	  elapsed_utime(cfg->start_time, cfg->end_time) /
	  cfg->iters / 2);

  return 0;
}

int main(int argc, char *argv[])
{

  struct myfirstrdma cfg;

  argconfig_append_usage("[SERVER_NAME]");
  int args = argconfig_parse(argc, argv, program_desc, command_line_options,
		  &defaults, &cfg, sizeof(cfg));
  
  if (args > 1) { 
    argconfig_print_help(argv[0], program_desc, command_line_options); 
    return 1; 
  } 

  if (args==1)
    cfg.server = strdup(argv[1]);

  if (cfg.copymmio && cfg.peerdirect)
    return report(&cfg, "bad defaults", BAD_ARGS);
  
  if (cfg.peerdirect && !cfg.mmap)
    return report(&cfg, "bad defaults", BAD_ARGS);

  if ( (cfg.copymmio || cfg.peerdirect) && cfg.mmap && !cfg.server) {
    cfg.mmiofd = open(cfg.mmap, O_RDWR);
    if (cfg.mmiofd < 0)
      return report(&cfg, "open", NO_OPEN);
    cfg.mmio = mmap(NULL, cfg.size, PROT_WRITE | PROT_READ,
		   MAP_SHARED, cfg.mmiofd, 0);
    if (!cfg.mmio < 0)
      return report(&cfg, "mmap", NO_MMAP);
  }

  if ( cfg.peerdirect && !cfg.server ){
    cfg.buf = cfg.mmio;
  } else {
    cfg.buf = malloc(cfg.size);
    if (!cfg.buf)
      return report(&cfg, "malloc", NO_BUFFER);
  }
  cfg.latency = malloc(2*cfg.iters*sizeof(struct timeval));
  if (!cfg.latency)
    return report(&cfg, "malloc", NO_BUFFER);
  
  if ( setup(&cfg) )
    return report(&cfg, "setup", SETUP_PROBLEM);
  
  if ( run(&cfg) )
    return report(&cfg, "run", RUN_PROBLEM);

  if ((cfg.copymmio || cfg.peerdirect) && cfg.mmap && !cfg.server) {
    munmap(cfg.mmio, cfg.size);
    close(cfg.mmiofd);
  }
  if (!cfg.peerdirect || cfg.server)
    free(cfg.buf);
  free(cfg.latency);
  ibv_dereg_mr(cfg.mr);

  return 0;
}
