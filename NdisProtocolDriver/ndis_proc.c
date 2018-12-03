#include	<ndis_shared_head.h>




/*
DriverEntry中要做的事:
1.注册一个协议,并提供协议特征,设置协议的回调函数
2.生成一个控制设备,并生成一个符号链接,指定分发函数
*/
NTSTATUS	DriverEntry(
	IN	PDRIVER_OBJECT	_pDriverObject ,
	IN	PUNICODE_STRING	_pRegistryPath
)
{
	//协议驱动的协议特征
	NDIS_PROTOCOL_DRIVER_CHARACTERISTICS	ProtocolChar;
	NTSTATUS	status = STATUS_SUCCESS;
	NDIS_STRING	ProtocolName =
		NDIS_STRING_CONST( "PowerfulGun_Ndis" );
	UNICODE_STRING	DeviceName;
	UNICODE_STRING	Win32DeviceName;
	PDEVICE_OBJECT	pDeviceObject = NULL;


	//在全局变量中记录驱动对象指针
	Globals.pDriverObject = _pDriverObject;
	//初始化事件
	NdisInitializeEvent( &Globals.BindCompleteEvent );

	do
	{
		//初始化控制设备名
		RtlInitUnicodeString(
			&DeviceName ,
			L"\\Device\\PowerfulGun_Ndis" );
		//创建控制设备
		status = IoCreateDevice(
			_pDriverObject ,
			0 ,
			&DeviceName ,
			FILE_DEVICE_NETWORK ,
			FILE_DEVICE_SECURE_OPEN ,
			FALSE ,
			&pDeviceObject );
		if (!NT_SUCCESS( status ))
			break;

		//生成符号链接
		RtlInitUnicodeString(
			&Win32DeviceName ,
			L"\\DosDevices\\PowerfulGun_Ndis" );
		status = IoCreateSymbolicLink(
			&Win32DeviceName ,
			&DeviceName );
		if (!NT_SUCCESS( status ))
			break;

		//设备采用直接IO方式
		pDeviceObject->Flags |= DO_DIRECT_IO;
		//记录控制设备
		Globals.pControlDeviceObject = pDeviceObject;

		//初始化链表和锁
		InitializeListHead( &Globals.OpenList );
		NdisAllocateSpinLock( &Globals.SpinLock );

		//填写协议特征
		NdisZeroMemory(
			&ProtocolChar ,
			sizeof( NDIS_PROTOCOL_DRIVER_CHARACTERISTICS) );

		ProtocolChar.Header.Type = 
			NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
		ProtocolChar.Header.Size = 
			sizeof( NDIS_PROTOCOL_DRIVER_CHARACTERISTICS );
		ProtocolChar.Header.Revision = 
			NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;

		ProtocolChar.MajorNdisVersion = 6;
		ProtocolChar.MinorNdisVersion = 0;
		ProtocolChar.MajorDriverVersion = 1;
		ProtocolChar.MinorDriverVersion = 0;
		ProtocolChar.Name = ProtocolName;
		ProtocolChar.OpenAdapterCompleteHandlerEx = _OpenAdapterComplete;
		ProtocolChar.CloseAdapterCompleteHandlerEx = _CloseAdapterComplete;
		ProtocolChar.SendNetBufferListsCompleteHandler = _SendComplete;
		ProtocolChar.OidRequestCompleteHandler = _RequestComplete;
		ProtocolChar.StatusHandlerEx = _StatusHandler;
		ProtocolChar.BindAdapterHandlerEx = _BindAdapterHandlerEx;
		ProtocolChar.UnbindAdapterHandlerEx = _UnbindAdapterHandlerEx;
		ProtocolChar.UninstallHandler = NULL;
		ProtocolChar.ReceiveNetBufferListsHandler = _ReceiveNetBufferList;
		ProtocolChar.NetPnPEventHandler = _PnpEventHandler;

		//注册网络协议
		status = NdisRegisterProtocolDriver(
			NULL ,
			&ProtocolChar ,
			&Globals.NdisProtocolHandle );
		if (status != NDIS_STATUS_SUCCESS)
		{
			KdPrint( ("Fail to register protocol,status=%x \n",status) );
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		//填写本驱动需要的分发函数,仅用于控制设备
		_pDriverObject->MajorFunction[IRP_MJ_CREATE] = _NdisCreateDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_CLOSE] = _NdisCloseDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_READ] = _NdisReadDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_WRITE] = _NdisWriteDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_CLEANUP] = _NdisCleanupDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = _NdisDeviceControlDispatch;
		_pDriverObject->DriverUnload = _NdisUnload;

		status = STATUS_SUCCESS;

	} while (FALSE);

	//如果不成功需要释放资源
	if (!NT_SUCCESS( status ))
	{
		if (pDeviceObject)
		{
			IoDeleteDevice( pDeviceObject );
			Globals.pControlDeviceObject = NULL;
		}
		IoDeleteSymbolicLink( &Win32DeviceName );
	}

	return	status;
}


