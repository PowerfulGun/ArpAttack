/*
	处理驱动收到的IRP_MJ_DEVICE_CONTROL请求
*/
#include	"ndis_shared_head.h"
#include	"ioctl.h"



/*
该函数处理IRP_MJ_DEVICE_CONTROL请求
*/
NTSTATUS	_NdisDeviceControlDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	PIO_STACK_LOCATION	pIrpStack =
		IoGetCurrentIrpStackLocation( _pIrp );
	PNDIS_OPEN_CONTEXT	pOpenContext;
	ULONG	BytesReturned = 0;
	ULONG	ControlCode;
	NTSTATUS	status;
	NDIS_STATUS	NdisStatus;

	KdPrint( ("_NdisDeviceControlDispatch:\n") );

	ControlCode =
		pIrpStack->Parameters.DeviceIoControl.IoControlCode;
	pOpenContext = pIrpStack->FileObject->FsContext;

	switch (ControlCode)
	{
		case IOCTL_NDIS_BIND_WAIT:
			ASSERT( (ControlCode & 0x3) == METHOD_BUFFERED );

			//等待全局绑定事件,这个事件会在绑定完成后被设置
			if (
				NdisWaitEvent( &Globals.BindCompleteEvent , 5000 )
				)
			{
				status = STATUS_SUCCESS;
				KdPrint( ("[IOCTL_NDIS_BIND_WAIT]: Success return\n") );
			}
			else
			{
				status = STATUS_TIMEOUT;
				KdPrint( ("[IOCTL_NDIS_BIND_WAIT]:Wait timeout\n") );
			}
			break;

		case IOCTL_NDIS_QUERY_BINDING:
			ASSERT( (ControlCode & 0x3) == METHOD_BUFFERED );

			KdPrint( ("[IOCTL_NDIS_QUERY_BINDING]\n") );

			NdisStatus = _QueryBinding(
				_pIrp->AssociatedIrp.SystemBuffer ,
				pIrpStack->Parameters.DeviceIoControl.InputBufferLength ,
				pIrpStack->Parameters.DeviceIoControl.OutputBufferLength ,
				&BytesReturned );

			NDIS_STATUS_TO_NT_STATUS( NdisStatus , &status );
			break;

		case IOCTL_NDIS_SET_DEVICE:
			if (pOpenContext != NULL)
			{
				KdPrint( ("[IOCTL_NDIS_SET_DEVICE]:FileObject is already associated with device\n") );
				status = STATUS_DEVICE_BUSY;
				break;
			}

			status = _NdisSetDevice(
				_pIrp->AssociatedIrp.SystemBuffer ,
				pIrpStack->Parameters.DeviceIoControl.InputBufferLength ,
				pIrpStack->FileObject ,
				&pOpenContext );
			if (NT_SUCCESS( status ))
			{
				KdPrint( ("[IOCTL_NDIS_SET_DEVICE]:_NdisSetDevice:Success\n") );
			}
			break;
	}

	if (status != STATUS_PENDING)
	{
		_pIrp->IoStatus.Information = BytesReturned;
		_pIrp->IoStatus.Status = status;
		IoCompleteRequest( _pIrp , IO_NO_INCREMENT );
	}

	return	status;
}


