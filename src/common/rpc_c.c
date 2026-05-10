

/* this ALWAYS GENERATED file contains the RPC client stubs */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 06:14:07 2038
 */
/* Compiler settings for rpc_interface.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_AMD64)


#pragma warning( disable: 4049 )  /* more than 64k source lines */
#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/

#include <string.h>

#include "service_rpc.h"

#define TYPE_FORMAT_STRING_SIZE   61                                
#define PROC_FORMAT_STRING_SIZE   589                               
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   0            

typedef struct _rpc_interface_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } rpc_interface_MIDL_TYPE_FORMAT_STRING;

typedef struct _rpc_interface_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } rpc_interface_MIDL_PROC_FORMAT_STRING;

typedef struct _rpc_interface_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } rpc_interface_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif


extern const rpc_interface_MIDL_TYPE_FORMAT_STRING rpc_interface__MIDL_TypeFormatString;
extern const rpc_interface_MIDL_PROC_FORMAT_STRING rpc_interface__MIDL_ProcFormatString;
extern const rpc_interface_MIDL_EXPR_FORMAT_STRING rpc_interface__MIDL_ExprFormatString;

#define GENERIC_BINDING_TABLE_SIZE   0            


/* Standard interface: ServiceControl, ver. 1.0,
   GUID={0x12345678,0x1234,0x1234,{0x12,0x34,0x12,0x34,0x56,0x78,0x9A,0xBC}} */



static const RPC_CLIENT_INTERFACE ServiceControl___RpcClientInterface =
    {
    sizeof(RPC_CLIENT_INTERFACE),
    {{0x12345678,0x1234,0x1234,{0x12,0x34,0x12,0x34,0x56,0x78,0x9A,0xBC}},{1,0}},
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    0,
    0,
    0,
    0,
    0x00000000
    };
RPC_IF_HANDLE ServiceControl_v1_0_c_ifspec = (RPC_IF_HANDLE)& ServiceControl___RpcClientInterface;
#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC ServiceControl_StubDesc;
#ifdef __cplusplus
}
#endif

static RPC_BINDING_HANDLE ServiceControl__MIDL_AutoBindHandle;


void StopService( 
    /* [in] */ handle_t hBinding)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[0],
                  hBinding);
    
}


long GetStatus( 
    /* [in] */ handle_t hBinding)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[30],
                  hBinding);
    return ( long  )_RetVal.Simple;
    
}


void Shutdown( 
    /* [in] */ handle_t hBinding)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[66],
                  hBinding);
    
}


long GetUserInfo( 
    /* [in] */ handle_t hBinding,
    /* [string][out] */ wchar_t **username)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[96],
                  hBinding,
                  username);
    return ( long  )_RetVal.Simple;
    
}


long Login( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *username,
    /* [string][in] */ const wchar_t *password)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[138],
                  hBinding,
                  username,
                  password);
    return ( long  )_RetVal.Simple;
    
}


long Logout( 
    /* [in] */ handle_t IDL_handle)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[186],
                  IDL_handle);
    return ( long  )_RetVal.Simple;
    
}


long GetLicenseInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ long *daysRemaining,
    /* [string][out] */ wchar_t **expiryDate)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[222],
                  hBinding,
                  daysRemaining,
                  expiryDate);
    return ( long  )_RetVal.Simple;
    
}


long ActivateProduct( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *activationKey)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[270],
                  hBinding,
                  activationKey);
    return ( long  )_RetVal.Simple;
    
}


long ScanFile( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *filePath,
    /* [out] */ ScanResultData *result)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[312],
                  hBinding,
                  filePath,
                  result);
    return ( long  )_RetVal.Simple;
    
}


long ScanDirectory( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *dirPath,
    /* [out] */ ScanResultData *results,
    /* [out] */ long *resultCount)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[360],
                  hBinding,
                  dirPath,
                  results,
                  resultCount);
    return ( long  )_RetVal.Simple;
    
}


long GetAvDbInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ AvDbInfo *info)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[414],
                  hBinding,
                  info);
    return ( long  )_RetVal.Simple;
    
}


long AddMonitoredDir( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *dirPath,
    /* [in] */ long recursive)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[456],
                  hBinding,
                  dirPath,
                  recursive);
    return ( long  )_RetVal.Simple;
    
}


long RemoveMonitoredDir( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *dirPath)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[504],
                  hBinding,
                  dirPath);
    return ( long  )_RetVal.Simple;
    
}


