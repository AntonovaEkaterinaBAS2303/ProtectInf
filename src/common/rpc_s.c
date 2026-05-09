/* this ALWAYS GENERATED file contains the RPC server stubs */

#pragma warning( disable: 4049 )  /* more than 64k source lines */
#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/

#include <stdlib.h>
#include "service_rpc.h"

void* __RPC_USER MIDL_user_allocate(size_t size)
{
    return malloc(size);
}

void __RPC_USER MIDL_user_free(void* p)
{
    free(p);
}


#define TYPE_FORMAT_STRING_SIZE   69                                
#define PROC_FORMAT_STRING_SIZE   277                               
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   0            

typedef struct _rpc_interface_MIDL_TYPE_FORMAT_STRING
{
    short          Pad;
    unsigned char  Format[TYPE_FORMAT_STRING_SIZE];
} rpc_interface_MIDL_TYPE_FORMAT_STRING;

typedef struct _rpc_interface_MIDL_PROC_FORMAT_STRING
{
    short          Pad;
    unsigned char  Format[PROC_FORMAT_STRING_SIZE];
} rpc_interface_MIDL_PROC_FORMAT_STRING;

typedef struct _rpc_interface_MIDL_EXPR_FORMAT_STRING
{
    long          Pad;
    unsigned char  Format[EXPR_FORMAT_STRING_SIZE];
} rpc_interface_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax =
{ {0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0} };

extern const rpc_interface_MIDL_TYPE_FORMAT_STRING rpc_interface__MIDL_TypeFormatString;
extern const rpc_interface_MIDL_PROC_FORMAT_STRING rpc_interface__MIDL_ProcFormatString;
extern const rpc_interface_MIDL_EXPR_FORMAT_STRING rpc_interface__MIDL_ExprFormatString;

/* Standard interface: ServiceControl, ver. 1.0,
   GUID={0x12345678,0x1234,0x1234,{0x12,0x34,0x12,0x34,0x56,0x78,0x9A,0xBC}} */


extern const MIDL_SERVER_INFO ServiceControl_ServerInfo;

extern const RPC_DISPATCH_TABLE ServiceControl_v1_0_DispatchTable;

static const RPC_SERVER_INTERFACE ServiceControl___RpcServerInterface =
{
sizeof(RPC_SERVER_INTERFACE),
{{0x12345678,0x1234,0x1234,{0x12,0x34,0x12,0x34,0x56,0x78,0x9A,0xBC}},{1,0}},
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
(RPC_DISPATCH_TABLE*)&ServiceControl_v1_0_DispatchTable,
0,
0,
0,
&ServiceControl_ServerInfo,
0x04000000
};
RPC_IF_HANDLE ServiceControl_v1_0_s_ifspec = (RPC_IF_HANDLE)&ServiceControl___RpcServerInterface;

extern const MIDL_STUB_DESC ServiceControl_StubDesc;

