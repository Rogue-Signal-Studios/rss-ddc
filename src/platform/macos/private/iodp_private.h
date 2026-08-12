#ifndef RSS_DDC_IODP_PRIVATE_H
#define RSS_DDC_IODP_PRIVATE_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdint.h>

/*
 * Reconstructed private native-DisplayPort ABI. Construction follows Create
 * ownership and each successful object must be released with CFRelease. This
 * header is internal because neither the object nor this ABI is portable.
 */
typedef CFTypeRef IODPDeviceRef;

extern IODPDeviceRef IODPDeviceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern CFTypeID IODPDeviceGetTypeID(void);
extern IOReturn IODPDeviceReadDPCD(IODPDeviceRef device, uint32_t address, void *buffer, uint32_t length);

#endif
