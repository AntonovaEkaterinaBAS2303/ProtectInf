/* this ALWAYS GENERATED file contains the definitions for the interfaces */

#pragma warning( disable: 4049 )  /* more than 64k source lines */

#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif

#ifndef __rpc_h__
#define __rpc_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif 

#ifndef __ServiceControl_INTERFACE_DEFINED__
#define __ServiceControl_INTERFACE_DEFINED__

    void StopService(
        /* [in] */ handle_t IDL_handle);

    long GetStatus(
        /* [in] */ handle_t IDL_handle);

    void Shutdown(
        /* [in] */ handle_t IDL_handle);

    long GetUserInfo(
        /* [in] */ handle_t IDL_handle,
        /* [out, string] */ wchar_t** username);

    long Login(
        /* [in] */ handle_t IDL_handle,
        /* [in, string] */ const wchar_t* username,
        /* [in, string] */ const wchar_t* password);

    long Logout(
        /* [in] */ handle_t IDL_handle);

    long GetLicenseInfo(
        /* [in] */ handle_t IDL_handle,
        /* [out] */ long* daysRemaining,
        /* [out, string] */ wchar_t** expiryDate);

    long ActivateProduct(
        /* [in] */ handle_t IDL_handle,
        /* [in, string] */ const wchar_t* activationKey);

    extern RPC_IF_HANDLE ServiceControl_v1_0_c_ifspec;
    extern RPC_IF_HANDLE ServiceControl_v1_0_s_ifspec;
#endif /* __ServiceControl_INTERFACE_DEFINED__ */

#ifdef __cplusplus
}
#endif

#endif