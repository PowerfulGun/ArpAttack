/*
	对网卡的绑定/解绑定
*/
#include	"ndis_shared_head.h"


/*
_BindAdapter函数主要工作:
1.设法防止多线程竞争
2.分配和初始化这次绑定的相关资源
3.获得网卡的一些参数
参数:
pOpenContext	打开上下文指针
pBindParameters	要绑定的适配器的参数指针
BindContext		The handle that identifies the NDIS context area for the bind operation. 
				NDIS passed this handle to the BindContext parameter of the ProtocolBindAdapterEx function.
				在NdisOpenAdapterEx函数中有使用
返回值:	NDIS_STATUS	操作状态
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
		先检查是否已经绑定了这个网卡,
		如果已经绑定的话,没必要再次绑定,直接返回即可
		_LookupOpenContext会给找到的打开上下文增加引用,
		需要减少引用
		*/
		pTmpOpenContext =
			_LookupOpenContext(
			_pBindParameters->AdapterName->Buffer ,
			_pBindParameters->AdapterName->Length );
		if (pTmpOpenContext != NULL)
		{
			KdPrint( ("_BindAdapter:The device [%ws]is already bind\n" ,
				pTmpOpenContext->DeviceName.Buffer) );
			//减少这个打开上下文的引用
			NdisInterlockedDecrement( &pTmpOpenContext->RefCount );
			NdisStatus = NDIS_STATUS_FAILURE;
			break;
		}

		//获得打开上下文的锁,为了独占设置其Flags
		NdisAcquireSpinLock( &_pOpenContext->Lock );

		//检查标记,如果标记不是空闲状态,或者是解除绑定状态
		//那么就返回失败
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

		//设置标记,表示我们已经开始绑定这个设备了
		NDIS_SET_FLAGS(
			_pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK ,
			NDIS_BIND_OPENING );
		//释放锁
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		//到这里开始绑定了,先分配名字
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
		//拷贝名称
		NdisMoveMemory(
			_pOpenContext->DeviceName.Buffer ,
			_pBindParameters->AdapterName->Buffer ,
			_pBindParameters->AdapterName->Length );
		//添加末尾\0
		*(PWCHAR)(_pOpenContext->DeviceName.Buffer + _pBindParameters->AdapterName->Length) = L'\0';
		NdisInitUnicodeString(
			&_pOpenContext->DeviceName ,
			_pOpenContext->DeviceName.Buffer );

		//填充NET_BUFFER_LIST_POOL_PARAMETERS结构体
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

		//分配发送NET_BUFFER_LIST池
		_pOpenContext->SendNetBufferListPool =
			NdisAllocateNetBufferListPool(
			Globals.NdisProtocolHandle ,
			&NetBufferListPoolParameters );
		if (_pOpenContext->SendNetBufferListPool == NULL)
		{
			KdPrint( ("_BindAdapter: Fail to allocate SendNetBufferListPool !\n") );
			break;
		}

		//分配接收NET_BUFFER_LIST池,以后接收到包的时候,就从这个包池里份分配
		_pOpenContext->RecvNetBufferListPool =
			NdisAllocateNetBufferListPool(
			Globals.NdisProtocolHandle ,
			&NetBufferListPoolParameters );
		if (_pOpenContext->RecvNetBufferListPool == NULL)
		{
			KdPrint( ("_BindAdapter: Fail to allocate RecvNetBufferListPool !\n") );
			break;
		}

		//填充NET_BUFFER_POOL_PARAMETERS结构体
		NET_BUFFER_POOL_PARAMETERS	NetBufferPoolParameters;
		NetBufferPoolParameters.Header.Type =
			NDIS_OBJECT_TYPE_DEFAULT;
		NetBufferPoolParameters.Header.Revision =
			NET_BUFFER_POOL_PARAMETERS_REVISION_1;
		NetBufferPoolParameters.Header.Size =
			NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1;
		NetBufferPoolParameters.PoolTag = POOL_TAG;
		NetBufferPoolParameters.DataSize = 0;
		//分配NET_BUFFER池,用来做接收
		_pOpenContext->RecvNetBufferPool =
			NdisAllocateNetBufferPool(
			Globals.NdisProtocolHandle ,
			&NetBufferPoolParameters );
		if (_pOpenContext->RecvNetBufferPool == 0)
		{
			KdPrint( ("_BindAdapter: Fail to allocate RecvNetBufferPool !\n") );
			break;
		}

		//电源状态是打开着的
		_pOpenContext->PowerState = NetDeviceStateD0;

		//初始化一个打开事件,打开就是绑定
		NdisInitializeEvent( &_pOpenContext->BindEvent );

		//填写OpenParameters参数
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

		//正式将本协议和目标网卡绑定
		NdisStatus = NdisOpenAdapterEx(
			Globals.NdisProtocolHandle ,
			_pOpenContext ,	//会传入到OpenComplete中做参数
			&OpenParameters ,
			_BindContext ,
			&_pOpenContext->BindingHandle );
		//等待请求完成
		if (NdisStatus == NDIS_STATUS_PENDING)
		{
			NdisWaitEvent( &_pOpenContext->BindEvent , 0 );
			NdisStatus = _pOpenContext->BindStatus;
		}

		//如果不成功
		if (NdisStatus != NDIS_STATUS_SUCCESS)
		{
			KdPrint( ("_BindAdapter.NdisOpenAdapter failed,status=%x\n" ,
				NdisStatus) );
			break;
		}

		//此时我们已经成功绑定了,但是还没有更新flags里状态,
		//这是为了避免别的线程开始关闭这个绑定
		bOpenComplete = TRUE;

		/*
		//发送请求,获得一个可阅读的名字,并非一定要成功,所以不检查返回值
		NdisQueryAdapterInstanceName(
			&_pOpenContext->DeviceDescr ,
			_pOpenContext->BindingHandle );
		*/

		//获得网卡mac地址
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

		//获得网卡选项
		_pOpenContext->MacOptions = _pBindParameters->MacOptions;

		//获得最大帧长
		_pOpenContext->MaxFrameSize = _pBindParameters->MtuSize;

		//获得下层链接状态
		NDIS_MEDIA_CONNECT_STATE	MediaConnectStatus =
			_pBindParameters->MediaConnectState;
		//将连接状态保存在Flags中
		if (MediaConnectStatus == NdisMediaStateConnected)
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_CONNECTED );
		else
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_MEDIA_FLAGS_MASK , NDIS_MEDIA_DISCONNECTED );
		
		//设置标记
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		NDIS_SET_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE );
		//检测这时候是否出现了一个解除绑定的flags
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_UNBIND_FLAGS_MASK , NDIS_UNBIND_RECEIVED ))
		{
			//出现了则这次绑定失败
			NdisStatus = NDIS_STATUS_FAILURE;
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

	} while (FALSE);

	//如果没有成功,并且bDoNotDisturb为FLASE
	if ((NdisStatus != NDIS_STATUS_SUCCESS) && !bDoNotDisturb)
	{
		NdisAcquireSpinLock( &_pOpenContext->Lock );
		//如果已经成功的绑定了
		if (bOpenComplete)
		{
			//如果已经绑定结束了,则设置已经绑定标记
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE );
		}
		else if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_OPENING ))
		{
			//如果是正在绑定过程中,则设置绑定失败了
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_FAILED );
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		//调用解除绑定函数,里面会释放所有资源
		_UnbindAdapter( _pOpenContext );
	}

	return	NdisStatus;
}


