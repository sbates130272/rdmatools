#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include <infiniband/verbs.h>

/*
 * NB. This code relies on the MOFED experimental code being
 * installed. If it is not this code will not even compile!
 */

static void __ibv_pretty_print(struct ibv_device *dev)
{
    fprintf(stdout, "Name = %s.\n", ibv_get_device_name(dev));
    fprintf(stdout, "GUID = %"PRIu64".\n", ibv_get_device_guid(dev));
}

int main ()
{

    int num_ibv_devices;
    struct ibv_device **ibv_device_list;
    struct ibv_context *ibv_ctx;
    struct ibv_device_attr ibv_dev_attr;
    struct ibv_exp_device_attr ibv_exp_dev_attr;

    /* Start by finding the number of RDMA devices on this host, we
     * can then use this to construct a list of devices.*/

    ibv_fork_init();
    ibv_device_list = ibv_get_device_list(&num_ibv_devices);
    if (ibv_device_list==NULL){
        fprintf(stderr, "ibv_get_device_list (%d): %s\n", errno,
                strerror(errno));
        return errno;
    }
    fprintf(stdout,"INFO: Found %d rdma device(s) on this host.\n",
            num_ibv_devices);

    for (unsigned i=0;i<num_ibv_devices;i++)
        __ibv_pretty_print(ibv_device_list[i]);

    if(num_ibv_devices==0)
        goto freelist;

    /* Open the first device on the list. */

    ibv_ctx = ibv_open_device(ibv_device_list[0]);
    if ( ibv_ctx == NULL ){
        fprintf(stderr, "ibv_open_device (%d): %s\n", errno,
                strerror(errno));
        return errno;
    }

    /* Confirm if this device has the capability to do erasure coding
     * or not. Note that if your libibverbs is not up to date this
     * code will break during compile as the EC features currently
     * reside in the experimental section of the MOFED releases (as of
     * Jan 2016).
     */

    if ( ibv_query_device(ibv_ctx, &ibv_dev_attr) ){
        fprintf(stderr, "ibv_query_device (%d): %s\n", errno,
                strerror(errno));
        return errno;
    }
    if ( ibv_exp_query_device(ibv_ctx, &ibv_exp_dev_attr) ){
        fprintf(stderr, "ibv_exp_query_device (%d): %s\n", errno,
                strerror(errno));
        return errno;
    }
    fprintf(stdout,"0x%08x : 0x%08x\n", ibv_dev_attr.device_cap_flags,
            ibv_exp_dev_attr.exp_device_cap_flags);
    if ( !(ibv_exp_dev_attr.exp_device_cap_flags & IBV_EXP_DEVICE_EC_OFFLOAD) ){
        fprintf(stderr, "device does not support EC!\n");
        return -1;
    }

    /*
     * Now that we have got this far we know we have a HCA that
     * supports EC. We now configure the device to do some simple
     * encoding tests so we can benchmark it.
     */

close:
    ibv_close_device(ibv_ctx);
freelist:
    ibv_free_device_list(ibv_device_list);

    return 0;
}