long GetMonitoredDirs( 
    /* [in] */ handle_t hBinding,
    /* [string][out] */ wchar_t **dirList)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&ServiceControl_StubDesc,
                  (PFORMAT_STRING) &rpc_interface__MIDL_ProcFormatString.Format[546],
                  hBinding,
                  dirList);
    return ( long  )_RetVal.Simple;
    
}


#if !defined(__RPC_WIN64__)
#error  Invalid build platform for this stub.
#endif

static const rpc_interface_MIDL_PROC_FORMAT_STRING rpc_interface__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure StopService */

			0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x0 ),	/* 0 */
/*  8 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 10 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 12 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 14 */	NdrFcShort( 0x0 ),	/* 0 */
/* 16 */	NdrFcShort( 0x0 ),	/* 0 */
/* 18 */	0x40,		/* Oi2 Flags:  has ext, */
			0x0,		/* 0 */
/* 20 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x0 ),	/* 0 */
/* 26 */	NdrFcShort( 0x0 ),	/* 0 */
/* 28 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Procedure GetStatus */

/* 30 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 32 */	NdrFcLong( 0x0 ),	/* 0 */
/* 36 */	NdrFcShort( 0x1 ),	/* 1 */
/* 38 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 40 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 42 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 44 */	NdrFcShort( 0x0 ),	/* 0 */
/* 46 */	NdrFcShort( 0x8 ),	/* 8 */
/* 48 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 50 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 52 */	NdrFcShort( 0x0 ),	/* 0 */
/* 54 */	NdrFcShort( 0x0 ),	/* 0 */
/* 56 */	NdrFcShort( 0x0 ),	/* 0 */
/* 58 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 60 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 62 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 64 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Shutdown */

/* 66 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 68 */	NdrFcLong( 0x0 ),	/* 0 */
/* 72 */	NdrFcShort( 0x2 ),	/* 2 */
/* 74 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 76 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 78 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 80 */	NdrFcShort( 0x0 ),	/* 0 */
/* 82 */	NdrFcShort( 0x0 ),	/* 0 */
/* 84 */	0x40,		/* Oi2 Flags:  has ext, */
			0x0,		/* 0 */
/* 86 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 88 */	NdrFcShort( 0x0 ),	/* 0 */
/* 90 */	NdrFcShort( 0x0 ),	/* 0 */
/* 92 */	NdrFcShort( 0x0 ),	/* 0 */
/* 94 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Procedure GetUserInfo */

/* 96 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 98 */	NdrFcLong( 0x0 ),	/* 0 */
/* 102 */	NdrFcShort( 0x3 ),	/* 3 */
/* 104 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 106 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 108 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 110 */	NdrFcShort( 0x0 ),	/* 0 */
/* 112 */	NdrFcShort( 0x8 ),	/* 8 */
/* 114 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 116 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 118 */	NdrFcShort( 0x0 ),	/* 0 */
/* 120 */	NdrFcShort( 0x0 ),	/* 0 */
/* 122 */	NdrFcShort( 0x0 ),	/* 0 */
/* 124 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter username */

/* 126 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 128 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 130 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Return value */

/* 132 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 134 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 136 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Login */

/* 138 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 140 */	NdrFcLong( 0x0 ),	/* 0 */
/* 144 */	NdrFcShort( 0x4 ),	/* 4 */
/* 146 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 148 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 150 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 152 */	NdrFcShort( 0x0 ),	/* 0 */
/* 154 */	NdrFcShort( 0x8 ),	/* 8 */
/* 156 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 158 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 160 */	NdrFcShort( 0x0 ),	/* 0 */
/* 162 */	NdrFcShort( 0x0 ),	/* 0 */
/* 164 */	NdrFcShort( 0x0 ),	/* 0 */
/* 166 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter username */

/* 168 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 170 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 172 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Parameter password */

/* 174 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 176 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 178 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Return value */

/* 180 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 182 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 184 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Logout */

/* 186 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 188 */	NdrFcLong( 0x0 ),	/* 0 */
/* 192 */	NdrFcShort( 0x5 ),	/* 5 */
/* 194 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 196 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 198 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 200 */	NdrFcShort( 0x0 ),	/* 0 */
/* 202 */	NdrFcShort( 0x8 ),	/* 8 */
/* 204 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 206 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 208 */	NdrFcShort( 0x0 ),	/* 0 */
/* 210 */	NdrFcShort( 0x0 ),	/* 0 */
/* 212 */	NdrFcShort( 0x0 ),	/* 0 */
/* 214 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 216 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 218 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 220 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure GetLicenseInfo */

