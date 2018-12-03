/*
	�������İ�/���
*/
#include	"ndis_shared_head.h"


/*
_BindAdapter������Ҫ����:
1.�跨��ֹ���߳̾���
2.����ͳ�ʼ����ΰ󶨵������Դ
3.���������һЩ����
����:
pOpenContext	��������ָ��
pBindParameters	Ҫ�󶨵��������Ĳ���ָ��
BindContext		The handle that identifies the NDIS context area for the bind operation. 
				NDIS passed this handle to the BindContext parameter of the ProtocolBindAdapterEx function.
				��NdisOpenAdapterEx��������ʹ��
����ֵ:	NDIS_STATUS	����״̬
*/
NDIS_STATUS	_BindAdapter(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNDIS_BIND_PARAMETERS	_pBindParameters,
	IN	NDIS_HANDLE			_BindContext
)
{
	NDIS_STATUS	NdisStatus = NDIS_STATUS_SUCCESS;
	NDIS_STATUS	OpenErrorCode;
	PNDIS_OPEN_CONTEXT	pTmpOpenContext;
	BOOLEAN	bOpenComplete = FALSE;
	BOOLEAN	bDoNotDisturb = FALSE;
	UINT	SelectedMediumIndex;
	NDIS_MEDIUM  MediumArray[1] = { NdisMedium802_3 };
	ULONG	BytesProcessed;
	ULONG	MediaStatus;
	NDIS_OPEN_PARAMETERS	OpenParameters;

	KdPrint( ("_BindAdapter:Open device [%wZ]\n" ,
		_pBindParameters->AdapterName) );
	do
	{
		/*
		�ȼ���Ƿ��Ѿ������������,
		����Ѿ��󶨵Ļ�,û��Ҫ�ٴΰ�,ֱ�ӷ��ؼ���
		_LookupOpenContext����ҵ��Ĵ���������������,
		��Ҫ��������
		*/
		pTmpOpenContext =
			_LookupOpenContext(
			_pBindParameters->AdapterName->Buffer ,
			_pBindParameters->AdapterName->Length );
		if (pTmpOpenContext != NULL)
		{
			KdPrint( ("_BindAdapter:The device [%ws]is already bind\n" ,
				pTmpOpenContext->DeviceName.Buffer) );
			//��������������ĵ�����
			NdisInterlockedDecrement( &pTmpOpenContext->RefCount );
			NdisStatus = NDIS_STATUS_FAILURE;
			break;
		}

		//��ô������ĵ���,Ϊ�˶�ռ������Flags
		NdisAcquireSpinLock( &_pOpenContext->Lock );

		//�����,�����ǲ��ǿ���״̬,�����ǽ����״̬
		//��ô�ͷ���ʧ��
		if (!NDIS_TEST_FLAGS(
			_pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK ,
			NDIS_BIND_IDLE )
			||
			NDIS_TEST_FLAGS(
			_pOpenContext->Flags ,
			NDIS_UNBIND_FLAGS_MASK ,
			NDIS_UNBIND_RECEIVED ))
		{
			NdisReleaseSpinLock( &_pOpenContext->Lock );
			NdisStatus = NDIS_STATUS_NOT_ACCEPTED;

			bDoNotDisturb = TRUE;
			break;
		}

		//���ñ��,��ʾ�����Ѿ���ʼ������豸��
		NDIS_SET_FLAGS(
			_pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK ,
			NDIS_BIND_OPENING );
		//�ͷ���
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		//�����￪ʼ����,�ȷ�������
		_pOpenContext->DeviceName.Buffer =
			NdisAllocateMemoryWithTagPriority(
			Globals.NdisProtocolHandle ,
			_pBindParameters->AdapterName->Length + sizeof( WCHAR ) ,
			POOL_TAG , NormalPoolPriority );
		if (_pOpenContext->DeviceName.Buffer == NULL)
		{
			KdPrint( ("_BindAdapter: fail to allocate device name buffer !\n") );
			NdisStatus = NDIS_STATUS_RESOURCES;
			break;
		}
		//��������
		NdisMoveMemory(
			_pOpenContext->DeviceName.Buffer ,
			_pBindParameters->AdapterName->Buffer ,
			_pBindParameters->AdapterName->Length );
		//���ĩβ\0
		*(PWCHAR)(_pOpenContext->DeviceName.Buffer + _pBindParameters->AdapterName->Length) = L'\0';
		NdisInitUnicodeString(
			&_pOpenContext->DeviceName ,
			_pOpenContext->DeviceName.Buffer );

		//���NET_BUFFER_LIST_POOL_PARAMETERS�ṹ��
		NET_BUFFER_LIST_POOL_PARAMETERS	NetBufferListPoolParameters;
		NetBufferListPoolParameters.Header.Type = 
			NDIS_OBJECT_TYPE_DEFAULT;
		NetBufferListPoolParameters.Header.Revision = 
			NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
		NetBufferListPoolParameters.Header.Size = 
			NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
		NetBufferListPoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
		NetBufferListPoolParameters.fAllocateNetBuffer = TRUE;
		NetBufferListPoolParameters.ContextSize = 0;
		NetBufferListPoolParameters.PoolTag = POOL_TAG;
		NetBufferListPoolParameters.DataSize = 0;

		//���䷢��NET_BUFFER_LIST��
		_pOpenContext->SendNetBufferListPool =
			NdisAllocateNetBufferListPool(
			Globals.NdisProtocolHandle ,
			&NetBufferListPoolParameters );
		if (_pOpenContext->SendNetBufferListPool == NULL)
		{
			KdPrint( ("_BindAdapter: Fail to allocate SendNetBufferListPool !\n") );
			break;
		}

		//�������NET_BUFFER_LIST��,�Ժ���յ�����ʱ��,�ʹ����������ݷ���
		_pOpenContext->RecvNetBufferListPool =
			NdisAllocateNetBufferListPool(
			Globals.NdisProtocolHandle ,
			&NetBufferListPoolParameters );
		if (_pOpenContext->RecvNetBufferListPool == NULL)
		{
			KdPrint( ("_BindAdapter: Fail to allocate RecvNetBufferListPool !\n") );
			break;
		}

		//���NET_BUFFER_POOL_PARAMETERS�ṹ��
		NET_BUFFER_POOL_PARAMETERS	NetBufferPoolParameters;
		NetBufferPoolParameters.Header.Type =
			NDIS_OBJECT_TYPE_DEFAULT;
		NetBufferPoolParameters.Header.Revision =
			NET_BUFFER_POOL_PARAMETERS_REVISION_1;
		NetBufferPoolParameters.Header.Size =
			NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1;
		NetBufferPoolParameters.PoolTag = POOL_TAG;
		NetBufferPoolParameters.DataSize = 0;
		//����NET_BUFFER��,����������
		_pOpenContext->RecvNetBufferPool =
			NdisAllocateNetBufferPool(
			Globals.NdisProtocolHandle ,
			&NetBufferPoolParameters );
		if (_pOpenContext->RecvNetBufferPool == 0)
		{
			KdPrint( ("_BindAdapter: Fail to allocate RecvNetBufferPool !\n") );
			break;
		}

		//��Դ״̬�Ǵ��ŵ�
		_pOpenContext->PowerState = NetDeviceStateD0;

		//��ʼ��һ�����¼�,�򿪾��ǰ�
		NdisInitializeEvent( &_pOpenContext->BindEvent );

		//��дOpenParameters����
		OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
		OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
		OpenParameters.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
		OpenParameters.AdapterName =
			_pBindParameters->AdapterName;
		OpenParameters.MediumArray = MediumArray;
		OpenParameters.MediumArraySize = 
			sizeof( MediumArray ) / sizeof( NDIS_MEDIUM );
		OpenParameters.FrameTypeArray = NULL;
		OpenParameters.FrameTypeArraySize = 0;

		//��ʽ����Э���Ŀ��������
		NdisStatus = NdisOpenAdapterEx(
			Globals.NdisProtocolHandle ,
			_pOpenContext ,	//�ᴫ�뵽OpenComplete��������
			&OpenParameters ,
			_BindContext ,
			&_pOpenContext->BindingHandle );
		//�ȴ��������
		if (NdisStatus == NDIS_STATUS_PENDING)
		{
			NdisWaitEvent( &_pOpenContext->BindEvent , 0 );
			NdisStatus = _pOpenContext->BindStatus;
		}

		//������ɹ�
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			KdPrint( ("_BindAdapter.NdisOpenAdapter failed,status=%x\n" ,
				NdisStatus) );
			break;
		}

		//��ʱ�����Ѿ��ɹ�����,���ǻ�û�и���flags��״̬,
		//����Ϊ�˱������߳̿�ʼ�ر������
		bOpenComplete = TRUE;

		/*
		//��������,���һ�����Ķ�������,����һ��Ҫ�ɹ�,���Բ���鷵��ֵ
		NdisQueryAdapterInstanceName(
			&_pOpenContext->DeviceDescr ,
			_pOpenContext->BindingHandle );
		*/

		//�������mac��ַ
		_pOpenContext->CurrentAddress[0] = 
			_pBindParameters->CurrentMacAddress[0];
		_pOpenContext->CurrentAddress[1] =
			_pBindParameters->CurrentMacAddress[1];
		_pOpenContext->CurrentAddress[2] =
			_pBindParameters->CurrentMacAddress[2];
		_pOpenContext->CurrentAddress[3] =
			_pBindParameters->CurrentMacAddress[3];
		_pOpenContext->CurrentAddress[4] =
			_pBindParameters->CurrentMacAddress[4];
		_pOpenContext->CurrentAddress[5] =
			_pBindParameters->CurrentMacAddress[5];

		//�������ѡ��
		_pOpenContext->MacOptions = _pBindParameters->MacOptions;

		//������֡��
		_pOpenContext->MaxFrameSize = _pBindParameters->MtuSize;

		//����²�����״̬
		NDIS_MEDIA_CONNECT_STATE	MediaConnectStatus =
			_pBindParameters->MediaConnectState;
		//������״̬������Flags��
		if (MediaConnectStatus == NdisMediaStateConnected)
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_CONNECTED );
		else
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_DISCONNECTED );
		
		//���ñ��
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE );
		//�����ʱ���Ƿ������һ������󶨵�flags
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_UNBIND_FLAGS_MASK , NDIS_UNBIND_RECEIVED ))
		{
			//����������ΰ�ʧ��
			NdisStatus = NDIS_STATUS_FAILURE;
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

	} while (FALSE);

	//���û�гɹ�,����bDoNotDisturbΪFLASE
	if ((NdisStatus != NDIS_STATUS_SUCCESS) && !bDoNotDisturb)
	{
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		//����Ѿ��ɹ��İ���
		if (bOpenComplete)
		{
			//����Ѿ��󶨽�����,�������Ѿ��󶨱��
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE );
		}
		else if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_OPENING ))
		{
			//��������ڰ󶨹�����,�����ð�ʧ����
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_FAILED );
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		//���ý���󶨺���,������ͷ�������Դ
		_UnbindAdapter( _pOpenContext );
	}

	return	NdisStatus;
}