/*
该函数主要工作是:
1.打开上下文的分配和初始化
2.将这个打开上下文保存到全局链表,并调用BindAdapter正式完成绑定
参数:
ProtocolDriverContext	驱动在注册ndis协议时传的自定义上下文参数,本驱动没有使用
BindContext				The handle that identifies the NDIS context area for this bind operation.
pBindParameters			指向需要绑定的适配器的参数的指针
返回值:	操作状态
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
		//分配打开上下文,打开上下文用来保存绑定的信息
		pOpenContext =
			NdisAllocateMemoryWithTagPriority(	//该函数分配的是non-page pool
			Globals.NdisProtocolHandle ,
			sizeof( NDIS_OPEN_CONTEXT ) ,
			POOL_TAG ,
			NormalPoolPriority );
		if (pOpenContext == NULL)
		{
			NdisStatus = NDIS_STATUS_RESOURCES;
			break;
		}

		//内存清0
		NdisZeroMemory( pOpenContext , sizeof( NDIS_OPEN_CONTEXT ) );

		//给这个空间写一个特征数据,便于识别判错
		pOpenContext->oc_sig = 'hq';

		//初始化上下文中的几个成员:
		//锁 读队列 写队列 包队列 电源打开事件
		NdisAllocateSpinLock( &pOpenContext->Lock );
		InitializeListHead( &pOpenContext->PendedReads );
		InitializeListHead( &pOpenContext->PendedWrites );
		InitializeListHead( &pOpenContext->RecvPktQueue );
		NdisInitializeEvent( &pOpenContext->PoweredUpEvent );

		//认为开始电源是打开的
		NdisSetEvent( &pOpenContext->PoweredUpEvent );

		//增加打开上下文的引用计数
		NdisInterlockedIncrement( &pOpenContext->RefCount );

		//将打开上下文保存到全局链表里以便之后检索
		//这个操需要加锁
		NdisAcquireSpinLock( &Globals.SpinLock );
		InsertTailList( &Globals.OpenList ,
			&pOpenContext->Link );
		NdisReleaseSpinLock( &Globals.SpinLock );

		//正式绑定过程
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
该函数做的事情:
1.处理多线程竞争问题
2.停止接发数据包
3.处理掉所有未完成请求(或者等待完成,或者取消掉)
4.调用NdisCloseAdapter
5.清理掉所有打开上下文中分配的资源
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
		//检查标记,如果正在打开,则立刻退出,放弃解除绑定操作
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_OPENING ))
		{
			NdisReleaseSpinLock( &_pOpenContext->Lock );
			break;
		}

		//如果绑定已经完成,则设置为开始解除绑定
		//其他的情况不用做下面的操作,因为如果绑定未完成,就不用解除绑定
		if (NDIS_TEST_FLAGS( _pOpenContext->Flags ,
			NDIS_BIND_FLAGS_MASK , NDIS_BIND_ACTIVE ))
		{
			NDIS_SET_FLAGS( _pOpenContext->Flags ,
				NDIS_BIND_FLAGS_MASK , NDIS_BIND_CLOSING );
			bDoCloseBinding = TRUE;
		}
		NdisReleaseSpinLock( &_pOpenContext->Lock );

		/*
		下面要进行的操作:
		1.把这个协议驱动的包过滤标记设置为全空,使之不再接收到包
		2.把这个协议的接收广播包列表设置为空,使这个协议不再接收任何数据包
		*/
		if (bDoCloseBinding)
		{
			ULONG	PacketFilter = 0;
			ULONG	BytesRead = 0;

			//发送请求设置网卡的包过滤器为0,停止收包
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

			//设置这个网卡的广播列表为NULL
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

			//取消所有的提交状态IRP
			_ServiceIndicateStatusIrp(
				_pOpenContext ,
				0 ,
				NULL ,
				0 ,
				TRUE );

			// 等待所有未决IRP完成
			_WaitForPendingIrp( _pOpenContext , TRUE );

			//清理掉接收队列中所有的包
			_FlushReceiveQueue( _pOpenContext );

			//正式调用解除绑定函数,等待事件和状态会在完成函数中设置
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
				//设置已经解除绑定的标记
				NdisAcquireSpinLock( &_pOpenContext->Lock );
				NDIS_SET_FLAGS( _pOpenContext->Flags ,
					NDIS_BIND_FLAGS_MASK , NDIS_BIND_IDLE );
				NDIS_SET_FLAGS( _pOpenContext->Flags ,
					NDIS_UNBIND_FLAGS_MASK , 0 );
				NdisReleaseSpinLock( &_pOpenContext->Lock );
			}

			//释放资源
			NdisAcquireSpinLock( &Globals.SpinLock );
			RemoveEntryList( &_pOpenContext->Link );
			NdisReleaseSpinLock( &Globals.SpinLock );
			_FreeContextResoureces( _pOpenContext );
			//减少一次打开上下文的引用,如果为0就彻底删除
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
该函数工作:
1.设置解除绑定NDIS_UNBIND_RECEIVED标记
让可能存在的某个尝试绑定的操作知道已经开始解除绑定了
2.设置电源开启事件,以便一些未决请求可以完成
3.调用_UnbindAdapter函数完成剩余工作
参数:
pStatus		用来返回操作状态
ProtocolBindingContext	打开上下文
UnbindContext	用不到
返回值:无
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

	//加锁,准备设置标记
	NdisAcquireSpinLock( &pOpenContext->Lock );
	NDIS_SET_FLAGS( pOpenContext->Flags ,
		NDIS_UNBIND_FLAGS_MASK , NDIS_UNBIND_RECEIVED );
	//设置事件,通知所有在等待的电源启动的线程,避免一些请求无法完成
	NdisSetEvent( &pOpenContext->PoweredUpEvent );
	NdisReleaseSpinLock( &pOpenContext->Lock );

	//剩余操作
	_UnbindAdapter( pOpenContext );

	return NDIS_STATUS_SUCCESS;
}


