/*
	对打开上下文的一些处理
*/
#include	"ndis_shared_head.h"


/*
该函数遍历全局打开上下文链表查看给出的设备是否已经在全局链表中
参数:
pDeviceName 需要查找的设备名指针
NameLength	该设备名称长度
返回值: 找到的该设备的打开上下文指针
*/
PNDIS_OPEN_CONTEXT	_LookupOpenContext(
	IN	PWCHAR	_pDeviceName ,
	IN	ULONG	_NameLength
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext = NULL;
	PLIST_ENTRY	pListEntry;

	//对全局资源的操作要用自旋锁
	NdisAcquireSpinLock( &Globals.SpinLock );
	for (pListEntry = Globals.OpenList.Flink;
		pListEntry != &Globals.OpenList;
		pListEntry = pListEntry->Flink)
	{
		pOpenContext =
			CONTAINING_RECORD(
			pListEntry , NDIS_OPEN_CONTEXT , Link );

		//查看这个打开上下文的设备名称是否就是要查询的设备名称
		if ((pOpenContext->DeviceName.Length == _NameLength) &&
			NdisEqualMemory(
			pOpenContext->DeviceName.Buffer ,
			_pDeviceName ,
			_NameLength ))
		{
			//增加该打开上下文的引用计数
			NdisInterlockedIncrement( &pOpenContext->RefCount );
			break;
		}
		pOpenContext = NULL;
	}
	//释放自旋锁
	NdisReleaseSpinLock( &Globals.SpinLock );

	return	pOpenContext;
}


/*
该函数释放打开上下文中的资源
参数:
pOpenContext	打开上下文
返回值:无
*/
VOID	_FreeContextResoureces(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
)
{
	if (_pOpenContext->SendNetBufferListPool != NULL)
	{
		NdisFreeNetBufferListPool( _pOpenContext->SendNetBufferListPool );
		_pOpenContext->SendNetBufferListPool = NULL;
	}

	if (_pOpenContext->RecvNetBufferListPool != NULL)
	{
		NdisFreeNetBufferListPool( _pOpenContext->RecvNetBufferListPool );
		_pOpenContext->RecvNetBufferListPool = NULL;
	}

	if (_pOpenContext->RecvNetBufferPool != NULL)
	{
		NdisFreeNetBufferPool( _pOpenContext->RecvNetBufferPool );
		_pOpenContext->RecvNetBufferPool = NULL;
	}

	if (_pOpenContext->SendBufferPool != NULL)
	{
		NdisFreeNetBufferPool( _pOpenContext->SendBufferPool );
		_pOpenContext->SendBufferPool = NULL;
	}

	if (_pOpenContext->DeviceName.Buffer != NULL)
	{
		NdisFreeMemory(
			_pOpenContext->DeviceName.Buffer , 0 , 0 );
		_pOpenContext->DeviceName.Buffer = NULL;
		_pOpenContext->DeviceName.Length = 0;
		_pOpenContext->DeviceName.MaximumLength = 0;
	}

	if (_pOpenContext->DeviceDescr.Buffer != NULL)
	{
		//这个缓冲区可能在NdisQueryAdpaterInstanceName请求中用到
		NdisFreeMemory( _pOpenContext->DeviceDescr.Buffer , 0 , 0 );
		_pOpenContext->DeviceDescr.Buffer = NULL;
	}
}


/*
该函数减少打开上下文的引用计数,如果减少为0就删除这个打开上下文
参数:
pOpenContext	打开上下文指针
返回值:无
*/
VOID	_NdisDereferenceOpenContext(
	IN	PNDIS_OPEN_CONTEXT	_pOpenContext
)
{
	if (NdisInterlockedDecrement( &_pOpenContext->RefCount ) == 0)
	{
		KdPrint( ("_NdisDereferenceOpenContext:RefCount is zero!\n") );

		//free it
		NdisFreeSpinLock( &_pOpenContext->Lock );
		NdisFreeMemory( _pOpenContext , 0 , 0 );
	}
}
