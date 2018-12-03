/*
	协议驱动特有的处理过程
*/
#include	<ndis_shared_head.h>


/*
该函数在OID请求完成后回调,主要设置事件,并保存请求的status
参数:
ProtocolBindingContext	设备的打开上下文
pNdisRequest	请求
status	请求完成状态
返回值:无
*/
PROTOCOL_OID_REQUEST_COMPLETE	_RequestComplete;
VOID	_RequestComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext ,
	IN	PNDIS_OID_REQUEST	_pNdisRequest ,
	IN	NDIS_STATUS	_NdisStatus
)
{
	PNDIS_REQUEST_CONTEXT	pReqContext;

	//从pNDISRequest中得到请求上下文
	pReqContext = CONTAINING_RECORD(
		_pNdisRequest , NDIS_REQUEST_CONTEXT , NdisRequest );

	//保存结果状态
	pReqContext->Status = _NdisStatus;

	//设置事件
	NdisSetEvent( &pReqContext->ReqEvent );
}


/*
该函数在包发送完之后被回调
功能:	减少各个引用计数,结束IRP请求
参数:
ProtocolBindingContext	和绑定有关的打开上下文指针
pNetBufferList			已发送的NetBufferList
SendCompleteFlags		发送时指定的SendFlags
返回值:无
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

	//获得打开上下文
	pOpenContext = _ProtocolBindingContext;

	//从包描述符中获得IRp指针
	pIrp = ((PNDIS_SEND_PACKET_RSVD)
		_pNetBufferList->Context->ContextData)->pIrp;
	//数据包解引用
	if (
		NdisInterlockedDecrement(
		&((PNDIS_SEND_PACKET_RSVD)
		_pNetBufferList->Context->ContextData)->RefCount ) == 0
		)
	{
		NdisFreeNetBufferList( _pNetBufferList );
	}

	//未决发送包数量减1
	NdisInterlockedDecrement( &pOpenContext->PendedSendCount );
	//打开上下文引用减1
	_NdisDereferenceOpenContext( pOpenContext );

	//完成Irp请求
	pIrpStack = IoGetCurrentIrpStackLocation( pIrp );
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information =
		pIrpStack->Parameters.Write.Length;
	IoCompleteRequest( pIrp , IO_NO_INCREMENT );

	KdPrint( ("_SendComplete: Packet completed success!\n") );
}


/*
该函数将一个收到的包插入读请求队列,
如果队列满了,就从队列头删除一个包
最后运行处理读请求队列的函数
参数:
pOpenContext	打开上下文
_pNetBufferList		收到的包
返回值:无
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
		//获得接收队列
		pEntry =
			&((PNDIS_RECV_PACKET_RSVD)
			_pNetBufferList->Context->ContextData)->Link;

		//增加对打开上下文的引用
		NdisInterlockedIncrement( &_pOpenContext->RefCount );

		//如果处于活动状态,并且电源状态正确,就把这个包插入链表
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
			// 状态不对就释放这个包;
			_FreeReceiveNetBufferList( _pOpenContext , _pNetBufferList );
			//解引用
			_NdisDereferenceOpenContext( _pOpenContext );
			break;
		}

		//如果输入缓冲区里包太多了,就删一个
		if (_pOpenContext->RecvPktCount > MAX_RECV_QUEUE_SIZE)
		{
			//要删除的节点
			pDiscardEntry = _pOpenContext->RecvPktQueue.Flink;
			RemoveEntryList( pDiscardEntry );

			//减少刚刚增加的数量
			_pOpenContext->RecvPktCount--;

			//获得该节点里的包描述符指针
			pDiscardNetBufferList =
				CONTAINING_RECORD( pDiscardEntry , NET_BUFFER_LIST , Context->ContextData );
			//把包释放掉
			_FreeReceiveNetBufferList( _pOpenContext , pDiscardNetBufferList );
			//打开上下文解引用
			_NdisDereferenceOpenContext( _pOpenContext );
			KdPrint( ("_QueueReceivePacket: Queue too long,discard one !\n") );
		}

		//调用函数查看是否有未决的读请求,有就完成请求
		//_NdisServiceReads( _pOpenContext );

	} while (FALSE);
}


/*
该函数释放一个NetBufferList,释放包里面的所有资源
如果这是一个自己拷贝的包,就释放回打开上下文的接收包池,不是就返回给ndis
参数:
pOpenContext		打开上下文
pNetBufferList		要释放的NetBufferList
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
//该函数将受到的包里的数据拷贝到用户缓冲区并完成读请求
//参数	
//pOpenContext	打开上下文
//返回值:	无
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
//	NdisInterlockedIncrement( &_pOpenContext->RefCount );	//防止绑定被解除
//
//	NdisAcquireSpinLock( &_pOpenContext->Lock );	//加锁为了独占使用
//
//	//只要读请求队列和接收包队列不同时为空,就可以做
//	while (!IsListEmpty( &_pOpenContext->PendedReads )
//		&& !IsListEmpty( &_pOpenContext->RecvPktQueue ))
//	{
//		bFoundPendingIrp = FALSE;
//
//		//获得第一个未决读请求
//		pIrpEntry = _pOpenContext->PendedReads.Flink;
//		while (pIrpEntry != &_pOpenContext->PendedReads)
//		{
//			//从链表节点的到Irp指针
//			pIrp =
//				CONTAINING_RECORD( pIrpEntry ,
//				IRP , Tail.Overlay.ListEntry );
//
//			//检查这个请求是否正在被取消
//			if (IoSetCancelRoutine( pIrp , NULL ))
//			{
//				//没有取消,将这iRp出列
//				RemoveEntryList( pIrpEntry );
//				bFoundPendingIrp = TRUE;
//				break;
//			}
//			else
//			{
//				//如果正在取消,则跳过这个IRp
//				KdPrint( ("_NdisServiceReads: Skip cancelled Irp\n") );
//
//				pIrpEntry = pIrpEntry->Flink;
//			}
//
//		} // end while (pIrpEntry != &_pOpenContext->PendedReads)
//
//		//如果没有Irp,直接跳出结束
//		if (bFoundPendingIrp == FALSE)
//			break;
//
//		//得到第一个包节点(最旧的),出队列
//		pRcvNetBufferListEntry =
//			_pOpenContext->RecvPktQueue.Flink;
//		RemoveEntryList( pRcvNetBufferListEntry );
//		//未处理包减1
//		_pOpenContext->RecvPktCount--;
//		_NdisDereferenceOpenContext( _pOpenContext );
//
//		//从节点获得包描述符
//		pRcvPacket =
//			CONTAINING_RECORD( pRcvNetBufferListEntry ,
//			NET_BUFFER_LIST , Context->ContextData );
//
//		//得到IRP的输出地址,然后尽量拷贝更多的数据到用户缓冲区
//		pDst =
//			MmGetSystemAddressForMdlSafe(
//			pIrp->MdlAddress , NormalPagePriority );
//		ASSERT( pDst != NULL );
//		BytesRemainingInUserBuffer
//			= MmGetMdlByteCount( pIrp->MdlAddress );
//		//获得第一个包缓冲区描述符
//		pNdisBuffer = pRcvPacket->Private.Head;
//
//		// 请注意，每个PNDIS_BUFFER都是一个PMDL，同时PNDIS_BUFFER
//		// 本身都是链表。用NdisGetNextBuffer可以从一个得到它的下面一个。
//		// 包的数据实际上是保存在一个缓冲描述符链表里的。
//		//下面循环的终止条件就是要么用户缓冲已满,要么包里已没数据可拷贝
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
//			//如果还可以继续拷贝,就继续拷贝
//			if (BytesInNdisBuffer)
//			{
//				ULONG	BytesToCopy =
//					min( BytesRemainingInUserBuffer , BytesInNdisBuffer );
//				NdisMoveMemory( pDst , pSrc , BytesToCopy );
//				BytesRemainingInUserBuffer -= BytesToCopy;
//				pDst += BytesToCopy;	//用户缓冲区指针增加偏移
//			}
//
//			//得到该包的下一个缓冲区描述符
//			NdisGetNextBuffer( pNdisBuffer , &pNdisBuffer );
//
//		} //end while (BytesRemainingInUserBuffer && (pNdisBuffer != NULL))
//
//		//拷贝好之后结束该IRp即可
//		pIrp->IoStatus.Status = STATUS_SUCCESS;
//
//		//information里存放的是真正往用户缓冲区里写的字节数
//		pIrp->IoStatus.Information =
//			MmGetMdlByteCount( pIrp->MdlAddress ) - BytesRemainingInUserBuffer;
//
//		KdPrint( ("_NdisServiceReads: Complete IRP and write %d bytes\n" ,
//			pIrp->IoStatus.Information) );
//		IoCompleteRequest( pIrp , IO_NO_INCREMENT );
//
//		//释放包
//		// 如果这个包描述符不是从接收包池里分配的，那么就是从
//		// 网卡驱动里重用的。如果是重用的，调用NdisReturnPackets
//		// 归还给网卡驱动，让它释放。
//		if (NdisGetPoolFromPacket( pRcvPacket ) != _pOpenContext->RecvNetBufferListPool)
//		{
//			NdisReturnPackets( &pRcvPacket , 1 );
//		}
//		else
//		{
//			//是自己分配的包,自己释放
//			_FreeReceiveNetBufferList( _pOpenContext , pRcvPacket );
//		}
//
//		//解引用
//		_NdisDereferenceOpenContext( _pOpenContext );
//		//读请求减1
//		_pOpenContext->PendedReadCount--;
//
//	} // end while (!IsListEmpty( &_pOpenContext->PendedReads )
//	//	&& !IsListEmpty( &_pOpenContext->RecvPktQueue ))
//
//	//解引用解锁
//	NdisReleaseSpinLock( &_pOpenContext->Lock );
//	_NdisDereferenceOpenContext( _pOpenContext );
//}
//

/*
该函数接收网卡上的包,但接收的是已经组织好的NetBufferList
我们可以直接使用这个NetBufferList,但如果下层资源紧张时就需要自己
生成一个NetBufferList并且拷贝
参数:
ProtocolBindingContext	打开上下文
pNetBufferList			接收到的NetBufferList的指针
PortNumber				标识小端口驱动的端口号,用不到
NumberOfNetBufferList	NetBufferList的数量
_ReceiveFlags			接收标识
返回值:	无
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
		//如果这个包的长度比以太网包头要小则丢弃
		if (BufferLength < sizeof( NDIS_ETH_HEADER ))
		{
			NdisStatus = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}

		//如果这个包有NDIS_RECEIVE_FLAGS_RESOURCES状态,则必须拷贝
		//不能重用
		if (_ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES)
		{
			//暂时不分配新的NetBufferList,而是直接将这个
			//NetBufferList返回给ndis
			NdisReturnNetBufferLists(
				pOpenContext->BindingHandle ,
				_pNetBufferList , 0 );
		} // if NDIS_RECEIVE_FLAGS_RESOURCES
		else
		{
			//将包放入队列
			_QueueNetBufferList( pOpenContext , _pNetBufferList );
		}

	} while (FALSE);

	//失败要释放资源
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
该函数在小端口驱动完成表明收到一捆数据包时调用
不执行什么操作
参数:
ProtocolBindingContext	打卡上下文指针
返回值:	无
*/
VOID	_ReceiveComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext
)
{
	return;
}


/*
该函数在小端口驱动表明状态发生变化时调用,
我们留意reset和media connect 的状态
参数:
ProtocolBindingContext	打开上下文指针
_pStatusIndication		A pointer to an NDIS_STATUS_INDICATION structure that contains the status information.
返回值:	无
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
			//设备是低电量状态
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
该函数在调用NdisReset后被回调,没有调用,不会回调
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
该函数在收到pnp event时回调,我们最关心的是电源状态变化的事件
参数;
ProtocolBindingContext	打开上下文指针
pNetPnpEvent	PNP事件
返回值:	操作状态
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

				//等待未决IRP完成
				_WaitForPendingIrp( pOpenContext , FALSE );

				//清理驱动接收队列中的包
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