static const rpc_interface_MIDL_PROC_FORMAT_STRING rpc_interface__MIDL_ProcFormatString =
{
    0,
    {

        /* Procedure StopService */

                0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /*  2 */	NdrFcLong(0x0),	/* 0 */
                /*  6 */	NdrFcShort(0x0),	/* 0 */
                /*  8 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
                /* 10 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 12 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 14 */	NdrFcShort(0x0),	/* 0 */
                            /* 16 */	NdrFcShort(0x0),	/* 0 */
                            /* 18 */	0x40,		/* Oi2 Flags:  has ext, */
                                        0x0,		/* 0 */
                                        /* 20 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 22 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 24 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 26 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 28 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Procedure GetStatus */

    /* 30 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 32 */	NdrFcLong(0x0),	/* 0 */
                /* 36 */	NdrFcShort(0x1),	/* 1 */
                /* 38 */	NdrFcShort(0x10),	/* x86 Stack size/offset = 16 */
                /* 40 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 42 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 44 */	NdrFcShort(0x0),	/* 0 */
                            /* 46 */	NdrFcShort(0x8),	/* 8 */
                            /* 48 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x1,		/* 1 */
                                        /* 50 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 52 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 54 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 56 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 58 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Return value */

    /* 60 */	NdrFcShort(0x70),	/* Flags:  out, return, base type, */
    /* 62 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
    /* 64 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                /* Procedure Shutdown */

    /* 66 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 68 */	NdrFcLong(0x0),	/* 0 */
                /* 72 */	NdrFcShort(0x2),	/* 2 */
                /* 74 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
                /* 76 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 78 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 80 */	NdrFcShort(0x0),	/* 0 */
                            /* 82 */	NdrFcShort(0x0),	/* 0 */
                            /* 84 */	0x40,		/* Oi2 Flags:  has ext, */
                                        0x0,		/* 0 */
                                        /* 86 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 88 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 90 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 92 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 94 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Procedure GetUserInfo */

    /* 96 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 98 */	NdrFcLong(0x0),	/* 0 */
                /* 102 */	NdrFcShort(0x3),	/* 3 */
                /* 104 */	NdrFcShort(0x18),	/* x86 Stack size/offset = 24 */
                /* 106 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 108 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 110 */	NdrFcShort(0x0),	/* 0 */
                            /* 112 */	NdrFcShort(0x10),	/* 16 */
                            /* 114 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x2,		/* 2 */
                                        /* 116 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 118 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 120 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 122 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 124 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Return value */

    /* 126 */	NdrFcShort(0x70),	/* Flags:  out, return, base type, */
    /* 128 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
    /* 130 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                /* Parameter username */

    /* 132 */	NdrFcShort(0x2110),	/* Flags:  out, base type, simple ref, */
    /* 134 */	NdrFcShort(0x10),	/* x86 Stack size/offset = 16 */
    /* 136 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                /* Procedure Login */

    /* 138 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 140 */	NdrFcLong(0x0),	/* 0 */
                /* 144 */	NdrFcShort(0x4),	/* 4 */
                /* 146 */	NdrFcShort(0x20),	/* x86 Stack size/offset = 32 */
                /* 148 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 150 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 152 */	NdrFcShort(0x0),	/* 0 */
                            /* 154 */	NdrFcShort(0x18),	/* 24 */
                            /* 156 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x3,		/* 3 */
                                        /* 158 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 160 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 162 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 164 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 166 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Return value */

    /* 168 */	NdrFcShort(0x70),	/* Flags:  out, return, base type, */
    /* 170 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
    /* 172 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                /* Procedure Logout */

    /* 174 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 176 */	NdrFcLong(0x0),	/* 0 */
                /* 180 */	NdrFcShort(0x5),	/* 5 */
                /* 182 */	NdrFcShort(0x10),	/* x86 Stack size/offset = 16 */
                /* 184 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 186 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 188 */	NdrFcShort(0x0),	/* 0 */
                            /* 190 */	NdrFcShort(0x8),	/* 8 */
                            /* 192 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x1,		/* 1 */
                                        /* 194 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 196 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 198 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 200 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 202 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Procedure GetLicenseInfo */

    /* 204 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 206 */	NdrFcLong(0x0),	/* 0 */
                /* 210 */	NdrFcShort(0x6),	/* 6 */
                /* 212 */	NdrFcShort(0x18),	/* x86 Stack size/offset = 24 */
                /* 214 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 216 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 218 */	NdrFcShort(0x0),	/* 0 */
                            /* 220 */	NdrFcShort(0x10),	/* 16 */
                            /* 222 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x2,		/* 2 */
                                        /* 224 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 226 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 228 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 230 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 232 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Return value */

    /* 234 */	NdrFcShort(0x70),	/* Flags:  out, return, base type, */
    /* 236 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
    /* 238 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                /* Procedure ActivateProduct */

    /* 240 */	0x0,		/* 0 */
                0x48,		/* Old Flags:  */
                /* 242 */	NdrFcLong(0x0),	/* 0 */
                /* 246 */	NdrFcShort(0x7),	/* 7 */
                /* 248 */	NdrFcShort(0x18),	/* x86 Stack size/offset = 24 */
                /* 250 */	0x32,		/* FC_BIND_PRIMITIVE */
                            0x0,		/* 0 */
                            /* 252 */	NdrFcShort(0x0),	/* x86 Stack size/offset = 0 */
                            /* 254 */	NdrFcShort(0x0),	/* 0 */
                            /* 256 */	NdrFcShort(0x10),	/* 16 */
                            /* 258 */	0x44,		/* Oi2 Flags:  has return, has ext, */
                                        0x2,		/* 2 */
                                        /* 260 */	0xa,		/* 10 */
                                                    0x1,		/* Ext Flags:  new corr desc, */
                                                    /* 262 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 264 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 266 */	NdrFcShort(0x0),	/* 0 */
                                                    /* 268 */	NdrFcShort(0x0),	/* 0 */

                                                    /* Return value */

    /* 270 */	NdrFcShort(0x70),	/* Flags:  out, return, base type, */
    /* 272 */	NdrFcShort(0x8),	/* x86 Stack size/offset = 8 */
    /* 274 */	0x8,		/* FC_LONG */
                0x0,		/* 0 */

                0x0
            }
};

static const rpc_interface_MIDL_TYPE_FORMAT_STRING rpc_interface__MIDL_TypeFormatString =
{
    0,
    {
        NdrFcShort(0x0),	/* 0 */
        /* 2 */
                    0x11, 0x2,	/* FC_RP [alloced_on_stack] [pointer_deref] */
                    /* 4 */	NdrFcShort(0x2),	/* Offset= 2 (6) */
                    /* 6 */
                                0x13, 0x8,	/* FC_OP [simple_pointer] */
                                /* 8 */
                                            0x35, 0x8,	/* FC_WSTRING */
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
174,
204,
240
};


static const MIDL_STUB_DESC ServiceControl_StubDesc =
{
(void*)&ServiceControl___RpcServerInterface,
MIDL_user_allocate,
MIDL_user_free,
0,
0,
0,
0,
0,
rpc_interface__MIDL_TypeFormatString.Format,
1, /* -error bounds_check flag */
0x50002, /* Ndr library version */
0,
0x801026e, /* MIDL Version 8.1.622 */
0,
0,
0,  /* notify & notify_flag routine table */
0x1, /* MIDL flag */
0, /* cs routines */
0,   /* proxy/server info */
0
};

static const RPC_DISPATCH_FUNCTION ServiceControl_table[] =
{
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
NdrServerCall2,
0
};
static const RPC_DISPATCH_TABLE ServiceControl_v1_0_DispatchTable =
{
8,
(RPC_DISPATCH_FUNCTION*)ServiceControl_table
};

static const SERVER_ROUTINE ServiceControl_ServerRoutineTable[] =
{
(SERVER_ROUTINE)StopService,
(SERVER_ROUTINE)GetStatus,
(SERVER_ROUTINE)Shutdown,
(SERVER_ROUTINE)GetUserInfo,
(SERVER_ROUTINE)Login,
(SERVER_ROUTINE)Logout,
(SERVER_ROUTINE)GetLicenseInfo,
(SERVER_ROUTINE)ActivateProduct
};

static const MIDL_SERVER_INFO ServiceControl_ServerInfo =
{
&ServiceControl_StubDesc,
ServiceControl_ServerRoutineTable,
rpc_interface__MIDL_ProcFormatString.Format,
ServiceControl_FormatStringOffsetTable,
0,
0,
0,
0 };
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif