#ifndef NDIS_SHARED_HEAD_H
#define	NDIS_SHARED_HEAD_H

#define	NDIS60	1	//启用ndis.h中的ndis6.0相关的定义
#include	<ntddk.h>
#include	<wdmsec.h>
#include	<ndis.h>
#include	"ioctl.h"

//宏定义

#define	POOL_TAG	'hqsb'

//
//  Send packet pool bounds
//
#define MIN_SEND_PACKET_POOL_SIZE    20
#define MAX_SEND_PACKET_POOL_SIZE    400
//
//  Receive packet pool bounds
//
#define MIN_RECV_PACKET_POOL_SIZE    4
#define MAX_RECV_PACKET_POOL_SIZE    20

#define NDIS_PACKET_FILTER  (NDIS_PACKET_TYPE_DIRECTED|    \
                              NDIS_PACKET_TYPE_MULTICAST|   \
                              NDIS_PACKET_TYPE_BROADCAST)


#define NDIS_STATUS_TO_NT_STATUS(_NdisStatus, _pNtStatus)                           \
{                                                                                   \
    /*                                                                              \
     *  The following NDIS status codes map directly to NT status codes.            \
     */                                                                             \
    if (((NDIS_STATUS_SUCCESS == (_NdisStatus)) ||                                  \
        (NDIS_STATUS_PENDING == (_NdisStatus)) ||                                   \
        (NDIS_STATUS_BUFFER_OVERFLOW == (_NdisStatus)) ||                           \
        (NDIS_STATUS_FAILURE == (_NdisStatus)) ||                                   \
        (NDIS_STATUS_RESOURCES == (_NdisStatus)) ||                                 \
        (NDIS_STATUS_NOT_SUPPORTED == (_NdisStatus))))                              \
    {                                                                               \
        *(_pNtStatus) = (NTSTATUS)(_NdisStatus);                                    \
    }                                                                               \
    else if (NDIS_STATUS_BUFFER_TOO_SHORT == (_NdisStatus))                         \
    {                                                                               \
        /*                                                                          \
         *  The above NDIS status codes require a little special casing.            \
         */                                                                         \
        *(_pNtStatus) = STATUS_BUFFER_TOO_SMALL;                                    \
    }                                                                               \
    else if (NDIS_STATUS_INVALID_LENGTH == (_NdisStatus))                           \
    {                                                                               \
        *(_pNtStatus) = STATUS_INVALID_BUFFER_SIZE;                                 \
    }                                                                               \
    else if (NDIS_STATUS_INVALID_DATA == (_NdisStatus))                             \
    {                                                                               \
        *(_pNtStatus) = STATUS_INVALID_PARAMETER;                                   \
    }                                                                               \
    else if (NDIS_STATUS_ADAPTER_NOT_FOUND == (_NdisStatus))                        \
    {                                                                               \
        *(_pNtStatus) = STATUS_NO_MORE_ENTRIES;                                     \
    }                                                                               \
    else if (NDIS_STATUS_ADAPTER_NOT_READY == (_NdisStatus))                        \
    {                                                                               \
        *(_pNtStatus) = STATUS_DEVICE_NOT_READY;                                    \
    }                                                                               \
    else                                                                            \
    {                                                                               \
        *(_pNtStatus) = STATUS_UNSUCCESSFUL;                                        \
    }                                                                               \
}

//
//  ProtocolReserved in sent packets. We save a pointer to the IRP
//  that generated the send.
//
//  The RefCount is used to determine when to free the packet back
//  to its pool. It is used to synchronize between a thread completing
//  a send and a thread attempting to cancel a send.
//
typedef struct _NDIS_SEND_PACKET_RSVD
{
	PIRP                    pIrp;
	ULONG                   RefCount;
} NDIS_SEND_PACKET_RSVD , *PNDIS_SEND_PACKET_RSVD;

//
//  ProtocolReserved in received packets: we link these
//  packets up in a queue waiting for Read IRPs.
//
typedef struct _NDIS_RECV_PACKET_RSVD
{
	LIST_ENTRY              Link;
	//PNDIS_BUFFER            pOriginalBuffer;    // used if we had to partial-map

} NDIS_RECV_PACKET_RSVD , *PNDIS_RECV_PACKET_RSVD;
#define MAX_RECV_QUEUE_SIZE          4


//对打开上下文Flags的操作
#define NDIS_SET_FLAGS(_FlagsVar, _Mask, _BitsToSet)    \
        (_FlagsVar) = ((_FlagsVar) & ~(_Mask)) | (_BitsToSet)

#define NDIS_TEST_FLAGS(_FlagsVar, _Mask, _BitsToCheck)    \
        (((_FlagsVar) & (_Mask)) == (_BitsToCheck))

//打开上下文的Bind Flags
#define NDIS_BIND_IDLE             0x00000000//空闲状态,暂未绑定
#define NDIS_BIND_OPENING          0x00000001//正在绑定状态
#define NDIS_BIND_FAILED           0x00000002
#define NDIS_BIND_ACTIVE           0x00000004
#define NDIS_BIND_CLOSING          0x00000008
#define NDIS_BIND_FLAGS_MASK       0x0000000F  // Bind_Flags的掩码

//打开上下文的Unbind Flags
#define NDIS_UNBIND_RECEIVED       0x10000000  // Seen NDIS Unbind?
#define NDIS_UNBIND_FLAGS_MASK     0x10000000

//链路连接状态Flags
#define NDIS_MEDIA_CONNECTED       0x00000000
#define NDIS_MEDIA_DISCONNECTED    0x00000200
#define NDIS_MEDIA_FLAGS_MASK      0x00000200

//打开状态
#define NDIS_OPEN_IDLE             0x00000000	//打开空闲状态
#define NDIS_OPEN_ACTIVE           0x00000010	//文件对象和网卡设备关联后的激活状态
#define NDIS_OPEN_FLAGS_MASK       0x000000F0  // State of the I/O open

//重置状态
#define NDIS_RESET_IN_PROGRESS     0x00000100
#define NDIS_NOT_RESETTING         0x00000000
#define NDIS_RESET_FLAGS_MASK      0x00000100


//全局结构体
typedef struct _NDISPROT_GLOBALS
{
	PDRIVER_OBJECT          pDriverObject;
	PDEVICE_OBJECT          pControlDeviceObject;
	NDIS_HANDLE             NdisProtocolHandle;
	UCHAR                   PartialCancelId;    // for cancelling sends
	ULONG                   LocalCancelId;
	LIST_ENTRY              OpenList;           // of OPEN_CONTEXT structures
	NDIS_SPIN_LOCK          SpinLock;         // to protect the above
	NDIS_EVENT              BindCompleteEvent;      // have we seen NetEventBindsComplete?

} NDISPROT_GLOBALS , *PNDISPROT_GLOBALS;
NDISPROT_GLOBALS	Globals;	//全局变量

#define NPROT_MAC_ADDR_LEN            6
//
//  The Open Context represents an open of our device object.
//  We allocate this on processing a BindAdapter from NDIS,
//  and free it when all references (see below) to it are gone.
//
//  Binding/unbinding to an NDIS device:
//
//  On processing a BindAdapter call from NDIS, we set up a binding
//  to the specified NDIS device (miniport). This binding is
//  torn down when NDIS asks us to Unbind by calling
//  our UnbindAdapter handler.
//
//  Receiving data:
//
//  While an NDIS binding exists, read IRPs are queued on this
//  structure, to be processed when packets are received.
//  If data arrives in the absense of a pended read IRP, we
//  queue it, to the extent of one packet, i.e. we save the
//  contents of the latest packet received. We fail read IRPs
//  received when no NDIS binding exists (or is in the process
//  of being torn down).
//
//  Sending data:
//
//  Write IRPs are used to send data. Each write IRP maps to
//  a single NDIS packet. Packet send-completion is mapped to
//  write IRP completion. We use NDIS 5.1 CancelSend to support
//  write IRP cancellation. Write IRPs that arrive when we don't
//  have an active NDIS binding are failed.
//
//  Reference count:
//
//  The following are long-lived references:
//  OPEN_DEVICE ioctl (goes away on processing a Close IRP)
//  Pended read IRPs
//  Queued received packets
//  Uncompleted write IRPs (outstanding sends)
//  Existence of NDIS binding
//
typedef struct _NDIS_OPEN_CONTEXT
{
	LIST_ENTRY              Link;           // Link into global list
	ULONG                   Flags;          // State information
	ULONG                   RefCount;
	NDIS_SPIN_LOCK          Lock;

	PFILE_OBJECT            pFileObject;    // Set on OPEN_DEVICE

	NDIS_HANDLE             BindingHandle;
	NDIS_HANDLE             SendNetBufferListPool;
	NDIS_HANDLE             SendBufferPool;
	NDIS_HANDLE             RecvNetBufferListPool;
	NDIS_HANDLE             RecvNetBufferPool;
	ULONG                   MacOptions;
	ULONG                   MaxFrameSize;

	LIST_ENTRY              PendedWrites;   // pended Write IRPs
	ULONG                   PendedSendCount;

	LIST_ENTRY              PendedReads;    // pended Read IRPs
	ULONG                   PendedReadCount;
	LIST_ENTRY              RecvPktQueue;   // queued rcv packets
	ULONG                   RecvPktCount;

	NET_DEVICE_POWER_STATE  PowerState;
	NDIS_EVENT              PoweredUpEvent; // signalled iff PowerState is D0
	NDIS_STRING             DeviceName;     // used in NdisOpenAdapter
	NDIS_STRING             DeviceDescr;    // friendly name

	NDIS_STATUS             BindStatus;     // for Open/CloseAdapter
	NDIS_EVENT              BindEvent;      // for Open/CloseAdapter

	BOOLEAN                 bRunningOnWin9x;// TRUE if Win98/SE/ME, FALSE if NT

	ULONG                   oc_sig;         // Signature for sanity

	UCHAR                   CurrentAddress[NPROT_MAC_ADDR_LEN];
	PIRP                  StatusIndicationIrp;
} NDIS_OPEN_CONTEXT , *PNDIS_OPEN_CONTEXT;

//
//  请求上下文
//
typedef struct _NDIS_REQUEST_CONTEXT
{
	NDIS_OID_REQUEST        NdisRequest;
	NDIS_EVENT              ReqEvent;	//用来等待请求完成
	ULONG                   Status;		//用来保存请求结果
} NDIS_REQUEST_CONTEXT , *PNDIS_REQUEST_CONTEXT;

//
//	数据包以太头结构
//
typedef	struct _NDIS_ETH_HEADER
{
	UCHAR	DestinationAddress[NPROT_MAC_ADDR_LEN];
	UCHAR	SourceAddress[NPROT_MAC_ADDR_LEN];
	USHORT	EthType;

}NDIS_ETH_HEADER , *PNDIS_ETH_HEADER;


//  Structure to go with IOCTL_NDISPROT_INDICATE_STATUS.
//  NDISPROT copies the status indicated by the NIC and
//  also the data indicated in the StatusBuffer.
//
typedef struct _NDIS_INDICATE_STATUS
{
	ULONG            IndicatedStatus;        // NDIS_STATUS
	ULONG            StatusBufferOffset;    // from start of this struct
	ULONG            StatusBufferLength;    // in bytes
} NDIS_INDICATE_STATUS , *PNDIS_INDICATE_STATUS;






//
//	函数声明
//
PNDIS_OPEN_CONTEXT	_LookupOpenContext(
	IN	PWCHAR	_pDeviceName ,
	IN	ULONG	_NameLength
);

VOID	_NdisDereferenceOpenContext(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);