/*
�ú�����Ҫ������:
1.�������ĵķ���ͳ�ʼ��
2.������������ı��浽ȫ������,������BindAdapter��ʽ��ɰ�
����:
ProtocolDriverContext	������ע��ndisЭ��ʱ�����Զ��������Ĳ���,������û��ʹ��
BindContext				The handle that identifies the NDIS context area for this bind operation.
pBindParameters			ָ����Ҫ�󶨵��������Ĳ�����ָ��
����ֵ:	����״̬
*/
PROTOCOL_BIND_ADAPTER_EX	_BindAdapterHandlerEx;
NDIS_STATUS	_BindAdapterHandlerEx(
	IN	NDIS_HANDLE	_ProtocolDriverContext,
	IN	NDIS_HANDLE	_BindContext,
	IN	PNDIS_BIND_PARAMETERS	_pBindParameters
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;
	NDIS_STATUS	NdisStatus;

	do
	{
		//�����������,����������������󶨵���Ϣ
		pOpenContext =
			NdisAllocateMemoryWithTagPriority(	//�ú����������non-page pool
			Globals.NdisProtocolHandle ,
			sizeof( NDIS_OPEN_CONTEXT ) ,
			POOL_TAG ,
			NormalPoolPriority );
		if (pOpenContext == NULL)
		{
			NdisStatus = NDIS_STATUS_RESOURCES;
			break;
		}

		//�ڴ���0
		NdisZeroMemory( pOpenContext , sizeof( NDIS_OPEN_CONTEXT ) );

		//������ռ�дһ����������,����ʶ���д�
		pOpenContext->oc_sig = 'hq';

		//��ʼ���������еļ�����Ա:
		//�� ������ д���� ������ ��Դ���¼�
		NdisAllocateSpinLock( &pOpenContext->Lock );
		InitializeListHead( &pOpenContext->PendedReads );
		InitializeListHead( &pOpenContext->PendedWrites );
		InitializeListHead( &pOpenContext->RecvPktQueue );
		NdisInitializeEvent( &pOpenContext->PoweredUpEvent );

		//��Ϊ��ʼ��Դ�Ǵ򿪵�
		NdisSetEvent( &pOpenContext->PoweredUpEvent );

		//���Ӵ������ĵ����ü���
		NdisInterlockedIncrement( &pOpenContext->RefCount );

		//���������ı��浽ȫ���������Ա�֮�����
		//�������Ҫ����
		NdisAcquireSpinLock( &Globals.SpinLock );
		InsertTailList( &Globals.OpenList ,
			&pOpenContext->Link );
		NdisReleaseSpinLock( &Globals.SpinLock );

		//��ʽ�󶨹���
		NdisStatus = _BindAdapter(
			pOpenContext ,
			_pBindParameters,
			_BindContext);
		if (NdisStatus != NDIS_STATUS_SUCCESS)
			break;

	} while (FALSE);

	return	NdisStatus;
}


