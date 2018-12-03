/*
	Э���������еĴ������
*/
#include	<ndis_shared_head.h>


/*
�ú�����OID������ɺ�ص�,��Ҫ�����¼�,�����������status
����:
ProtocolBindingContext	�豸�Ĵ�������
pNdisRequest	����
status	�������״̬
����ֵ:��
*/
PROTOCOL_OID_REQUEST_COMPLETE	_RequestComplete;
VOID	_RequestComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNDIS_OID_REQUEST	_pNdisRequest ,
	IN	NDIS_STATUS	_NdisStatus
)
{
	PNDIS_REQUEST_CONTEXT	pReqContext;

	//��pNDISRequest�еõ�����������
	pReqContext = CONTAINING_RECORD(
		_pNdisRequest , NDIS_REQUEST_CONTEXT , NdisRequest );

	//������״̬
	pReqContext->Status = _NdisStatus;

	//�����¼�
	NdisSetEvent( &pReqContext->ReqEvent );
}


/*
�ú����ڰ�������֮�󱻻ص�
����:	���ٸ������ü���,����IRP����
����:
ProtocolBindingContext	�Ͱ��йصĴ�������ָ��
pNetBufferList			�ѷ��͵�NetBufferList
SendCompleteFlags		����ʱָ����SendFlags
����ֵ:��
*/
PROTOCOL_SEND_NET_BUFFER_LISTS_COMPLETE	_SendComplete;
VOID	_SendComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList ,
	IN	ULONG		_SendCompleteFlags
)
{
	PIRP	pIrp;
	PIO_STACK_LOCATION	pIrpStack;
	PNDIS_OPEN_CONTEXT	pOpenContext;

	//��ô�������
	pOpenContext = _ProtocolBindingContext;

	//�Ӱ��������л��IRpָ��
	pIrp = ((PNDIS_SEND_PACKET_RSVD)
		_pNetBufferList->Context->ContextData)->pIrp;
	//���ݰ�������
	if (
		NdisInterlockedDecrement(
		&((PNDIS_SEND_PACKET_RSVD)
		_pNetBufferList->Context->ContextData)->RefCount ) == 0
		)
	{
		NdisFreeNetBufferList( _pNetBufferList );
	}

	//δ�����Ͱ�������1
	NdisInterlockedDecrement( &pOpenContext->PendedSendCount );
	//�����������ü�1
	_NdisDereferenceOpenContext( pOpenContext );

	//���Irp����
	pIrpStack = IoGetCurrentIrpStackLocation( pIrp );
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information =
		pIrpStack->Parameters.Write.Length;
	IoCompleteRequest( pIrp , IO_NO_INCREMENT );

	KdPrint( ("_SendComplete: Packet completed success!\n") );
}


/*
�ú�����һ���յ��İ�������������,
�����������,�ʹӶ���ͷɾ��һ����
������д����������еĺ���
����:
pOpenContext	��������
_pNetBufferList		�յ��İ�
����ֵ:��
*/
VOID	_QueueNetBufferList(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList
)
{
	PLIST_ENTRY	pEntry;
	PLIST_ENTRY	pDiscardEntry;
	PNET_BUFFER_LIST	pDiscardNetBufferList;

	do
	{
		//��ý��ն���
		pEntry =
			&((PNDIS_RECV_PACKET_RSVD)
			_pNetBufferList->Context->ContextData)->Link;

		//���ӶԴ������ĵ�����
		NdisInterlockedIncrement( &_pOpenContext->RefCount );

		//������ڻ״̬,���ҵ�Դ״̬��ȷ,�Ͱ��������������
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE )
			&&
			_pOpenContext->PowerState == NetDeviceStateD0)
		{
			InsertTailList(
				&_pOpenContext->RecvPktQueue ,
				pEntry );
			_pOpenContext->RecvPktCount++;

		}
		else
		{
			// ״̬���Ծ��ͷ������;
			_FreeReceiveNetBufferList( _pOpenContext , _pNetBufferList );
			//������
			_NdisDereferenceOpenContext( _pOpenContext );
			break;
		}

		//������뻺�������̫����,��ɾһ��
		if (_pOpenContext->RecvPktCount > MAX_RECV_QUEUE_SIZE)
		{
			//Ҫɾ���Ľڵ�
			pDiscardEntry = _pOpenContext->RecvPktQueue.Flink;
			RemoveEntryList( pDiscardEntry );

			//���ٸո����ӵ�����
			_pOpenContext->RecvPktCount--;

			//��øýڵ���İ�������ָ��
			pDiscardNetBufferList =
				CONTAINING_RECORD( pDiscardEntry , NET_BUFFER_LIST , Context->ContextData );
			//�Ѱ��ͷŵ�
			_FreeReceiveNetBufferList( _pOpenContext , pDiscardNetBufferList );
			//�������Ľ�����
			_NdisDereferenceOpenContext( _pOpenContext );
			KdPrint( ("_QueueReceivePacket: Queue too long,discard one !\n") );
		}

		//���ú����鿴�Ƿ���δ���Ķ�����,�о��������
		//_NdisServiceReads( _pOpenContext );

	} while (FALSE);
}


