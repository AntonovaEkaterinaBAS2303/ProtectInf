#pragma once

#include <windows.h>
#include <rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

	void* __RPC_USER midl_user_allocate(size_t size);
	void __RPC_USER midl_user_free(void* p);
	RPC_STATUS CreateRpcBinding(RPC_WSTR* pszStringBinding, handle_t* phBinding);
	void FreeRpcBinding(RPC_WSTR* pszStringBinding, handle_t* phBinding);

#ifdef __cplusplus
}
#endif