/*
�ú�����������:
1.������߳̾�������
2.ֹͣ�ӷ����ݰ�
3.���������δ�������(���ߵȴ����,����ȡ����)
4.����NdisCloseAdapter
5.��������д��������з������Դ
*/
VOID	_UnbindAdapter(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
)
{
	NDIS_STATUS	NdisStatus;
	BOOLEAN	bDoCloseBinding = FALSE;

	do
	{
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		//�����,������ڴ�,�������˳�,��������󶨲���
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_OPENING ))
		{
			NdisReleaseSpinLock( &_pOpenContext->Lock );
			break;
		}

		//������Ѿ����,������Ϊ��ʼ�����
		//�������������������Ĳ���,��Ϊ�����δ���,�Ͳ��ý����
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_CLOSING );
			bDoCloseBinding = TRUE;
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		/*
		����Ҫ���еĲ���:
		1.�����Э�������İ����˱������Ϊȫ��,ʹ֮���ٽ��յ���
		2.�����Э��Ľ��չ㲥���б�����Ϊ��,ʹ���Э�鲻�ٽ����κ����ݰ�
		*/
		if (bDoCloseBinding)
		{
			ULONG	PacketFilter = 0;
			ULONG	BytesRead = 0;

			//�����������������İ�������Ϊ0,ֹͣ�հ�
			NdisStatus = _NdisDoRequest(
				_pOpenContext ,
				NdisRequestSetInformation ,
				OID_GEN_CURRENT_PACKET_FILTER ,
				&PacketFilter ,
				sizeof( PacketFilter ) ,
				&BytesRead );
			if (NdisStatus != NDIS_STATUS_SUCCESS)
			{
				KdPrint( ("_UnbindAdapter: Fail to set packet filter,status=%x\n" , NdisStatus) );
			}

			//������������Ĺ㲥�б�ΪNULL
			NdisStatus = _NdisDoRequest(
				_pOpenContext ,
				NdisRequestSetInformation ,
				OID_802_3_MULTICAST_LIST ,
				NULL , 0 ,
				&BytesRead );
			if (NdisStatus != NDIS_STATUS_SUCCESS)
			{
				KdPrint( ("_UnbindAdapter:Failt to set multicast list to null,status=%x\n" , NdisStatus) );
			}

			//ȡ�����е��ύ״̬IRP
			_ServiceIndicateStatusIrp(
				_pOpenContext ,
				0 ,
				NULL ,
				0 ,
				TRUE );

			// �ȴ�����δ��IRP���
			_WaitForPendingIrp( _pOpenContext , TRUE );

			//��������ն��������еİ�
			_FlushReceiveQueue( _pOpenContext );

			//��ʽ���ý���󶨺���,�ȴ��¼���״̬������ɺ���������
			NdisStatus = 
				NdisCloseAdapterEx( _pOpenContext->BindingHandle );
			if (NdisStatus == NDIS_STATUS_PENDING)
			{
				NdisWaitEvent( &_pOpenContext->BindEvent , 0 );
				NdisStatus = _pOpenContext->BindStatus;
			}
			ASSERT( NdisStatus == NDIS_STATUS_SUCCESS );
			_pOpenContext->BindingHandle = NULL;

			if (bDoCloseBinding)
			{
				//�����Ѿ�����󶨵ı��
				NdisAcquireSpinLock( &_pOpenContext->Lock );
				NDIS_SET_FLAGS( _pOpenContext->Flags ,
					NDIS_BIND_FLAGS_MASK , NDIS_BIND_IDLE );
				NDIS_SET_FLAGS( _pOpenContext->Flags ,
					NDIS_UNBIND_FLAGS_MASK , 0 );
				NdisReleaseSpinLock( &_pOpenContext->Lock );
			}

			//�ͷ���Դ
			NdisAcquireSpinLock( &Globals.SpinLock );
			RemoveEntryList( &_pOpenContext->Link );
			NdisReleaseSpinLock( &Globals.SpinLock );
			_FreeContextResoureces( _pOpenContext );
			//����һ�δ������ĵ�����,���Ϊ0�ͳ���ɾ��
			if (NdisInterlockedDecrement(
				&_pOpenContext->RefCount ) == 0)
			{
				//free it 
				NdisFreeSpinLock( &_pOpenContext->Lock );
				NdisFreeMemory( _pOpenContext , 0 , 0 );
			}

		} // if (bDoCloseBinding)

	} while (FALSE);
}