VOID	_NdisCancelRead(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

NTSTATUS	_NdisCloseDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

NTSTATUS	_NdisCreateDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

NTSTATUS	_NdisReadDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

NTSTATUS	_NdisWriteDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

VOID	_OpenAdapterComplete(
	IN	NDIS_HANDLE	_pProtocolBindingContext ,	//调用OpenAdaterEx时传入的OpenContext
	IN	NDIS_STATUS	_NdisStatus
);

VOID	_RequestComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNDIS_OID_REQUEST	_pNdisRequest ,
	IN	NDIS_STATUS	_Status
);

VOID	_SendComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList ,
	IN	ULONG		_SendCompleteFlags
);


VOID	_NdisServiceReads(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);

VOID	_ReceiveNetBufferList(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList ,
	IN	NDIS_PORT_NUMBER	_PortNumber ,
	IN	ULONG			_NumberOfNetBufferList ,
	IN	ULONG			ReceiveFlags
);



NDIS_STATUS	_BindAdapter(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNDIS_BIND_PARAMETERS	_pBindParameters ,
	IN	NDIS_HANDLE			_BindContext
);

NDIS_STATUS	_BindAdapterHandlerEx(
	IN	NDIS_HANDLE	_ProtocolDriverContext ,
	IN	NDIS_HANDLE	_BindContext ,
	IN	PNDIS_BIND_PARAMETERS	_pBindParameters
);

NDIS_STATUS	_NdisDoRequest(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_REQUEST_TYPE	_RequestType ,
	IN	NDIS_OID	_Oid ,
	IN	PVOID	_pInformationBuffer ,
	IN	ULONG	_InformationBufferLength ,
	OUT	PULONG	_pBytesProcessed
);

VOID	_UnbindAdapter(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);

NDIS_STATUS	_UnbindAdapterHandlerEx(
	IN	NDIS_HANDLE		_UnbindContext ,
	IN	NDIS_HANDLE		_ProtocolBindingContext
);

NTSTATUS	_NdisDeviceControlDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

NTSTATUS	_NdisSetDevice(
	IN	PWCHAR	_pDeviceName ,
	IN	ULONG	_DeviceNameLength ,
	IN	PFILE_OBJECT	_pFileObject ,
	OUT	PNDIS_OPEN_CONTEXT*	_ppOpenContext
);

NDIS_STATUS	_NdisValidateOpenAndDoRequest(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_REQUEST_TYPE	_RequestType ,
	IN	NDIS_OID	_Oid ,
	IN	PVOID	_pInformationBuffer ,
	IN	ULONG	_InformationBufferLength ,
	OUT	PULONG	_pBytesProcessed ,
	IN	BOOLEAN	_bWaitForPowerOn
);

VOID	_CancelPendingReads(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);

VOID	_FreeReceiveNetBufferList(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList
);

VOID	_ServiceIndicateStatusIrp(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_STATUS	_GeneralStatus ,
	IN	PVOID	_pStatusBuffer ,
	IN	UINT	_StatusBufferSize ,
	IN	BOOLEAN	_bCancel
);

VOID	_FlushReceiveQueue(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);

NTSTATUS	_NdisCleanupDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
);

VOID	_NdisUnload(
	IN	PDRIVER_OBJECT	_pDriverObject
);

VOID	_CloseAdapterComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
);

VOID	_ReceiveComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
);

VOID	_StatusHandler(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	 PNDIS_STATUS_INDICATION StatusIndication
);

VOID	_StatusComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
);

VOID	_ResetComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	NDIS_STATUS	_Status
);

VOID	_WaitForPendingIrp(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	BOOLEAN	_bDoCancelReads
);

NDIS_STATUS	_QueryBinding(
	IN	PUCHAR	_pBuffer ,
	IN	ULONG	_InputLength ,
	IN	ULONG	_OutputLength ,
	OUT	PULONG	_pBytesReturned
);

NDIS_STATUS	_PnpEventHandler(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_PNP_EVENT_NOTIFICATION _pNetPnPEventNotification
);

VOID	_QueueNetBufferList(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList
);

VOID	_FreeContextResoureces(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
);



#endif // !NDIS_SHARED_HEAD_H