/*
该函数处理驱动控制设备受到的IRP_MJ_CREATE请求,简单地返回成功
*/
NTSTATUS	_NdisCreateDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	NTSTATUS	status = STATUS_SUCCESS;
	PIO_STACK_LOCATION	pIrpStack =
		IoGetCurrentIrpStackLocation( _pIrp );
	pIrpStack->FileObject->FsContext = NULL;

	KdPrint( ("_NidsCreateDispatch:Open fileobject %p\n" ,
		pIrpStack->FileObject) );

	_pIrp->IoStatus.Status = status;
	_pIrp->IoStatus.Information = 0;
	IoCompleteRequest( _pIrp , IO_NO_INCREMENT );

	return	status;
}


/*
该函数处理IRP_MJ_CLOSE请求,需要减少对打开上下文的引用
*/
NTSTATUS	_NdisCloseDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	NTSTATUS	status;
	PNDIS_OPEN_CONTEXT	pOpenContext;
	PIO_STACK_LOCATION	pIrpStack =
		IoGetCurrentIrpStackLocation( _pIrp );

	KdPrint( ("_NdisCloseDispatch:Close FileObject %p\n" ,
		pIrpStack->FileObject) );

	pOpenContext = pIrpStack->FileObject->FsContext;
	if (pOpenContext != NULL)
	{
		//减少引用
		_NdisDereferenceOpenContext( pOpenContext );
	}

	pIrpStack->FileObject->FsContext = NULL;
	status = STATUS_SUCCESS;
	_pIrp->IoStatus.Status = STATUS_SUCCESS;
	_pIrp->IoStatus.Information = 0;
	IoCompleteRequest( _pIrp , IO_NO_INCREMENT );

	return	status;
}


/*
处理驱动收到的IRP_MJ_READ请求
读请求的本质是从应用层获取网卡上已经收到的包,这些包会被本协议驱动
放入到缓冲队列里,处理读请求就是要检测这个队列中有无数据包,如果有,
则把包的内容拷贝到读请求的输出缓冲区,
FileObject->FsContext指定了处理哪个网卡上的数据包
*/
NTSTATUS	_NdisReadDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	NTSTATUS	status;
	PIO_STACK_LOCATION	pIrpStack =
		IoGetCurrentIrpStackLocation( _pIrp );
	PNDIS_OPEN_CONTEXT	pOpenContext =
		pIrpStack->FileObject->FsContext;

	do
	{
		//检测打开上下文的可靠性
		if (pOpenContext == NULL)
		{
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//read和Write使用的都是直接IO操作,所以应该使用MDLaddress
		if (_pIrp->MdlAddress == NULL)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//得到缓冲区的虚拟地址
		if (MmGetSystemAddressForMdlSafe(
			_pIrp->MdlAddress , NormalPagePriority
			) == NULL)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		NdisAcquireSpinLock( &pOpenContext->Lock );
		//此时这个绑定应该处于活动状态
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//将这个请求插入处理队列里,并把打开上下文引用计数增加1
		//未处理的读请求数目增加1
		InsertTailList( &pOpenContext->PendedReads ,
			&_pIrp->Tail.Overlay.ListEntry );
		NdisInterlockedIncrement( &pOpenContext->RefCount );
		NdisInterlockedIncrement( &pOpenContext->PendedReadCount );

		//标记该IRp未决,给irp设置一个取消函数,使之变得可取消
		_pIrp->Tail.Overlay.DriverContext[0] = pOpenContext;
		IoMarkIrpPending( _pIrp );
		IoSetCancelRoutine( _pIrp , _NdisCancelRead );

		NdisReleaseSpinLock( &pOpenContext->Lock );

		status = STATUS_PENDING;

		// 调用一个处理例程处理所有的未决的读请求
		//_NdisServiceReads( pOpenContext );

	} while (FALSE);

	if (status != STATUS_PENDING)
	{
		_pIrp->IoStatus.Status = status;
		_pIrp->IoStatus.Information = 0;
		IoCompleteRequest( _pIrp , IO_NO_INCREMENT );
	}

	return	status;
}