/* 222 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 224 */	NdrFcLong( 0x0 ),	/* 0 */
/* 228 */	NdrFcShort( 0x6 ),	/* 6 */
/* 230 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 232 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 234 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 236 */	NdrFcShort( 0x0 ),	/* 0 */
/* 238 */	NdrFcShort( 0x24 ),	/* 36 */
/* 240 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x3,		/* 3 */
/* 242 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 244 */	NdrFcShort( 0x0 ),	/* 0 */
/* 246 */	NdrFcShort( 0x0 ),	/* 0 */
/* 248 */	NdrFcShort( 0x0 ),	/* 0 */
/* 250 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter daysRemaining */

/* 252 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 254 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 256 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter expiryDate */

/* 258 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 260 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 262 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Return value */

/* 264 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 266 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 268 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ActivateProduct */

/* 270 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 272 */	NdrFcLong( 0x0 ),	/* 0 */
/* 276 */	NdrFcShort( 0x7 ),	/* 7 */
/* 278 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 280 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 282 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 284 */	NdrFcShort( 0x0 ),	/* 0 */
/* 286 */	NdrFcShort( 0x8 ),	/* 8 */
/* 288 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 290 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 292 */	NdrFcShort( 0x0 ),	/* 0 */
/* 294 */	NdrFcShort( 0x0 ),	/* 0 */
/* 296 */	NdrFcShort( 0x0 ),	/* 0 */
/* 298 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter activationKey */

/* 300 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 302 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 304 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Return value */

/* 306 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 308 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 310 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ScanFile */

/* 312 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 314 */	NdrFcLong( 0x0 ),	/* 0 */
/* 318 */	NdrFcShort( 0x8 ),	/* 8 */
/* 320 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 322 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 324 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 326 */	NdrFcShort( 0x0 ),	/* 0 */
/* 328 */	NdrFcShort( 0x8 ),	/* 8 */
/* 330 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 332 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 334 */	NdrFcShort( 0x0 ),	/* 0 */
/* 336 */	NdrFcShort( 0x0 ),	/* 0 */
/* 338 */	NdrFcShort( 0x0 ),	/* 0 */
/* 340 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter filePath */

/* 342 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 344 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 346 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Parameter result */

/* 348 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 350 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 352 */	NdrFcShort( 0x16 ),	/* Type Offset=22 */

	/* Return value */

/* 354 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 356 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 358 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ScanDirectory */

/* 360 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 362 */	NdrFcLong( 0x0 ),	/* 0 */
/* 366 */	NdrFcShort( 0x9 ),	/* 9 */
/* 368 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 370 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 372 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 374 */	NdrFcShort( 0x0 ),	/* 0 */
/* 376 */	NdrFcShort( 0x24 ),	/* 36 */
/* 378 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 380 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 382 */	NdrFcShort( 0x0 ),	/* 0 */
/* 384 */	NdrFcShort( 0x0 ),	/* 0 */
/* 386 */	NdrFcShort( 0x0 ),	/* 0 */
/* 388 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter dirPath */

/* 390 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 392 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 394 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Parameter results */

