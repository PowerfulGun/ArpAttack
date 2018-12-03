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

		//枚举网卡绑定信息
		_EnumerateBindingContext( hDevice );

		//指定控制设备句柄和哪个网卡设备关联
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
该函数打开驱动的控制设备并等待驱动完成对网卡的绑定
参数:
pDeviceName		控制设备的符号链接名
返回值:	控制设备的句柄
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

	//打开设备,ShareMode为0防止多个应用程序同时打开设备造成冲突
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

	//等待驱动绑定网卡
	if (!DeviceIoControl(
		hFile ,
		IOCTL_NDIS_BIND_WAIT ,
		NULL , 0 ,
		NULL , 0 ,
		&BytesReturned ,
		NULL ))
	{
		//驱动绑定网卡失败
		printf( "_OpenControlDevice.DeviceIoControl:Wait bind failed!\n" );
		CloseHandle( hFile );
		hFile = INVALID_HANDLE_VALUE;
	}

	return	hFile;
}


/*
该函数枚举驱动绑定的网卡并将信息显示出来
参数:
hDevice		驱动控制设备的句柄
返回值:	无
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

			//缓冲区重新清空
			memset( Buffer , 0 , BufferLength );
		}
		else
		{
			//没有对应的绑定信息了
			printf( "_EnumerateBindingContext: No more context!\n" );
			break;
		}
	}// end for 
}