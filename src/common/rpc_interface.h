#pragma once

#include <rpc.h>
#include <rpcndr.h>

#ifdef __cplusplus
extern "C" {
#endif

	void* __RPC_USER midl_user_allocate(size_t size);
	void __RPC_USER midl_user_free(void* p);

	extern RPC_IF_HANDLE ServiceControl_v1_0_s_ifspec;

	void StopService(void);
	long GetStatus(void);
	void Shutdown(void);

#ifdef __cplusplus
}
#endif