/*
该函数给文件对象指定关联一个网卡设备
1.从输入缓冲区中拿到设备名
2.通过设备名去寻找对应的打开上下文
3.如果找到了,就保存到FileObject->FsContext中
参数:
pDeviceName	设备名
DeviceNameLength	设备名长度
pFIleObject	文件对象指针
ppOpenContext	用来返回网卡设备的打开上下文
*/
NTSTATUS	_NdisSetDevice(
	IN	PWCHAR	_pDeviceName ,
	IN	ULONG	_DeviceNameLength ,
	IN	PFILE_OBJECT	_pFileObject ,
	OUT	PNDIS_OPEN_CONTEXT*	_ppOpenContext
)
{
	NTSTATUS	status;
	NDIS_STATUS	NdisStatus;
	PNDIS_OPEN_CONTEXT	pOpenContext , pOpenContextInFsContext;
	ULONG	PacketFilter;
	ULONG	BytesProcessed;

	do
	{
		//根据设备名找到打开上下文,调用会增加打开上下文的引用
		pOpenContext = _LookupOpenContext(
			_pDeviceName , _DeviceNameLength );
		//如果找不到打开上下文,就说明这个设备没有绑定过
		if (pOpenContext == NULL)
		{
			status = STATUS_OBJECT_NAME_NOT_FOUND;
			break;
		}

		NdisAcquireSpinLock( &pOpenContext->Lock );
		//如果找到了但不是打开空闲状态,则返回设备忙
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_IDLE ))
		{
			ASSERT( pOpenContext->pFileObject != NULL );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			//解引用
			_NdisDereferenceOpenContext( pOpenContext );

			status = STATUS_DEVICE_BUSY;
			break;
		}

		//调用比较交换
		/*
		首先比较FsContext是否为NULL,如果是NULL,则将FsContext
		设置为pOpenContext,然后返回NULL,如果不是NULL,则不交换,
		并返回FsContext的值
		*/
		if ((pOpenContextInFsContext =
			InterlockedCompareExchangePointer(
			&_pFileObject->FsContext , pOpenContext , NULL )) != NULL)
		{
			//到这里说明这个文件对象已经关联了一个网卡设备,不支持再次关联
			//返回失败
			KdPrint( ("_NdisSetDevice:FileObject is already associated with OpenContext:%p" ,
				pOpenContextInFsContext) );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			_NdisDereferenceOpenContext( pOpenContext );
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}

		//这个打开上下文被打开了,保存在这个文件对象的FsContext中
		//打开上下文也保存了文件对象的指针
		pOpenContext->pFileObject = _pFileObject;
		NDIS_SET_FLAGS( pOpenContext->Flags ,
			NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_ACTIVE );
		NdisReleaseSpinLock( &pOpenContext->Lock );

		//设置PackerFilter,使之能够接受到包
		PacketFilter = NDIS_PACKET_FILTER;
		NdisStatus = _NdisValidateOpenAndDoRequest(
			pOpenContext ,
			NdisRequestSetInformation ,
			OID_GEN_CURRENT_PACKET_FILTER ,
			&PacketFilter , sizeof( PacketFilter ) ,
			&BytesProcessed ,
			TRUE );
		//不成功的话解锁,减引用退出
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			NdisAcquireSpinLock( &pOpenContext->Lock );
			//若不成功,再次比较交换,去掉FsContext
			//如果FsContext是pOpenContext,则设置为NULL
			pOpenContextInFsContext =
				InterlockedCompareExchangePointer(
				&_pFileObject->FsContext , NULL , pOpenContext );
			ASSERT( pOpenContextInFsContext == pOpenContext );
			NDIS_SET_FLAGS( pOpenContext->Flags ,
				NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_IDLE );
			pOpenContext->pFileObject = NULL;
			NdisReleaseSpinLock( &pOpenContext->Lock );
			_NdisDereferenceOpenContext( pOpenContext );
			NDIS_STATUS_TO_NT_STATUS( NdisStatus , &status );
			break;
		}

		//返回打开上下文
		*_ppOpenContext = pOpenContext;
		status = STATUS_SUCCESS;

	} while (FALSE);

	return	status;
}


/*
	该函数在调用DORequest之前确保绑定的有效性
	参数:
	pOpenContext 打开上下文
	RequestType	NDISRequestSet/QueryInformation
	Oid
	InformationBuffer
	InformationBufferLength
	pBytesProcessed	实际处理的字节数
	bWaitForPowerOn	是否需要等待电源打开
	返回值:	NDIS_STATUS
*/
NDIS_STATUS	_NdisValidateOpenAndDoRequest(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_REQUEST_TYPE	_RequestType ,
	IN	NDIS_OID	_Oid ,
	IN	PVOID	_pInformationBuffer ,
	IN	ULONG	_InformationBufferLength ,
	OUT	PULONG	_pBytesProcessed ,
	IN	BOOLEAN	_bWaitForPowerOn
)
{
	NDIS_STATUS	NdisStatus;

	do
	{
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		//Proceed only if we have a binding
		if (!NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NdisReleaseSpinLock( &_pOpenContext->Lock );
			NdisStatus = NDIS_STATUS_INVALID_DATA;
			break;
		}

		ASSERT( _pOpenContext->BindingHandle != NULL );
		//确保绑定不会在处理Request的时候解除,所以增加一个发送引用
		NdisInterlockedIncrement( &_pOpenContext->PendedSendCount );
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		if (_bWaitForPowerOn)
		{
			//
			//  Wait for the device below to be powered up.
			//  We don't wait indefinitely here - this is to avoid
			//  a PROCESS_HAS_LOCKED_PAGES bugcheck that could happen
			//  if the calling process terminates, and this IRP doesn't
			//  complete within a reasonable time. An alternative would
			//  be to explicitly handle cancellation of this IRP.
			//
			NdisWaitEvent( &_pOpenContext->PoweredUpEvent , 4500 );
		}

		//发送Request
		NdisStatus = _NdisDoRequest(
			_pOpenContext ,
			_RequestType ,
			_Oid ,
			_pInformationBuffer ,
			_InformationBufferLength ,
			_pBytesProcessed );

		//减少之前额外增加的发送引用计数
		NdisInterlockedDecrement( &_pOpenContext->PendedSendCount );
	} while (FALSE);

	return	NdisStatus;
}


