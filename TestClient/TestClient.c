#include	<Windows.h>
#include	<stdio.h>
#include	"../ndis/ioctl.h"
#include	"TestClient.h"

int		main()
{
	HANDLE	hDevice = INVALID_HANDLE_VALUE;
	DWORD	BytesReturned;

	do
	{
		hDevice = _OpenControlDevice(
			L"\\\\.\\PowerfulGun_Ndis" );
		if (hDevice == INVALID_HANDLE_VALUE)
		{
			printf( "Failed to open control device!\n" );
			break;
		}

		//ö����������Ϣ
		_EnumerateBindingContext( hDevice );

		//ָ�������豸������ĸ������豸����
		WCHAR	pSetDeviceName[128] = { 0 };
		printf( "Set Device:\n" );
		wscanf( L"%ws\n" , pSetDeviceName );

		if (DeviceIoControl(
			hDevice ,
			IOCTL_NDIS_SET_DEVICE ,
			pSetDeviceName ,
			wcslen( pSetDeviceName ) * sizeof( WCHAR ) ,
			NULL , 0 ,
			&BytesReturned ,
			NULL ))
		{
			printf( "Set Device Success!\n" );
		}
		else
		{
			printf( "Set Device Fail!\n" );
			break;
		}


	} while (FALSE);
}


/*
�ú����������Ŀ����豸���ȴ�������ɶ������İ�
����:
pDeviceName		�����豸�ķ���������
����ֵ:	�����豸�ľ��
*/
HANDLE	_OpenControlDevice(
	IN	PWCHAR	_pDeviceName
)
{
	DWORD	DesiredAccess;
	DWORD	SharedMode;
	LPSECURITY_ATTRIBUTES	lpSecurityAttr = NULL;
	DWORD	CreationDistribution;
	DWORD	FlagsAndAttr;
	HANDLE	TemplateFile;
	HANDLE	hFile;
	DWORD	BytesReturned;

	DesiredAccess = GENERIC_READ | GENERIC_WRITE;
	SharedMode = 0;
	CreationDistribution = OPEN_EXISTING;
	FlagsAndAttr = FILE_ATTRIBUTE_NORMAL;
	TemplateFile = (HANDLE)INVALID_HANDLE_VALUE;

	//���豸,ShareModeΪ0��ֹ���Ӧ�ó���ͬʱ���豸��ɳ�ͻ
	hFile = CreateFile(
		_pDeviceName ,
		DesiredAccess ,
		SharedMode ,
		lpSecurityAttr ,
		CreationDistribution ,
		FlagsAndAttr ,
		TemplateFile );
	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf( "Open device fail\n" );
		return hFile;
	}

	//�ȴ�����������
	if (!DeviceIoControl(
		hFile ,
		IOCTL_NDIS_BIND_WAIT ,
		NULL , 0 ,
		NULL , 0 ,
		&BytesReturned ,
		NULL ))
	{
		//����������ʧ��
		printf( "_OpenControlDevice.DeviceIoControl:Wait bind failed!\n" );
		CloseHandle( hFile );
		hFile = INVALID_HANDLE_VALUE;
	}

	return	hFile;
}


/*
�ú���ö�������󶨵�����������Ϣ��ʾ����
����:
hDevice		���������豸�ľ��
����ֵ:	��
*/
VOID	_EnumerateBindingContext(
	IN	HANDLE	_hDevice
)
{
	PUCHAR	Buffer[1024];
	DWORD	BufferLength = sizeof( Buffer );
	DWORD	BytesWritten;
	PNDIS_QUERY_BINDING	pQueryBinding = 
		(PNDIS_QUERY_BINDING)Buffer;

	for (pQueryBinding->BindingIndex = 0;
		;
		pQueryBinding->BindingIndex++)
	{
		if (DeviceIoControl(
			_hDevice ,
			IOCTL_NDIS_QUERY_BINDING ,
			pQueryBinding ,
			sizeof( NDIS_QUERY_BINDING ) ,
			Buffer ,
			BufferLength ,
			&BytesWritten ,
			NULL ))
		{
			printf( "_EnumerateBindingContext:\n" );
			printf( "Index=%d\nDeviceName=%ws\nDeviceDescr=%ws\n" ,
				pQueryBinding->BindingIndex ,
				(PWCHAR)(Buffer + pQueryBinding->DeviceNameOffset) ,
				(PWCHAR)(Buffer + pQueryBinding->DeviceDescrOffset) );

			//�������������
			memset( Buffer , 0 , BufferLength );
		}
		else
		{
			//û�ж�Ӧ�İ���Ϣ��
			printf( "_EnumerateBindingContext: No more context!\n" );
			break;
		}
	}// end for 
}