/*
该函数用来向网卡发送OID请求获取信息
参数:
pOpenContext	打开上下文
RequestType		请求类型
Oid				请求类型号 Object Identifiers
InformationBuffer 信息存放的缓冲区
InformationBufferLength 缓冲区的大小
pBytesProcessed	缓冲区中有效结果的长度
返回值:操作状态
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

	//初始化请求事件,这个事件会在请求的完成函数中被设置
	//用来通知请求完成了
	NdisInitializeEvent( &RequestContext.ReqEvent );

	//初始化NDIS_OID_REQUEST结构
	pNdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
	pNdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
	pNdisRequest->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
	//获得请求的类型,如果只是查询信息,只要用
	//ndisrequestqueryInformation就行了
	pNdisRequest->RequestType = _RequestType;
	pNdisRequest->PortNumber = 0;

	//根据不同的请求类型,填写OID和输入/输出缓冲区
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
		Ndis 5x的调用方式
	//发送请求
	NdisRequest(
		&NdisStatus ,
		_pOpenContext->BindingHandle ,
		pNdisRequest );
	*/

	//Ndis 6.0的调用方式
	NdisStatus = NdisOidRequest(
		_pOpenContext->BindingHandle , pNdisRequest );
	//如果是未决状态,则等待事件,事件会在完成函数中设置
	if (NdisStatus == NDIS_STATUS_PENDING)
	{
		NdisWaitEvent( &RequestContext.ReqEvent , 0 );
		NdisStatus = RequestContext.Status;
	}
	//如果成功了
	if (NdisStatus == NDIS_STATUS_SUCCESS)
	{
		//获得结果长度,这个结果长度是实际需要的长度
		//可能比我们实际提供的长度要长
		*_pBytesProcessed =
			(_RequestType == NdisRequestQueryInformation) ?
			pNdisRequest->
			DATA.QUERY_INFORMATION.BytesWritten :
			pNdisRequest->
			DATA.SET_INFORMATION.BytesRead;

		//如果结果长度比提供的BufferLength要长,就简单地重新设置为最大长度
		if (*_pBytesProcessed > _InformationBufferLength)
			*_pBytesProcessed = _InformationBufferLength;
	}

	return	NdisStatus;
}