/*
�ú�������:
1.���ý����NDIS_UNBIND_RECEIVED���
�ÿ��ܴ��ڵ�ĳ�����԰󶨵Ĳ���֪���Ѿ���ʼ�������
2.���õ�Դ�����¼�,�Ա�һЩδ������������
3.����_UnbindAdapter�������ʣ�๤��
����:
pStatus		�������ز���״̬
ProtocolBindingContext	��������
UnbindContext	�ò���
����ֵ:��
*/
PROTOCOL_UNBIND_ADAPTER_EX	_UnbindAdapterHandlerEx;
NDIS_STATUS	_UnbindAdapterHandlerEx(
	IN	NDIS_HANDLE		_UnbindContext,
	IN	NDIS_HANDLE		_ProtocolBindingContext
)
{
	UNREFERENCED_PARAMETER( _UnbindContext );

	PNDIS_OPEN_CONTEXT	pOpenContext =
		(PNDIS_OPEN_CONTEXT)_ProtocolBindingContext;

	//����,׼�����ñ��
	NdisAcquireSpinLock( &pOpenContext->Lock );
	NDIS_SET_FLAGS( pOpenContext->Flags ,
		NDIS_UNBIND_FLAGS_MASK , NDIS_UNBIND_RECEIVED );
	//�����¼�,֪ͨ�����ڵȴ��ĵ�Դ�������߳�,����һЩ�����޷����
	NdisSetEvent( &pOpenContext->PoweredUpEvent );
	NdisReleaseSpinLock( &pOpenContext->Lock );

	//ʣ�����
	_UnbindAdapter( pOpenContext );

	return NDIS_STATUS_SUCCESS;
}


