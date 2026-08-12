#ifndef RSS_DDC_IOAV_PRIVATE_H
#define RSS_DDC_IOAV_PRIVATE_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdint.h>

/*
 * Reconstructed Apple-private IOAV declarations. These signatures and
 * CoreFoundation ownership assumptions are confined to the macOS backend:
 * CreateWithService returns a retained CF object; callers validate its type
 * and balance it with CFRelease. The public API never exposes these types.
 */
typedef CFTypeRef IOAVServiceRef;
typedef CFTypeRef IOAVDeviceRef;

extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef service, uint32_t chip, uint32_t data,
                                   void *buffer, uint32_t length);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef service, uint32_t chip, uint32_t data,
                                    void *buffer, uint32_t length);

extern IOAVDeviceRef IOAVDeviceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern CFTypeID IOAVDeviceGetTypeID(void);
extern IOReturn IOAVDeviceReadI2C(IOAVDeviceRef device, uint32_t chip, uint32_t data,
                                  void *buffer, uint32_t length);

#endif
