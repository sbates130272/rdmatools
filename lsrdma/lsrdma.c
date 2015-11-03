#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include <infiniband/verbs.h>

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

    /* Start by finding the number of RDMA devices on this host, we
     * can then use this to construct a list of devices.*/

    ibv_fork_init();
    ibv_device_list = ibv_get_device_list(&num_ibv_devices);
    if (ibv_device_list==NULL){
        fprintf(stderr, "Error (%d): %s\n", errno,
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

close:
    ibv_close_device(ibv_ctx);
freelist:
    ibv_free_device_list(ibv_device_list);

    return 0;
}