/*
�ú�����������������OID�����ȡ��Ϣ
����:
pOpenContext	��������
RequestType		��������
Oid				�������ͺ� Object Identifiers
InformationBuffer ��Ϣ��ŵĻ�����
InformationBufferLength �������Ĵ�С
pBytesProcessed	����������Ч����ĳ���
����ֵ:����״̬
*/
NDIS_STATUS	_NdisDoRequest(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_REQUEST_TYPE	_RequestType ,
	IN	NDIS_OID	_Oid ,
	IN	PVOID	_pInformationBuffer ,
	IN	ULONG	_InformationBufferLength ,
	OUT	PULONG	_pBytesProcessed
)
{
	NDIS_REQUEST_CONTEXT	RequestContext = { 0 };
	PNDIS_OID_REQUEST	pNdisRequest =
		&RequestContext.NdisRequest;
	NDIS_STATUS	NdisStatus;

	//��ʼ�������¼�,����¼������������ɺ����б�����
	//����֪ͨ���������
	NdisInitializeEvent( &RequestContext.ReqEvent );

	//��ʼ��NDIS_OID_REQUEST�ṹ
	pNdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
	pNdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
	pNdisRequest->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
	//������������,���ֻ�ǲ�ѯ��Ϣ,ֻҪ��
	//ndisrequestqueryInformation������
	pNdisRequest->RequestType = _RequestType;
	pNdisRequest->PortNumber = 0;

	//���ݲ�ͬ����������,��дOID������/���������
	switch (_RequestType)
	{
		case NdisRequestQueryInformation:
			pNdisRequest->DATA.QUERY_INFORMATION.Oid = _Oid;
			pNdisRequest->
				DATA.QUERY_INFORMATION.InformationBuffer = _pInformationBuffer;
			pNdisRequest->
				DATA.QUERY_INFORMATION.InformationBufferLength = _InformationBufferLength;
			break;

		case NdisRequestSetInformation:
			pNdisRequest->DATA.SET_INFORMATION.Oid = _Oid;
			pNdisRequest->
				DATA.SET_INFORMATION.InformationBuffer = _pInformationBuffer;
			pNdisRequest->
				DATA.SET_INFORMATION.InformationBufferLength = _InformationBufferLength;
			break;

		default:
			ASSERT( FALSE );
			break;
	}

	/*	
		Ndis 5x�ĵ��÷�ʽ
	//��������
	NdisRequest(
		&NdisStatus ,
		_pOpenContext->BindingHandle ,
		pNdisRequest );
	*/

	//Ndis 6.0�ĵ��÷�ʽ
	NdisStatus = NdisOidRequest(
		_pOpenContext->BindingHandle , pNdisRequest );
	//�����δ��״̬,��ȴ��¼�,�¼�������ɺ���������
	if (NdisStatus == NDIS_STATUS_PENDING)
	{
		NdisWaitEvent( &RequestContext.ReqEvent , 0 );
		NdisStatus = RequestContext.Status;
	}
	//����ɹ���
	if (NdisStatus == NDIS_STATUS_SUCCESS)
	{
		//��ý������,������������ʵ����Ҫ�ĳ���
		//���ܱ�����ʵ���ṩ�ĳ���Ҫ��
		*_pBytesProcessed =
			(_RequestType == NdisRequestQueryInformation) ?
			pNdisRequest->
			DATA.QUERY_INFORMATION.BytesWritten :
			pNdisRequest->
			DATA.SET_INFORMATION.BytesRead;

		//���������ȱ��ṩ��BufferLengthҪ��,�ͼ򵥵���������Ϊ��󳤶�
		if (*_pBytesProcessed > _InformationBufferLength)
			*_pBytesProcessed = _InformationBufferLength;
	}

	return	NdisStatus;
}