/*
�ú����ͷ�һ��NetBufferList,�ͷŰ������������Դ
�������һ���Լ������İ�,���ͷŻش������ĵĽ��հ���,���Ǿͷ��ظ�ndis
����:
pOpenContext		��������
pNetBufferList		Ҫ�ͷŵ�NetBufferList
*/
VOID	_FreeReceiveNetBufferList(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList
)
{
	PNDIS_BUFFER	pNdisBuffer;
	UINT	TotalLength,BufferLength;
	PUCHAR	pCopyData;

	NdisFreeNetBufferList( _pNetBufferList );
}

//
///*
//�ú������ܵ��İ�������ݿ������û�����������ɶ�����
//����	
//pOpenContext	��������
//����ֵ:	��
//*/
//VOID	_NdisServiceReads(
//	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
//)
//{
//	PIRP	pIrp = NULL;
//	PLIST_ENTRY	pIrpEntry,pRcvNetBufferListEntry;
//	PNDIS_PACKET	pRcvPacket;
//	PNDIS_BUFFER	pNdisBuffer;
//	PUCHAR	pSrc , pDst;
//	ULONG	BytesRemainingInUserBuffer;
//	ULONG	BytesInNdisBuffer;
//	BOOLEAN	bFoundPendingIrp;
//
//	NdisInterlockedIncrement( &_pOpenContext->RefCount );	//��ֹ�󶨱����
//
//	NdisAcquireSpinLock( &_pOpenContext->Lock );	//����Ϊ�˶�ռʹ��
//
//	//ֻҪ��������кͽ��հ����в�ͬʱΪ��,�Ϳ�����
//	while (!IsListEmpty( &_pOpenContext->PendedReads )
//		&& !IsListEmpty( &_pOpenContext->RecvPktQueue ))
//	{
//		bFoundPendingIrp = FALSE;
//
//		//��õ�һ��δ��������
//		pIrpEntry = _pOpenContext->PendedReads.Flink;
//		while (pIrpEntry != &_pOpenContext->PendedReads)
//		{
//			//������ڵ�ĵ�Irpָ��
//			pIrp =
//				CONTAINING_RECORD( pIrpEntry ,
//				IRP , Tail.Overlay.ListEntry );
//
//			//�����������Ƿ����ڱ�ȡ��
//			if (IoSetCancelRoutine( pIrp , NULL ))
//			{
//				//û��ȡ��,����iRp����
//				RemoveEntryList( pIrpEntry );
//				bFoundPendingIrp = TRUE;
//				break;
//			}
//			else
//			{
//				//�������ȡ��,���������IRp
//				KdPrint( ("_NdisServiceReads: Skip cancelled Irp\n") );
//
//				pIrpEntry = pIrpEntry->Flink;
//			}
//
//		} // end while (pIrpEntry != &_pOpenContext->PendedReads)
//
//		//���û��Irp,ֱ����������
//		if (bFoundPendingIrp == FALSE)
//			break;
//
//		//�õ���һ�����ڵ�(��ɵ�),������
//		pRcvNetBufferListEntry =
//			_pOpenContext->RecvPktQueue.Flink;
//		RemoveEntryList( pRcvNetBufferListEntry );
//		//δ�������1
//		_pOpenContext->RecvPktCount--;
//		_NdisDereferenceOpenContext( _pOpenContext );
//
//		//�ӽڵ��ð�������
//		pRcvPacket =
//			CONTAINING_RECORD( pRcvNetBufferListEntry ,
//			NET_BUFFER_LIST , Context->ContextData );
//
//		//�õ�IRP�������ַ,Ȼ����������������ݵ��û�������
//		pDst =
//			MmGetSystemAddressForMdlSafe(
//			pIrp->MdlAddress , NormalPagePriority );
//		ASSERT( pDst != NULL );
//		BytesRemainingInUserBuffer
//			= MmGetMdlByteCount( pIrp->MdlAddress );
//		//��õ�һ����������������
//		pNdisBuffer = pRcvPacket->Private.Head;
//
//		// ��ע�⣬ÿ��PNDIS_BUFFER����һ��PMDL��ͬʱPNDIS_BUFFER
//		// ������������NdisGetNextBuffer���Դ�һ���õ���������һ����
//		// ��������ʵ�����Ǳ�����һ������������������ġ�
//		//����ѭ������ֹ��������Ҫô�û���������,Ҫô������û���ݿɿ���
//		while (BytesRemainingInUserBuffer && (pNdisBuffer != NULL))
//		{
//			NdisQueryBufferSafe(
//				pNdisBuffer , &pSrc ,
//				&BytesInNdisBuffer , NormalPagePriority );
//			if (pSrc == NULL)
//			{
//				KdPrint( ("_NdisServiceReads.NdisQueryBufferSafe: failed to query pSrc!\n") );
//				break;
//			}
//
//			//��������Լ�������,�ͼ�������
//			if (BytesInNdisBuffer)
//			{
//				ULONG	BytesToCopy =
//					min( BytesRemainingInUserBuffer , BytesInNdisBuffer );
//				NdisMoveMemory( pDst , pSrc , BytesToCopy );
//				BytesRemainingInUserBuffer -= BytesToCopy;
//				pDst += BytesToCopy;	//�û�������ָ������ƫ��
//			}
//
//			//�õ��ð�����һ��������������
//			NdisGetNextBuffer( pNdisBuffer , &pNdisBuffer );
//
//		} //end while (BytesRemainingInUserBuffer && (pNdisBuffer != NULL))
//
//		//������֮�������IRp����
//		pIrp->IoStatus.Status = STATUS_SUCCESS;
//
//		//information���ŵ����������û���������д���ֽ���
//		pIrp->IoStatus.Information =
//			MmGetMdlByteCount( pIrp->MdlAddress ) - BytesRemainingInUserBuffer;
//
//		KdPrint( ("_NdisServiceReads: Complete IRP and write %d bytes\n" ,
//			pIrp->IoStatus.Information) );
//		IoCompleteRequest( pIrp , IO_NO_INCREMENT );
//
//		//�ͷŰ�
//		// �����������������Ǵӽ��հ��������ģ���ô���Ǵ�
//		// �������������õġ���������õģ�����NdisReturnPackets
//		// �黹�����������������ͷš�
//		if (NdisGetPoolFromPacket( pRcvPacket ) != _pOpenContext->RecvNetBufferListPool)
//		{
//			NdisReturnPackets( &pRcvPacket , 1 );
//		}
//		else
//		{
//			//���Լ�����İ�,�Լ��ͷ�
//			_FreeReceiveNetBufferList( _pOpenContext , pRcvPacket );
//		}
//
//		//������
//		_NdisDereferenceOpenContext( _pOpenContext );
//		//�������1
//		_pOpenContext->PendedReadCount--;
//
//	} // end while (!IsListEmpty( &_pOpenContext->PendedReads )
//	//	&& !IsListEmpty( &_pOpenContext->RecvPktQueue ))
//
//	//�����ý���
//	NdisReleaseSpinLock( &_pOpenContext->Lock );
//	_NdisDereferenceOpenContext( _pOpenContext );
//}
//

