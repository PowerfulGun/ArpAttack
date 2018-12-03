#ifndef IOCTL_H
#define	IOCTL_H



//
//  Structure to go with IOCTL_NDISPROT_QUERY_BINDING.
//  The input parameter is BindingIndex, which is the
//  index into the list of bindings active at the driver.
//  On successful completion, we get back a device name
//  and a device descriptor (friendly name).
//
typedef struct _NDIS_QUERY_BINDING
{
	ULONG            BindingIndex;        // 0-based binding number
	ULONG            DeviceNameOffset;    // from start of this struct
	ULONG            DeviceNameLength;    // in bytes
	ULONG            DeviceDescrOffset;    // from start of this struct
	ULONG            DeviceDescrLength;    // in bytes

} NDIS_QUERY_BINDING , *PNDIS_QUERY_BINDING;

#define IOCTL_NDIS_SET_DEVICE   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x200, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDIS_QUERY_OID_VALUE   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x201, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDIS_SET_OID_VALUE   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x205, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDIS_QUERY_BINDING   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x203, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDIS_BIND_WAIT   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x204, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDIS_INDICATE_STATUS   \
           CTL_CODE(FILE_DEVICE_NETWORK,0x206, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)




#endif // !IOCTL_H