/*
该函数完成状态IRP请求,如果IRP正在取消
就让该IRP的取消例程完成这个IRP
参数:
pOpenContext	打开上下文
GeneralStatus	状态
StatusBuffer	额外的状态信息
StatusBufferSize额外的状态信息的大小
Cancel			IRP是否能立即取消
返回值:	无
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

			//查看是否有取消例程
			if (IoSetCancelRoutine( pIrp , NULL ))
			{
				//没有取消例程
				status = STATUS_CANCELLED;

				if (!_bCancel)
				{
					//参数里明确不允许取消

					//检查缓冲区是否足够大
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
						//缓冲区太小
						status = STATUS_BUFFER_OVERFLOW;
					}
				}// if (!_bCancel)

				//clear field
				_pOpenContext->StatusIndicationIrp = NULL;
				//拷贝的字节数量或者需要的字节数量
				Bytes =
					sizeof( NDIS_INDICATE_STATUS ) + _StatusBufferSize;
				break;
			}// if (IoSetCancelRoutine( pIrp , NULL ))
			else
			{
				//这个Irp有取消例程,不管
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
该函数等待打开上下文里所有IRP请求完成
参数:
pOpenContext	打开上下文
bDoCancelReads	是否可以取消这些未决的读IRP
返回值:	无
*/
VOID	_WaitForPendingIrp(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext ,
	IN	BOOLEAN	_bDoCancelReads
)
{
	NDIS_STATUS	NdisStatus;
	ULONG	LoopCount;
	ULONG	PendingCount;

	//确保队列里没有未发送的包,如果有就等一会
	for (LoopCount = 0; LoopCount < 60; LoopCount++)
	{
		if (_pOpenContext->PendedSendCount == 0)
			break;

		KdPrint( ("_WaitForPendingIrp: Wait for pended send count=%d\n" ,
			_pOpenContext->PendedSendCount) );

		//睡1秒
		NdisMSleep( 1000000 );
	}
	ASSERT( LoopCount < 60 );

	if (_bDoCancelReads)
	{
		//等待读IRP完成或者取消
		while (_pOpenContext->PendedReadCount != 0)
		{
			KdPrint( ("_WaitForPendingIrp: Wait for pended read count=%d\n" ,
				_pOpenContext->PendedReadCount) );

			//取消读请求
			_CancelPendingReads( _pOpenContext );

			//睡一秒
			NdisMSleep( 1000000 );
		}
	}
}