/*
该函数将绑定的网卡信息拷贝给缓冲区
参数:
pBuffer		指向NDIS_QUERY_BINDING的指针
InputLength	输入长度
OutputLength输出长度
pBytesReturned	用来返回拷贝的字节数
返回值:	操作状态
*/
NDIS_STATUS	_QueryBinding(
	IN	PUCHAR	_pBuffer ,
	IN	ULONG	_InputLength ,
	IN	ULONG	_OutputLength ,
	OUT	PULONG	_pBytesReturned
)
{
	NDIS_STATUS	NdisStatus;
	PNDIS_QUERY_BINDING	pQueryBinding;
	PNDIS_OPEN_CONTEXT	pOpenContext;
	PLIST_ENTRY		pListEntry;
	ULONG	Remaining;
	ULONG	BindingIndex;

	do
	{
		if (_InputLength < sizeof( NDIS_QUERY_BINDING ))
		{
			NdisStatus = NDIS_STATUS_RESOURCES;
			break;
		}

		if (_OutputLength < sizeof( NDIS_QUERY_BINDING ))
		{
			NdisStatus = NDIS_STATUS_BUFFER_OVERFLOW;
			break;
		}

		Remaining = _OutputLength - sizeof( NDIS_QUERY_BINDING );

		pQueryBinding = (PNDIS_QUERY_BINDING)_pBuffer;
		BindingIndex = pQueryBinding->BindingIndex;

		NdisStatus = NDIS_STATUS_ADAPTER_NOT_FOUND;
		pOpenContext = NULL;

		NdisAcquireSpinLock( &Globals.SpinLock );

		for (pListEntry = Globals.OpenList.Flink;
			pListEntry != &Globals.OpenList;
			pListEntry = pListEntry->Flink)
		{
			pOpenContext =
				CONTAINING_RECORD( pListEntry ,
				NDIS_OPEN_CONTEXT , Link );

			NdisAcquireSpinLock( &pOpenContext->Lock );

			//如果当前打开上下文没有绑定网卡就跳过该
			if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
			{
				NdisReleaseSpinLock( &pOpenContext->Lock );
				continue;
			}

			if (BindingIndex == 0)
			{
				//已经找到我们要查询的绑定信息了
				KdPrint( ("_QueryBinding: Found opencontext\n") );

				pQueryBinding->DeviceNameLength =
					 pOpenContext->DeviceName.Length + sizeof( WCHAR );
				pQueryBinding->DeviceDescrLength =
					pOpenContext->DeviceDescr.Length + sizeof( WCHAR );
				if (Remaining < pQueryBinding->DeviceNameLength
					+ pQueryBinding->DeviceDescrLength)
				{
					//目标缓冲区放不下
					NdisReleaseSpinLock( &pOpenContext->Lock );
					NdisStatus = NDIS_STATUS_BUFFER_OVERFLOW;
					break;
				}

				//初始化缓冲区内存为0
				NdisZeroMemory(
					_pBuffer + sizeof( NDIS_QUERY_BINDING ) ,
					pQueryBinding->DeviceNameLength +
					pQueryBinding->DeviceDescrLength );

				//拷贝名称,在这个结构体之后就是存放名称的地方
				pQueryBinding->DeviceNameOffset = sizeof( NDIS_QUERY_BINDING );
				NdisMoveMemory(
					_pBuffer + pQueryBinding->DeviceNameOffset ,
					pOpenContext->DeviceName.Buffer ,
					pOpenContext->DeviceName.Length );

				//拷贝描述信息,在存放名称的地方的后面就是存放描述信息的地方
				pQueryBinding->DeviceDescrOffset =
					pQueryBinding->DeviceNameOffset + pQueryBinding->DeviceNameLength;
				NdisMoveMemory(
					_pBuffer + pQueryBinding->DeviceDescrOffset ,
					pOpenContext->DeviceDescr.Buffer ,
					pOpenContext->DeviceDescr.Length );

				NdisReleaseSpinLock( &pOpenContext->Lock );

				*_pBytesReturned =
					pQueryBinding->DeviceDescrOffset + pQueryBinding->DeviceDescrLength;
				NdisStatus = NDIS_STATUS_SUCCESS;
				break;
			}// if (BindingIndex == 0)

			NdisReleaseSpinLock( &pOpenContext->Lock );

			BindingIndex--;
		}// end for

		NdisReleaseSpinLock( &Globals.SpinLock );

	} while (FALSE);

	return	NdisStatus;
}