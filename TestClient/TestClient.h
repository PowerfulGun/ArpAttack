#pragma once



//
//	º¯ÊýÉùÃ÷
//

HANDLE	_OpenControlDevice(
	IN	PWCHAR	_pDeviceName
);

VOID	_EnumerateBindingContext(
	IN	HANDLE	_hDevice
);