/*
该函数取消队列里的读请求
参数:
pOpenContext	打开上下文
返回值:	无
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

		//检查是否正在被取消
		if (IoSetCancelRoutine( pIrp , NULL ))
		{
			//没有被取消
			RemoveEntryList( pIrpEntry );
			NdisReleaseSpinLock( &_pOpenContext->Lock );

			//完成这个Irp
			pIrp->IoStatus.Status = STATUS_CANCELLED;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest( pIrp , IO_NO_INCREMENT );

			//减少因为未决IRP增加的打开上下文的引用
			_NdisDereferenceOpenContext( _pOpenContext );
			NdisAcquireSpinLock( &_pOpenContext->Lock );
			_pOpenContext->PendedReadCount--;

		}// if(IoSetCancelRoutine(pIrp,NULL))
		else
		{
			//正在被取消,让它的取消例程处理这个IRP
			NdisReleaseSpinLock( &_pOpenContext->Lock );

			NdisMSleep( 1000000 );

			NdisAcquireSpinLock( &_pOpenContext->Lock );
		}
	}

	NdisReleaseSpinLock( &_pOpenContext->Lock );
	_NdisDereferenceOpenContext( _pOpenContext );
}


/*
该函数清理掉打开上下文中的接收队列的包
参数:
pOpenContext	打开上下文
返回值:	无
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
该函数在解除绑定函数NdisCloseAdapter调用后回调
主要设置等待事件和保存操作状态
参数:
ProtocolBindingContext	打开上下文指针
NdisStatus		操作状态
返回值:	无
*/
PROTOCOL_CLOSE_ADAPTER_COMPLETE_EX	_CloseAdapterComplete;
VOID	_CloseAdapterComplete(
	IN	NDIS_HANDLE	_ProtocolBindingContext 
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;

	pOpenContext = _ProtocolBindingContext;

	//保存状态
	pOpenContext->BindStatus = NDIS_STATUS_SUCCESS;

	//设置事件有信号
	NdisSetEvent( &pOpenContext->BindEvent );
}


/*
该函数会在NdisOpenAdapter完成之后回调,主要设置事件让等待的线程继续执行
参数:
pProtocolBindingContext 调用OpenAdaterEx时传入的OpenContext
NdisStatus 绑定完成的状态
返回值:无
*/
PROTOCOL_OPEN_ADAPTER_COMPLETE_EX	_OpenAdapterComplete;
VOID	_OpenAdapterComplete(
	IN	NDIS_HANDLE	_pProtocolBindingContext ,	//调用OpenAdaterEx时传入的OpenContext
	IN	NDIS_STATUS	_NdisStatus
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext;

	pOpenContext =
		(PNDIS_OPEN_CONTEXT)_pProtocolBindingContext;

	pOpenContext->BindStatus = _NdisStatus;

	//设置等待事件
	NdisSetEvent( &pOpenContext->BindEvent );
}