/* 396 */	NdrFcShort( 0x6113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=24 */
/* 398 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 400 */	NdrFcShort( 0x16 ),	/* Type Offset=22 */

	/* Parameter resultCount */

/* 402 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 404 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 406 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 408 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 410 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 412 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure GetAvDbInfo */

/* 414 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 416 */	NdrFcLong( 0x0 ),	/* 0 */
/* 420 */	NdrFcShort( 0xa ),	/* 10 */
/* 422 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 424 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 426 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 428 */	NdrFcShort( 0x0 ),	/* 0 */
/* 430 */	NdrFcShort( 0x8 ),	/* 8 */
/* 432 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 434 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 436 */	NdrFcShort( 0x0 ),	/* 0 */
/* 438 */	NdrFcShort( 0x0 ),	/* 0 */
/* 440 */	NdrFcShort( 0x0 ),	/* 0 */
/* 442 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter info */

/* 444 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 446 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 448 */	NdrFcShort( 0x2c ),	/* Type Offset=44 */

	/* Return value */

/* 450 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 452 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 454 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure AddMonitoredDir */

/* 456 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 458 */	NdrFcLong( 0x0 ),	/* 0 */
/* 462 */	NdrFcShort( 0xb ),	/* 11 */
/* 464 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 466 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 468 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 470 */	NdrFcShort( 0x8 ),	/* 8 */
/* 472 */	NdrFcShort( 0x8 ),	/* 8 */
/* 474 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 476 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 478 */	NdrFcShort( 0x0 ),	/* 0 */
/* 480 */	NdrFcShort( 0x0 ),	/* 0 */
/* 482 */	NdrFcShort( 0x0 ),	/* 0 */
/* 484 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter dirPath */

/* 486 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 488 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 490 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Parameter recursive */

/* 492 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 494 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 496 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 498 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 500 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 502 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure RemoveMonitoredDir */

/* 504 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 506 */	NdrFcLong( 0x0 ),	/* 0 */
/* 510 */	NdrFcShort( 0xc ),	/* 12 */
/* 512 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 514 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 516 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 518 */	NdrFcShort( 0x0 ),	/* 0 */
/* 520 */	NdrFcShort( 0x8 ),	/* 8 */
/* 522 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 524 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 526 */	NdrFcShort( 0x0 ),	/* 0 */
/* 528 */	NdrFcShort( 0x0 ),	/* 0 */
/* 530 */	NdrFcShort( 0x0 ),	/* 0 */
/* 532 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter dirPath */

/* 534 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 536 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 538 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Return value */

/* 540 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 542 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 544 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure GetMonitoredDirs */

/* 546 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 548 */	NdrFcLong( 0x0 ),	/* 0 */
/* 552 */	NdrFcShort( 0xd ),	/* 13 */
/* 554 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 556 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 558 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 560 */	NdrFcShort( 0x0 ),	/* 0 */
/* 562 */	NdrFcShort( 0x8 ),	/* 8 */
/* 564 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 566 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 568 */	NdrFcShort( 0x0 ),	/* 0 */
/* 570 */	NdrFcShort( 0x0 ),	/* 0 */
/* 572 */	NdrFcShort( 0x0 ),	/* 0 */
/* 574 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter dirList */

/* 576 */	NdrFcShort( 0x2013 ),	/* Flags:  must size, must free, out, srv alloc size=8 */
/* 578 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 580 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Return value */

/* 582 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 584 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 586 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

			0x0
        }
    };

static const rpc_interface_MIDL_TYPE_FORMAT_STRING rpc_interface__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	
			0x11, 0x14,	/* FC_RP [alloced_on_stack] [pointer_deref] */
/*  4 */	NdrFcShort( 0x2 ),	/* Offset= 2 (6) */
/*  6 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/*  8 */	
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */
/* 10 */	
			0x11, 0x8,	/* FC_RP [simple_pointer] */
/* 12 */	
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */
/* 14 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 16 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */
/* 18 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 20 */	NdrFcShort( 0x2 ),	/* Offset= 2 (22) */
/* 22 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 24 */	NdrFcShort( 0x18 ),	/* 24 */
/* 26 */	NdrFcShort( 0x0 ),	/* 0 */
/* 28 */	NdrFcShort( 0x8 ),	/* Offset= 8 (36) */
/* 30 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 32 */	0x36,		/* FC_POINTER */
			0x8,		/* FC_LONG */
/* 34 */	0x40,		/* FC_STRUCTPAD4 */
			0x5b,		/* FC_END */
/* 36 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 38 */	
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */
/* 40 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 42 */	NdrFcShort( 0x2 ),	/* Offset= 2 (44) */
/* 44 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 46 */	NdrFcShort( 0x10 ),	/* 16 */
/* 48 */	NdrFcShort( 0x0 ),	/* 0 */
/* 50 */	NdrFcShort( 0x6 ),	/* Offset= 6 (56) */
/* 52 */	0x36,		/* FC_POINTER */
			0x8,		/* FC_LONG */
/* 54 */	0x40,		/* FC_STRUCTPAD4 */
			0x5b,		/* FC_END */
/* 56 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 58 */	
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */

			0x0
        }
    };

static const unsigned short ServiceControl_FormatStringOffsetTable[] =
    {
    0,
    30,
    66,
    96,
    138,
    186,
    222,
    270,
    312,
    360,
    414,
    456,
    504,
    546
    };


#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC ServiceControl_StubDesc = 
    {
    (void *)& ServiceControl___RpcClientInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    &ServiceControl__MIDL_AutoBindHandle,
    0,
    0,
    0,
    0,
    rpc_interface__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x50002, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    0x1, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* defined(_M_AMD64)*/