/*
�ú������״̬IRP����,���IRP����ȡ��
���ø�IRP��ȡ������������IRP
����:
pOpenContext	��������
GeneralStatus	״̬
StatusBuffer	�����״̬��Ϣ
StatusBufferSize�����״̬��Ϣ�Ĵ�С
Cancel			IRP�Ƿ�������ȡ��
����ֵ:	��
*/
VOID	_ServiceIndicateStatusIrp(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	NDIS_STATUS	_GeneralStatus ,
	IN	PVOID	_pStatusBuffer ,
	IN	UINT	_StatusBufferSize ,
	IN	BOOLEAN	_bCancel
)
{
	NTSTATUS	status;
	PIRP	pIrp = NULL;
	PIO_STACK_LOCATION	pIrpStack = NULL;
	PNDIS_INDICATE_STATUS	pIndicateStatus = NULL;
	ULONG	InBufLength , OutBufLength;
	ULONG	Bytes;

	KdPrint( ("[_ServiceIndicateStatusIrp]\n") );

	NdisAcquireSpinLock( &_pOpenContext->Lock );
	pIrp = _pOpenContext->StatusIndicationIrp;

	do
	{
		if (pIrp)
		{
			pIrpStack = IoGetCurrentIrpStackLocation( pIrp );
			pIndicateStatus = pIrp->AssociatedIrp.SystemBuffer;
			InBufLength =
				pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
			OutBufLength =
				pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;

			//�鿴�Ƿ���ȡ������
			if (IoSetCancelRoutine( pIrp , NULL ))
			{
				//û��ȡ������
				status = STATUS_CANCELLED;

				if (!_bCancel)
				{
					//��������ȷ������ȡ��

					//��黺�����Ƿ��㹻��
					if(OutBufLength >= sizeof(NDIS_INDICATE_STATUS)
						&& ((OutBufLength - sizeof( NDIS_INDICATE_STATUS ) >= _StatusBufferSize)))
					{
						pIndicateStatus->IndicatedStatus = _GeneralStatus;
						pIndicateStatus->StatusBufferLength = _StatusBufferSize;
						pIndicateStatus->StatusBufferOffset = sizeof( NDIS_INDICATE_STATUS );

						NdisMoveMemory(
							pIndicateStatus + pIndicateStatus->StatusBufferOffset ,
							_pStatusBuffer ,
							_StatusBufferSize );
						status = STATUS_SUCCESS;

					}// if(OutBufLength >= sizeof(NDIS_INDICATE_STATUS)
					 //   && ((OutBufLength - sizeof( NDIS_INDICATE_STATUS ) >= _StatusBufferSize)))
					else
					{
						//������̫С
						status = STATUS_BUFFER_OVERFLOW;
					}
				}// if (!_bCancel)

				//clear field
				_pOpenContext->StatusIndicationIrp = NULL;
				//�������ֽ�����������Ҫ���ֽ�����
				Bytes =
					sizeof( NDIS_INDICATE_STATUS ) + _StatusBufferSize;
				break;
			}// if (IoSetCancelRoutine( pIrp , NULL ))
			else
			{
				//���Irp��ȡ������,����
				pIrp = NULL;
			}
		}//  if (pIRp)
	} while (FALSE);

	NdisReleaseSpinLock( &_pOpenContext->Lock );

	if (pIrp)
	{
		pIrp->IoStatus.Information = Bytes;
		pIrp->IoStatus.Status = status;
		IoCompleteRequest( pIrp , IO_NO_INCREMENT );
	}
	return;
}


