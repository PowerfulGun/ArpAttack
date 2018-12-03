#include	<ndis_shared_head.h>




/*
DriverEntry��Ҫ������:
1.ע��һ��Э��,���ṩЭ������,����Э��Ļص�����
2.����һ�������豸,������һ����������,ָ���ַ�����
*/
NTSTATUS	DriverEntry(
	IN	PDRIVER_OBJECT	_pDriverObject ,
	IN	PUNICODE_STRING	_pRegistryPath
)
{
	//Э��������Э������
	NDIS_PROTOCOL_DRIVER_CHARACTERISTICS	ProtocolChar;
	NTSTATUS	status = STATUS_SUCCESS;
	NDIS_STRING	ProtocolName =
		NDIS_STRING_CONST( "PowerfulGun_Ndis" );
	UNICODE_STRING	DeviceName;
	UNICODE_STRING	Win32DeviceName;
	PDEVICE_OBJECT	pDeviceObject = NULL;


	//��ȫ�ֱ����м�¼��������ָ��
	Globals.pDriverObject = _pDriverObject;
	//��ʼ���¼�
	NdisInitializeEvent( &Globals.BindCompleteEvent );

	do
	{
		//��ʼ�������豸��
		RtlInitUnicodeString(
			&DeviceName ,
			L"\\Device\\PowerfulGun_Ndis" );
		//���������豸
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

		//���ɷ�������
		RtlInitUnicodeString(
			&Win32DeviceName ,
			L"\\DosDevices\\PowerfulGun_Ndis" );
		status = IoCreateSymbolicLink(
			&Win32DeviceName ,
			&DeviceName );
		if (!NT_SUCCESS( status ))
			break;

		//�豸����ֱ��IO��ʽ
		pDeviceObject->Flags |= DO_DIRECT_IO;
		//��¼�����豸
		Globals.pControlDeviceObject = pDeviceObject;

		//��ʼ���������
		InitializeListHead( &Globals.OpenList );
		NdisAllocateSpinLock( &Globals.SpinLock );

		//��дЭ������
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

		//ע������Э��
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

		//��д��������Ҫ�ķַ�����,�����ڿ����豸
		_pDriverObject->MajorFunction[IRP_MJ_CREATE] = _NdisCreateDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_CLOSE] = _NdisCloseDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_READ] = _NdisReadDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_WRITE] = _NdisWriteDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_CLEANUP] = _NdisCleanupDispatch;
		_pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = _NdisDeviceControlDispatch;
		_pDriverObject->DriverUnload = _NdisUnload;

		status = STATUS_SUCCESS;

	} while (FALSE);

	//������ɹ���Ҫ�ͷ���Դ
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
�ú����������������豸�ܵ���IRP_MJ_CREATE����,�򵥵ط��سɹ�
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
�ú�������IRP_MJ_CLOSE����,��Ҫ���ٶԴ������ĵ�����
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
		//��������
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
���������յ���IRP_MJ_READ����
������ı����Ǵ�Ӧ�ò��ȡ�������Ѿ��յ��İ�,��Щ���ᱻ��Э������
���뵽���������,������������Ҫ�������������������ݰ�,�����,
��Ѱ������ݿ���������������������,
FileObject->FsContextָ���˴����ĸ������ϵ����ݰ�
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
		//���������ĵĿɿ���
		if (pOpenContext == NULL)
		{
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//read��Writeʹ�õĶ���ֱ��IO����,����Ӧ��ʹ��MDLaddress
		if (_pIrp->MdlAddress == NULL)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//�õ��������������ַ
		if (MmGetSystemAddressForMdlSafe(
			_pIrp->MdlAddress , NormalPagePriority
			) == NULL)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		NdisAcquireSpinLock( &pOpenContext->Lock );
		//��ʱ�����Ӧ�ô��ڻ״̬
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//�����������봦�������,���Ѵ����������ü�������1
		//δ����Ķ�������Ŀ����1
		InsertTailList( &pOpenContext->PendedReads ,
			&_pIrp->Tail.Overlay.ListEntry );
		NdisInterlockedIncrement( &pOpenContext->RefCount );
		NdisInterlockedIncrement( &pOpenContext->PendedReadCount );

		//��Ǹ�IRpδ��,��irp����һ��ȡ������,ʹ֮��ÿ�ȡ��
		_pIrp->Tail.Overlay.DriverContext[0] = pOpenContext;
		IoMarkIrpPending( _pIrp );
		IoSetCancelRoutine( _pIrp , _NdisCancelRead );

		NdisReleaseSpinLock( &pOpenContext->Lock );

		status = STATUS_PENDING;

		// ����һ���������̴������е�δ���Ķ�����
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
�ú���ȡ��һ��δ���Ķ�Irp,��OpenContext��PendedRead������ȡ�����
irp�������
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

	//��δ�������������Ѱ�Ҹ�Irp
	for (pIrpEntry = pOpenContext->PendedReads.Flink;
		pIrpEntry != &pOpenContext->PendedReads;
		pIrpEntry = pIrpEntry->Flink)
	{
		if (_pIrp ==
			CONTAINING_RECORD( pIrpEntry ,
			IRP , Tail.Overlay.ListEntry ))
		{
			RemoveEntryList( &_pIrp->Tail.Overlay.ListEntry );
			//δ���Ķ��������1
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
�ú������������յ���IRP_MJ_WRITE����
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
		//���������Ŀɿ���
		if (pOpenContext == NULL)
		{
			status = STATUS_INVALID_HANDLE;
			break;
		}

		//ȷ�����뻺�����Ŀɿ���
		if (_pIrp->MdlAddress == NULL)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//�õ����뻺�����������ַ֮��Ҫ����һϵ�м��
		//1.��ַ����ΪNULL
		//2.�������ĳ�������Ҫ��һ����̫ͷҪ��
		//3.�����ĳ��Ȳ��ܳ������������֡��
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

		//��������Ƿ��ڿ��Է�����״̬
		NdisAcquireSpinLock( &pOpenContext->Lock );
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_DEVICE_NOT_READY;
			break;
		}

		//����һ��NET_BUFFER_LIST�ṹ��,
		ASSERT( pOpenContext->SendNetBufferListPool != NULL );
		pSendNetBufferList =
			NdisAllocateNetBufferAndNetBufferList(
			pOpenContext->SendNetBufferListPool ,
			sizeof( NDIS_SEND_PACKET_RSVD ) ,
			0 ,
			_pIrp->MdlAddress ,	//�����û�Ҫ���͵Ļ�����
			0 ,
			_pIrp->MdlAddress->ByteCount );
		if (pSendNetBufferList == NULL)
		{
			KdPrint( ("_NdisWriteDispatch: Fail to allocate SendNetBufferList !\n") );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		//��ʼ�������ü���,��������ڼ���Ϊ0��ʱ���ͷŵ�
		((PNDIS_SEND_PACKET_RSVD)
			(pSendNetBufferList->Context->ContextData))->RefCount = 1;
		//��irpָ����ڰ���������,�Ա�����
		((PNDIS_SEND_PACKET_RSVD)
			(pSendNetBufferList->Context->ContextData))->pIrp = _pIrp;

		//��¼���Ͱ�������һ��
		NdisInterlockedIncrement( &pOpenContext->PendedSendCount );
		//�����������ü�����1,Ϊ�˷�ֹ���������������󶨱����
		NdisInterlockedIncrement( &pOpenContext->RefCount );

		NdisReleaseSpinLock( &pOpenContext->Lock );


		//���IRPδ��
		IoMarkIrpPending( _pIrp );
		status = STATUS_PENDING;

		//��������,������ɺ�����SendNetBufferListsComplete
		NdisSendNetBufferLists(
			pOpenContext->BindingHandle ,
			pSendNetBufferList ,
			0 ,
			0 );

	} while (FALSE);

	//�������,��status��STATUS_PENDING,������Ǿ�����
	if (status != STATUS_PENDING)
	{
		_pIrp->IoStatus.Status = status;
		IoCompleteRequest( _pIrp , IO_NO_INCREMENT );
	}
	return	status;
}


/*
�ú�������IRP_MJ_CLEANUP����
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

		//֪ͨ�²�ֹͣ�հ�
		PacketFilter = 0;
		NdisStatus = _NdisValidateOpenAndDoRequest(
			pOpenContext ,
			NdisRequestSetInformation ,
			OID_GEN_CURRENT_PACKET_FILTER ,
			&PacketFilter ,
			sizeof( PacketFilter ) ,
			&BytesProcessed ,
			FALSE );	//���õȴ���Դ����
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			//���ò��ɹ�û��
			KdPrint( ("_NdisCleanupDispatch: Fail to set packet filter!\n") );
			NdisStatus = NDIS_STATUS_SUCCESS;
		}

		//������Ӵ������
		_CancelPendingReads( pOpenContext );

		//����״̬�ύ�� control Irp ����
		_ServiceIndicateStatusIrp(
			pOpenContext ,
			0 ,
			NULL ,
			0 ,
			TRUE );

		//������հ�����
		_FlushReceiveQueue( pOpenContext );

	}// if (pOpenContext != NULL)

	status = STATUS_SUCCESS;

	_pIrp->IoStatus.Status = status;
	_pIrp->IoStatus.Information = 0;
	IoCompleteRequest( _pIrp , IO_NO_INCREMENT );

	return	status;
}


/*
�ú���ʵ������ж��,�ͷ�������Դ
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

	//ɾ������������
	IoDeleteSymbolicLink( &Win32DeviceName );

	if (Globals.pControlDeviceObject)
	{
		//ɾ�������豸
		IoDeleteDevice( Globals.pControlDeviceObject );
		Globals.pControlDeviceObject = NULL;
	}

	//ȡ������Э��ע��
	if (Globals.NdisProtocolHandle != NULL)
	{
		NdisDeregisterProtocolDriver(
			Globals.NdisProtocolHandle );
	}

	//�ͷ�ȫ��������
	NdisFreeSpinLock( &Globals.SpinLock );
}