/*
该函数取消一个未决的读Irp,在OpenContext的PendedRead队列里取出这个
irp并完成它
*/
VOID	_NdisCancelRead(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;
	PLIST_ENTRY	pIrpEntry;
	BOOLEAN	bFound = FALSE;

	IoReleaseCancelSpinLock( _pIrp->CancelIrql );

	pOpenContext =
		_pIrp->Tail.Overlay.DriverContext[0];

	NdisAcquireSpinLock( &pOpenContext->Lock );

	//在未决读请求队列中寻找该Irp
	for (pIrpEntry = pOpenContext->PendedReads.Flink;
		pIrpEntry != &pOpenContext->PendedReads;
		pIrpEntry = pIrpEntry->Flink)
	{
		if (_pIrp ==
			CONTAINING_RECORD( pIrpEntry ,
			IRP , Tail.Overlay.ListEntry ))
		{
			RemoveEntryList( &_pIrp->Tail.Overlay.ListEntry );
			//未决的读请求减少1
			pOpenContext->PendedReadCount--;
			bFound = TRUE;
			break;
		}
	}
	NdisReleaseSpinLock( &pOpenContext->Lock );

	if (bFound)
	{
		KdPrint( ("_NdisCancelRead:Found irp,now cancel it\n") );
		_pIrp->IoStatus.Status = STATUS_CANCELLED;
		_pIrp->IoStatus.Information = 0;
		IoCompleteRequest( _pIrp , IO_NO_INCREMENT );
		_NdisDereferenceOpenContext( pOpenContext );
	}
}


/*
该函数处理驱动收到的IRP_MJ_WRITE请求
*/
NTSTATUS	_NdisWriteDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	NTSTATUS	status;
	NDIS_STATUS	NdisStatus;
	PIO_STACK_LOCATION	pIrpStack =
		IoGetCurrentIrpStackLocation( _pIrp );
	PNDIS_OPEN_CONTEXT	pOpenContext =
		pIrpStack->FileObject->FsContext;
	PNDIS_ETH_HEADER	pEthHeader;
	ULONG	DataLength;
	PNET_BUFFER_LIST	pSendNetBufferList;

	do
	{
		//检测打开上下文可靠性
		if (pOpenContext == NULL)
		{
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//确认输入缓冲区的可靠性
		if (_pIrp->MdlAddress == NULL)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//得到输入缓冲区的虚拟地址之后要进行一系列检查
		//1.地址不能为NULL
		//2.缓冲区的长度至少要比一个以太头要长
		//3.发包的长度不能超过网卡的最大帧长
		pEthHeader =
			MmGetSystemAddressForMdlSafe(
			_pIrp->MdlAddress , NormalPagePriority );
		if (pEthHeader == NULL)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		DataLength = MmGetMdlByteCount( _pIrp->MdlAddress );
		if (DataLength , sizeof( NDIS_ETH_HEADER ))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		if (DataLength > (pOpenContext->MaxFrameSize + sizeof( NDIS_ETH_HEADER )))
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		//检查网卡是否处于可以发包的状态
		NdisAcquireSpinLock( &pOpenContext->Lock );
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_DEVICE_NOT_READY;
			break;
		}

		//分配一个NET_BUFFER_LIST结构体,
		ASSERT( pOpenContext->SendNetBufferListPool != NULL );
		pSendNetBufferList =
			NdisAllocateNetBufferAndNetBufferList(
			pOpenContext->SendNetBufferListPool ,
			sizeof( NDIS_SEND_PACKET_RSVD ) ,
			0 ,
			_pIrp->MdlAddress ,	//链接用户要发送的缓冲区
			0 ,
			_pIrp->MdlAddress->ByteCount );
		if (pSendNetBufferList == NULL)
		{
			KdPrint( ("_NdisWriteDispatch: Fail to allocate SendNetBufferList !\n") );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		//初始化包引用计数,这个包会在计数为0的时候释放掉
		((PNDIS_SEND_PACKET_RSVD)
			(pSendNetBufferList->Context->ContextData))->RefCount = 1;
		//把irp指针放在包描述符里,以备后用
		((PNDIS_SEND_PACKET_RSVD)
			(pSendNetBufferList->Context->ContextData))->pIrp = _pIrp;

		//记录发送包增加了一个
		NdisInterlockedIncrement( &pOpenContext->PendedSendCount );
		//打开上下文引用计数加1,为了防止发包过程中网卡绑定被解除
		NdisInterlockedIncrement( &pOpenContext->RefCount );

		NdisReleaseSpinLock( &pOpenContext->Lock );


		//标记IRP未决
		IoMarkIrpPending( _pIrp );
		status = STATUS_PENDING;

		//发送数据,发送完成后会调用SendNetBufferListsComplete
		NdisSendNetBufferLists(
			pOpenContext->BindingHandle ,
			pSendNetBufferList ,
			0 ,
			0 );

	} while (FALSE);

	//如果正常,则status是STATUS_PENDING,如果不是就有误
	if (status != STATUS_PENDING)
	{
		_pIrp->IoStatus.Status = status;
		IoCompleteRequest( _pIrp , IO_NO_INCREMENT );
	}
	return	status;
}


