/*
	�Դ������ĵ�һЩ����
*/
#include	"ndis_shared_head.h"


/*
�ú�������ȫ�ִ�����������鿴�������豸�Ƿ��Ѿ���ȫ��������
����:
pDeviceName ��Ҫ���ҵ��豸��ָ��
NameLength	���豸���Ƴ���
����ֵ: �ҵ��ĸ��豸�Ĵ�������ָ��
*/
PNDIS_OPEN_CONTEXT	_LookupOpenContext(
	IN	PWCHAR	_pDeviceName ,
	IN	ULONG	_NameLength
)
{
	PNDIS_OPEN_CONTEXT	pOpenContext = NULL;
	PLIST_ENTRY	pListEntry;

	//��ȫ����Դ�Ĳ���Ҫ��������
	NdisAcquireSpinLock( &Globals.SpinLock );
	for (pListEntry = Globals.OpenList.Flink;
		pListEntry != &Globals.OpenList;
		pListEntry = pListEntry->Flink)
	{
		pOpenContext =
			CONTAINING_RECORD(
			pListEntry , NDIS_OPEN_CONTEXT , Link );

		//�鿴����������ĵ��豸�����Ƿ����Ҫ��ѯ���豸����
		if ((pOpenContext->DeviceName.Length == _NameLength) &&
			NdisEqualMemory(
			pOpenContext->DeviceName.Buffer ,
			_pDeviceName ,
			_NameLength ))
		{
			//���Ӹô������ĵ����ü���
			NdisInterlockedIncrement( &pOpenContext->RefCount );
			break;
		}
		pOpenContext = NULL;
	}
	//�ͷ�������
	NdisReleaseSpinLock( &Globals.SpinLock );

	return	pOpenContext;
}


/*
�ú����ͷŴ��������е���Դ
����:
pOpenContext	��������
����ֵ:��
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
		//���������������NdisQueryAdpaterInstanceName�������õ�
		NdisFreeMemory( _pOpenContext->DeviceDescr.Buffer , 0 , 0 );
		_pOpenContext->DeviceDescr.Buffer = NULL;
	}
}


/*
�ú������ٴ������ĵ����ü���,�������Ϊ0��ɾ�������������
����:
pOpenContext	��������ָ��
����ֵ:��
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