/*
�ú����ȴ���������������IRP�������
����:
pOpenContext	��������
bDoCancelReads	�Ƿ����ȡ����Щδ���Ķ�IRP
����ֵ:	��
*/
VOID	_WaitForPendingIrp(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	BOOLEAN	_bDoCancelReads
)
{
	NDIS_STATUS	NdisStatus;
	ULONG	LoopCount;
	ULONG	PendingCount;

	//ȷ��������û��δ���͵İ�,����о͵�һ��
	for (LoopCount = 0; LoopCount < 60; LoopCount++)
	{
		if (_pOpenContext->PendedSendCount == 0)
			break;

		KdPrint( ("_WaitForPendingIrp: Wait for pended send count=%d\n" ,
			_pOpenContext->PendedSendCount) );

		//˯1��
		NdisMSleep( 1000000 );
	}
	ASSERT( LoopCount < 60 );

	if (_bDoCancelReads)
	{
		//�ȴ���IRP��ɻ���ȡ��
		while (_pOpenContext->PendedReadCount != 0)
		{
			KdPrint( ("_WaitForPendingIrp: Wait for pended read count=%d\n" ,
				_pOpenContext->PendedReadCount) );

			//ȡ��������
			_CancelPendingReads( _pOpenContext );

			//˯һ��
			NdisMSleep( 1000000 );
		}
	}
}