/*
该函数处理IRP_MJ_CLEANUP请求
*/
NTSTATUS	_NdisCleanupDispatch(
	IN	PDEVICE_OBJECT	_pDeviceObject ,
	IN	PIRP	_pIrp
)
{
	PIO_STACK_LOCATION	pIrpStack;
	NTSTATUS	status;
	NDIS_STATUS	NdisStatus;
	PNDIS_OPEN_CONTEXT	pOpenContext;
	ULONG	PacketFilter;
	ULONG	BytesProcessed;

	pIrpStack = IoGetCurrentIrpStackLocation( _pIrp );
	pOpenContext = pIrpStack->FileObject->FsContext;

	if (pOpenContext != NULL)
	{
		NdisAcquireSpinLock( &pOpenContext->Lock );
		NDIS_SET_FLAGS( pOpenContext->Flags ,
			NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_IDLE );
		NdisReleaseSpinLock( &pOpenContext->Lock );

		//通知下层停止收包
		PacketFilter = 0;
		NdisStatus = _NdisValidateOpenAndDoRequest(
			pOpenContext ,
			NdisRequestSetInformation ,
			OID_GEN_CURRENT_PACKET_FILTER ,
			&PacketFilter ,
			sizeof( PacketFilter ) ,
			&BytesProcessed ,
			FALSE );	//不用等待电源开启
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			//设置不成功没事
			KdPrint( ("_NdisCleanupDispatch: Fail to set packet filter!\n") );
			NdisStatus = NDIS_STATUS_SUCCESS;
		}

		//结束所哟读请求
		_CancelPendingReads( pOpenContext );

		//结束状态提交的 control Irp 请求
		_ServiceIndicateStatusIrp(
			pOpenContext ,
			0 ,
			NULL ,
			0 ,
			TRUE );

		//清理接收包队列
		_FlushReceiveQueue( pOpenContext );

	}// if (pOpenContext != NULL)

	status = STATUS_SUCCESS;

	_pIrp->IoStatus.Status = status;
	_pIrp->IoStatus.Information = 0;
	IoCompleteRequest( _pIrp , IO_NO_INCREMENT );

	return	status;
}


/*
该函数实行驱动卸载,释放所有资源
*/
VOID	_NdisUnload(
	IN	PDRIVER_OBJECT	_pDriverObject
)
{
	UNICODE_STRING	Win32DeviceName;
	NDIS_STATUS		NdisStatus;

	KdPrint( ("[_NdisUnload]\n"));

	RtlInitUnicodeString( &Win32DeviceName ,
		L"\\DosDevices\\PowerfulGun_Ndis" );

	//删除符号链接名
	IoDeleteSymbolicLink( &Win32DeviceName );

	if (Globals.pControlDeviceObject)
	{
		//删除控制设备
		IoDeleteDevice( Globals.pControlDeviceObject );
		Globals.pControlDeviceObject = NULL;
	}

	//取消网络协议注册
	if (Globals.NdisProtocolHandle != NULL)
	{
		NdisDeregisterProtocolDriver(
			Globals.NdisProtocolHandle );
	}

	//释放全局自旋锁
	NdisFreeSpinLock( &Globals.SpinLock );
}