/*
�ú������������ϵİ�,�����յ����Ѿ���֯�õ�NetBufferList
���ǿ���ֱ��ʹ�����NetBufferList,������²���Դ����ʱ����Ҫ�Լ�
����һ��NetBufferList���ҿ���
����:
ProtocolBindingContext	��������
pNetBufferList			���յ���NetBufferList��ָ��
PortNumber				��ʶС�˿������Ķ˿ں�,�ò���
NumberOfNetBufferList	NetBufferList������
_ReceiveFlags			���ձ�ʶ
����ֵ:	��
*/
PROTOCOL_RECEIVE_NET_BUFFER_LISTS	_ReceiveNetBufferList;
VOID	_ReceiveNetBufferList(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_BUFFER_LIST	_pNetBufferList,
	IN	NDIS_PORT_NUMBER	_PortNumber,
	IN	ULONG			_NumberOfNetBufferList,
	IN	ULONG			_ReceiveFlags
)
{
	NDIS_STATUS	NdisStatus = NDIS_STATUS_SUCCESS;
	PNDIS_OPEN_CONTEXT	pOpenContext;
	PNDIS_BUFFER	pNdisBuffer=NULL,pNewNdisBuffer=NULL;
	PUCHAR			pNewBuffer=NULL;
	PNDIS_ETH_HEADER	pEthHeader;
	UINT	BufferLength;
	UINT	TotalPacketLength;
	UINT	BytesCopied;
	INT		RefCount;

	pOpenContext = 
		(PNDIS_OPEN_CONTEXT)_ProtocolBindingContext;

	do
	{
		BufferLength =
			NET_BUFFER_DATA_LENGTH( _pNetBufferList->FirstNetBuffer );
		//���������ĳ��ȱ���̫����ͷҪС����
		if (BufferLength < sizeof( NDIS_ETH_HEADER ))
		{
			NdisStatus = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}

		//����������NDIS_RECEIVE_FLAGS_RESOURCES״̬,����뿽��
		//��������
		if (_ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES)
		{
			//��ʱ�������µ�NetBufferList,����ֱ�ӽ����
			//NetBufferList���ظ�ndis
			NdisReturnNetBufferLists(
				pOpenContext->BindingHandle ,
				_pNetBufferList , 0 );
		} // if NDIS_RECEIVE_FLAGS_RESOURCES
		else
		{
			//�����������
			_QueueNetBufferList( pOpenContext , _pNetBufferList );
		}

	} while (FALSE);

	//ʧ��Ҫ�ͷ���Դ
	if (NdisStatus != NDIS_STATUS_SUCCESS)
	{
		if (pNewNdisBuffer != NULL)
		{
			NdisFreeBuffer( pNewNdisBuffer );
		}
		if (pNewBuffer != NULL)
		{
			NdisFreeMemory( pNewBuffer , 0 , 0 );
		}
	}
}


