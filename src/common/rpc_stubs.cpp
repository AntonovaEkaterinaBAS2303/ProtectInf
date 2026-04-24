#include "rpc_stubs.h"
#include <stdlib.h>

void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

void __RPC_USER midl_user_free(void* p)
{
    free(p);
}

RPC_STATUS CreateRpcBinding(RPC_WSTR* pszStringBinding, handle_t* phBinding)
{
    RPC_STATUS status;
    RPC_WSTR pszProtoSeq = (RPC_WSTR)L"ncalrpc";
    RPC_WSTR pszEndpoint = (RPC_WSTR)L"TrayAppServiceRPC";

    status = RpcStringBindingComposeW(
        NULL,
        pszProtoSeq,
        NULL,
        pszEndpoint,
        NULL,
        pszStringBinding
    );

    if (status != RPC_S_OK) return status;

    status = RpcBindingFromStringBindingW(*pszStringBinding, phBinding);

    if (status != RPC_S_OK)
    {
        RpcStringFreeW(pszStringBinding);
        *pszStringBinding = NULL;
    }

    return status;
}

void FreeRpcBinding(RPC_WSTR* pszStringBinding, handle_t* phBinding)
{
    if (*phBinding)
    {
        RpcBindingFree(phBinding);
        *phBinding = NULL;
    }
    if (*pszStringBinding)
    {
        RpcStringFreeW(pszStringBinding);
        *pszStringBinding = NULL;
    }
}