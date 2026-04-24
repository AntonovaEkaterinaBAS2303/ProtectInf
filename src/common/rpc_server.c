#include "rpc_interface.h"
#include <stdlib.h>

RPC_IF_HANDLE ServiceControl_v1_0_s_ifspec = NULL;

void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

void __RPC_USER midl_user_free(void* p)
{
    free(p);
}