/*
�ú�����С�˿�������ɱ����յ�һ�����ݰ�ʱ����
��ִ��ʲô����
����:
ProtocolBindingContext	��������ָ��
����ֵ:	��
*/
VOID	_ReceiveComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
)
{
	return;
}


/*
�ú�����С�˿���������״̬�����仯ʱ����,
��������reset��media connect ��״̬
����:
ProtocolBindingContext	��������ָ��
_pStatusIndication		A pointer to an NDIS_STATUS_INDICATION structure that contains the status information.
����ֵ:	��
*/
PROTOCOL_STATUS_EX	_StatusHandler;
VOID	_StatusHandler(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNDIS_STATUS_INDICATION _pStatusIndication
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;

	KdPrint( ("[_StatusHandler]\n") );

	pOpenContext = _ProtocolBindingContext;

	_ServiceIndicateStatusIrp(
		pOpenContext ,
		_pStatusIndication->StatusCode ,
		_pStatusIndication->StatusBuffer ,
		_pStatusIndication->StatusBufferSize ,
		FALSE );

	NdisAcquireSpinLock( &pOpenContext->Lock );

	do
	{
		if (pOpenContext->PowerState != NetDeviceStateD0)
		{
			//�豸�ǵ͵���״̬
			KdPrint( ("_StatusHandler: The device is in a low state!\n") );
		}

		switch (_pStatusIndication->StatusCode)
		{
			case NDIS_STATUS_RESET_START:
				ASSERT( !NDIS_TEST_FLAGS( pOpenContext->Flags ,
					NDIS_RESET_FLAGS_MASK , NDIS_RESET_IN_PROGRESS ) );

				NDIS_SET_FLAGS( pOpenContext->Flags ,
					NDIS_RESET_FLAGS_MASK , NDIS_RESET_IN_PROGRESS );

				break;

			case NDIS_STATUS_RESET_END:
				ASSERT( NDIS_TEST_FLAGS( pOpenContext->Flags ,
					NDIS_RESET_FLAGS_MASK , NDIS_RESET_IN_PROGRESS ));

				NDIS_SET_FLAGS( pOpenContext->Flags ,
					NDIS_RESET_FLAGS_MASK , NDIS_NOT_RESETTING );

				break;

			case NDIS_STATUS_MEDIA_CONNECT:

				NDIS_SET_FLAGS( pOpenContext->Flags ,
					NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_CONNECTED );

				break;

			case NDIS_STATUS_MEDIA_DISCONNECT:

				NDIS_SET_FLAGS( pOpenContext->Flags ,
					NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_DISCONNECTED );

				break;

			default:
				break;
		}
	} while (FALSE);

	NdisReleaseSpinLock( &pOpenContext->Lock );
}


