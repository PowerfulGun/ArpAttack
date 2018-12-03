/*
	���������յ���IRP_MJ_DEVICE_CONTROL����
*/
#include	"ndis_shared_head.h"
#include	"ioctl.h"



/*
�ú�������IRP_MJ_DEVICE_CONTROL����
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

			//�ȴ�ȫ�ְ��¼�,����¼����ڰ���ɺ�����
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
�ú������ļ�����ָ������һ�������豸
1.�����뻺�������õ��豸��
2.ͨ���豸��ȥѰ�Ҷ�Ӧ�Ĵ�������
3.����ҵ���,�ͱ��浽FileObject->FsContext��
����:
pDeviceName	�豸��
DeviceNameLength	�豸������
pFIleObject	�ļ�����ָ��
ppOpenContext	�������������豸�Ĵ�������
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
		//�����豸���ҵ���������,���û����Ӵ������ĵ�����
		pOpenContext = _LookupOpenContext(
			_pDeviceName , _DeviceNameLength );
		//����Ҳ�����������,��˵������豸û�а󶨹�
		if (pOpenContext == NULL)
		{
			status = STATUS_OBJECT_NAME_NOT_FOUND;
			break;
		}

		NdisAcquireSpinLock( &pOpenContext->Lock );
		//����ҵ��˵����Ǵ򿪿���״̬,�򷵻��豸æ
		if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
			NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_IDLE ))
		{
			ASSERT( pOpenContext->pFileObject != NULL );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			//������
			_NdisDereferenceOpenContext( pOpenContext );

			status = STATUS_DEVICE_BUSY;
			break;
		}

		//���ñȽϽ���
		/*
		���ȱȽ�FsContext�Ƿ�ΪNULL,�����NULL,��FsContext
		����ΪpOpenContext,Ȼ�󷵻�NULL,�������NULL,�򲻽���,
		������FsContext��ֵ
		*/
		if ((pOpenContextInFsContext =
			InterlockedCompareExchangePointer(
			&_pFileObject->FsContext , pOpenContext , NULL )) != NULL)
		{
			//������˵������ļ������Ѿ�������һ�������豸,��֧���ٴι���
			//����ʧ��
			KdPrint( ("_NdisSetDevice:FileObject is already associated with OpenContext:%p" ,
				pOpenContextInFsContext) );
			NdisReleaseSpinLock( &pOpenContext->Lock );
			_NdisDereferenceOpenContext( pOpenContext );
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}

		//����������ı�����,����������ļ������FsContext��
		//��������Ҳ�������ļ������ָ��
		pOpenContext->pFileObject = _pFileObject;
		NDIS_SET_FLAGS( pOpenContext->Flags ,
			NDIS_OPEN_FLAGS_MASK , NDIS_OPEN_ACTIVE );
		NdisReleaseSpinLock( &pOpenContext->Lock );

		//����PackerFilter,ʹ֮�ܹ����ܵ���
		PacketFilter = NDIS_PACKET_FILTER;
		NdisStatus = _NdisValidateOpenAndDoRequest(
			pOpenContext ,
			NdisRequestSetInformation ,
			OID_GEN_CURRENT_PACKET_FILTER ,
			&PacketFilter , sizeof( PacketFilter ) ,
			&BytesProcessed ,
			TRUE );
		//���ɹ��Ļ�����,�������˳�
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			NdisAcquireSpinLock( &pOpenContext->Lock );
			//�����ɹ�,�ٴαȽϽ���,ȥ��FsContext
			//���FsContext��pOpenContext,������ΪNULL
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

		//���ش�������
		*_ppOpenContext = pOpenContext;
		status = STATUS_SUCCESS;

	} while (FALSE);

	return	status;
}


/*
	�ú����ڵ���DORequest֮ǰȷ���󶨵���Ч��
	����:
	pOpenContext ��������
	RequestType	NDISRequestSet/QueryInformation
	Oid
	InformationBuffer
	InformationBufferLength
	pBytesProcessed	ʵ�ʴ�����ֽ���
	bWaitForPowerOn	�Ƿ���Ҫ�ȴ���Դ��
	����ֵ:	NDIS_STATUS
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
		//ȷ���󶨲����ڴ���Request��ʱ����,��������һ����������
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

		//����Request
		NdisStatus = _NdisDoRequest(
			_pOpenContext ,
			_RequestType ,
			_Oid ,
			_pInformationBuffer ,
			_InformationBufferLength ,
			_pBytesProcessed );

		//����֮ǰ�������ӵķ������ü���
		NdisInterlockedDecrement( &_pOpenContext->PendedSendCount );
	} while (FALSE);

	return	NdisStatus;
}


/*
�ú������󶨵�������Ϣ������������
����:
pBuffer		ָ��NDIS_QUERY_BINDING��ָ��
InputLength	���볤��
OutputLength�������
pBytesReturned	�������ؿ������ֽ���
����ֵ:	����״̬
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

			//�����ǰ��������û�а�������������
			if (!NDIS_TEST_FLAGS( pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
			{
				NdisReleaseSpinLock( &pOpenContext->Lock );
				continue;
			}

			if (BindingIndex == 0)
			{
				//�Ѿ��ҵ�����Ҫ��ѯ�İ���Ϣ��
				KdPrint( ("_QueryBinding: Found opencontext\n") );

				pQueryBinding->DeviceNameLength =
					 pOpenContext->DeviceName.Length + sizeof( WCHAR );
				pQueryBinding->DeviceDescrLength =
					pOpenContext->DeviceDescr.Length + sizeof( WCHAR );
				if (Remaining < pQueryBinding->DeviceNameLength
					+ pQueryBinding->DeviceDescrLength)
				{
					//Ŀ�껺�����Ų���
					NdisReleaseSpinLock( &pOpenContext->Lock );
					NdisStatus = NDIS_STATUS_BUFFER_OVERFLOW;
					break;
				}

				//��ʼ���������ڴ�Ϊ0
				NdisZeroMemory(
					_pBuffer + sizeof( NDIS_QUERY_BINDING ) ,
					pQueryBinding->DeviceNameLength +
					pQueryBinding->DeviceDescrLength );

				//��������,������ṹ��֮����Ǵ�����Ƶĵط�
				pQueryBinding->DeviceNameOffset = sizeof( NDIS_QUERY_BINDING );
				NdisMoveMemory(
					_pBuffer + pQueryBinding->DeviceNameOffset ,
					pOpenContext->DeviceName.Buffer ,
					pOpenContext->DeviceName.Length );

				//����������Ϣ,�ڴ�����Ƶĵط��ĺ�����Ǵ��������Ϣ�ĵط�
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