/*
�ú���ȡ��������Ķ�����
����:
pOpenContext	��������
����ֵ:	��
*/
VOID	_CancelPendingReads(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
)
{
	PIRP	pIrp;
	PLIST_ENTRY	pIrpEntry;

	NdisInterlockedIncrement( &_pOpenContext->RefCount );
	NdisAcquireSpinLock( &_pOpenContext->Lock );

	while (!IsListEmpty( &_pOpenContext->PendedReads ))
	{
		pIrpEntry =
			_pOpenContext->PendedReads.Flink;
		pIrp =
			CONTAINING_RECORD( pIrpEntry ,
			IRP , Tail.Overlay.ListEntry );

		//����Ƿ����ڱ�ȡ��
		if (IoSetCancelRoutine( pIrp , NULL ))
		{
			//û�б�ȡ��
			RemoveEntryList( pIrpEntry );
			NdisReleaseSpinLock( &_pOpenContext->Lock );

			//������Irp
			pIrp->IoStatus.Status = STATUS_CANCELLED;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest( pIrp , IO_NO_INCREMENT );

			//������Ϊδ��IRP���ӵĴ������ĵ�����
			_NdisDereferenceOpenContext( _pOpenContext );
			NdisAcquireSpinLock( &_pOpenContext->Lock );
			_pOpenContext->PendedReadCount--;

		}// if(IoSetCancelRoutine(pIrp,NULL))
		else
		{
			//���ڱ�ȡ��,������ȡ�����̴������IRP
			NdisReleaseSpinLock( &_pOpenContext->Lock );

			NdisMSleep( 1000000 );

			NdisAcquireSpinLock( &_pOpenContext->Lock );
		}
	}

	NdisReleaseSpinLock( &_pOpenContext->Lock );
	_NdisDereferenceOpenContext( _pOpenContext );
}


/*
�ú�����������������еĽ��ն��еİ�
����:
pOpenContext	��������
����ֵ:	��
*/
VOID	_FlushReceiveQueue(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
)
{
	PLIST_ENTRY	pRcvPacketEntry;
	PNET_BUFFER_LIST	pRcvNetBufferList;

	NdisInterlockedIncrement( &_pOpenContext->RefCount );
	NdisAcquireSpinLock( &_pOpenContext->Lock );

	while (!IsListEmpty( &_pOpenContext->RecvPktQueue ))
	{
		pRcvPacketEntry = _pOpenContext->RecvPktQueue.Flink;
		RemoveEntryList( pRcvPacketEntry );
		_pOpenContext->RecvPktCount--;

		NdisReleaseSpinLock( &_pOpenContext->Lock );

		pRcvNetBufferList =
			CONTAINING_RECORD( pRcvPacketEntry ,
			NDIS_RECV_PACKET_RSVD , Link );

		_FreeReceiveNetBufferList( _pOpenContext , pRcvNetBufferList );
		_NdisDereferenceOpenContext( _pOpenContext );

		NdisAcquireSpinLock( &_pOpenContext->Lock );
	}

	NdisReleaseSpinLock( &_pOpenContext->Lock );

	_NdisDereferenceOpenContext( _pOpenContext );
}


/*
�ú����ڽ���󶨺���NdisCloseAdapter���ú�ص�
��Ҫ���õȴ��¼��ͱ������״̬
����:
ProtocolBindingContext	��������ָ��
NdisStatus		����״̬
����ֵ:	��
*/
PROTOCOL_CLOSE_ADAPTER_COMPLETE_EX	_CloseAdapterComplete;
VOID	_CloseAdapterComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext 
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;

	pOpenContext = _ProtocolBindingContext;

	//����״̬
	pOpenContext->BindStatus = NDIS_STATUS_SUCCESS;

	//�����¼����ź�
	NdisSetEvent( &pOpenContext->BindEvent );
}


/*
�ú�������NdisOpenAdapter���֮��ص�,��Ҫ�����¼��õȴ����̼߳���ִ��
����:
pProtocolBindingContext ����OpenAdaterExʱ�����OpenContext
NdisStatus ����ɵ�״̬
����ֵ:��
*/
PROTOCOL_OPEN_ADAPTER_COMPLETE_EX	_OpenAdapterComplete;
VOID	_OpenAdapterComplete(
	IN	NDIS_HANDLE	_pProtocolBindingContext ,	//����OpenAdaterExʱ�����OpenContext
	IN	NDIS_STATUS	_NdisStatus
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;

	pOpenContext =
		(PNDIS_OPEN_CONTEXT)_pProtocolBindingContext;

	pOpenContext->BindStatus = _NdisStatus;

	//���õȴ��¼�
	NdisSetEvent( &pOpenContext->BindEvent );
}