VOID	_StatusComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
)
{
	return;
}


/*
�ú����ڵ���NdisReset�󱻻ص�,û�е���,����ص�
*/
VOID	_ResetComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	NDIS_STATUS	_Status
)
{
	ASSERT( FALSE );

	return;
}


/*
�ú������յ�pnp eventʱ�ص�,��������ĵ��ǵ�Դ״̬�仯���¼�
����;
ProtocolBindingContext	��������ָ��
pNetPnpEvent	PNP�¼�
����ֵ:	����״̬
*/
PROTOCOL_NET_PNP_EVENT	_PnpEventHandler;
NDIS_STATUS	_PnpEventHandler(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNET_PNP_EVENT_NOTIFICATION _pNetPnPEventNotification
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;
	NDIS_STATUS	NdisStatus;

	pOpenContext = 
		(PNDIS_OPEN_CONTEXT)_ProtocolBindingContext;

	switch (_pNetPnPEventNotification->NetPnPEvent.NetEvent)
	{
		case NetEventSetPower:

			pOpenContext->PowerState =
				*(PNET_DEVICE_POWER_STATE)_pNetPnPEventNotification->NetPnPEvent.Buffer;
			if (pOpenContext->PowerState > NetDeviceStateD0)
			{
				//
				//  The device below is transitioning to a low power state.
				//  Block any threads attempting to query the device while
				//  in this state.
				//
				KdPrint( ("_PnpEventHandler: The device is low power!\n") );

				NdisInitializeEvent( &pOpenContext->PoweredUpEvent );

				//�ȴ�δ��IRP���
				_WaitForPendingIrp( pOpenContext , FALSE );

				//�����������ն����еİ�
				_FlushReceiveQueue( pOpenContext );
			}
			else
			{
				//the device is powered up
				KdPrint( ("_PnpEventHandler: The device is powered up!\n") );

				NdisSetEvent( &pOpenContext->PoweredUpEvent );
			}

			NdisStatus = NDIS_STATUS_SUCCESS;
			break;

		case NetEventQueryPower:
			
			NdisStatus = NDIS_STATUS_SUCCESS;
			break;

		case NetEventBindsComplete:

			NdisSetEvent( &Globals.BindCompleteEvent );

			NdisStatus = NDIS_STATUS_SUCCESS;
			break;

		case NetEventQueryRemoveDevice:
		case NetEventCancelRemoveDevice:
		case NetEventReconfigure:
		case NetEventBindList:
		case NetEventPnPCapabilities:
			NdisStatus = NDIS_STATUS_SUCCESS;
			break;

		default:
			NdisStatus = NDIS_STATUS_NOT_SUPPORTED;
			break;
	}

	return	NdisStatus;
}