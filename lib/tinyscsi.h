/*
 * The littlest and dumbest generic userspace SCSI interface.
 * This implementation is for Mac OS only.
 *
 * Copyright (c) 2014 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/scsi/SCSITask.h>
#elif defined(WIN32) || defined(__WIN32) || defined(__WIN32__) || defined(__NT__)
#error No support for Windows just yet.
#elif __linux__
// TODO: Linux
#warning No support for Linux just yet.


#elif __unix__
// TODO: BSD
#warning No support for Unix just yet.
#else
#error Define the right variable for your library.
#endif

#include <stdio.h>


/*
 * Everything is synchronous.
 * Everything returns true on success, false on failure.
 */

class TinySCSI {
public:
    enum DataDirection {
        NONE = 0,
        IN,
        OUT
    };

    bool open(uint16_t idVendor, uint16_t idProduct);
    bool command(uint8_t* cdb, unsigned cdbLen, DataDirection dir = NONE, uint8_t* data = 0, unsigned dataLen = 0);
    bool reEnumerate();

    bool in(uint8_t* cdb, unsigned cdbLen, uint8_t* data, unsigned dataLen) {
        return command(cdb, cdbLen, IN, data, dataLen);
    }

    bool out(uint8_t* cdb, unsigned cdbLen, uint8_t* data, unsigned dataLen) {
        return command(cdb, cdbLen, OUT, data, dataLen);
    }

#ifdef __APPLE__
    struct {
        uint64_t transferCount;
        SCSITaskStatus taskStatus;
        SCSI_Sense_Data senseData;
    } result;

private:
    SCSITaskDeviceInterface** mInterface;
    SCSITaskInterface** mTask;
    IOUSBDeviceInterface187** mUSB;
#endif

};


/************************************************************************************/



inline bool TinySCSI::open(uint16_t idVendor, uint16_t idProduct)
{
#ifdef __APPLE__
    CFMutableDictionaryRef matching = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // Exact vendor ID match

    CFNumberRef numVendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &idVendor);
    CFDictionarySetValue(matching, CFSTR( kUSBVendorID ), numVendor);
    CFRelease(numVendor);

    // Exact product ID match

    CFNumberRef numProduct = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &idProduct);
    CFDictionarySetValue(matching, CFSTR(kUSBProductID), numProduct);
    CFRelease(numProduct);

    // This tactic of looking for an "authoring device" seems to be the mojo we need in order
    // to get the io_service_t that has an MMCDeviceInterface available.

    CFMutableDictionaryRef property = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(property,
        CFSTR(kIOPropertySCSITaskDeviceCategory), CFSTR(kIOPropertySCSITaskAuthoringDevice));
    CFDictionarySetValue(matching, CFSTR(kIOPropertyMatchKey), property);
    CFRelease(property);

    // Look for a matching device that's already attached

    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);

    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "[SCSI] No device found for idVendor=%04x idProduct=%04x\n", idVendor, idProduct);
        return false;
    }

    IOCFPlugInInterface** plugin = 0;
    MMCDeviceInterface** mmc = 0;
    SInt32 score = 0;

    // Attach an MMC user client right here
    if (kIOReturnSuccess != IOCreatePlugInInterfaceForService(
            service, kIOMMCDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) ||
        S_OK != (*plugin)->QueryInterface(
            plugin, CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID), (LPVOID*) &mmc) ) {
        fprintf(stderr, "[SCSI] Failed to create MMC user client plugin for device\n");
        return false;
    }

    // Look for a parent where we can attach a USB user client
    io_registry_entry_t parent = 0;
    mUSB = 0;
    while (kIOReturnSuccess == IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent)) {
        service = parent;

        if (kIOReturnSuccess != IOCreatePlugInInterfaceForService(
                service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) ||
            S_OK != (*plugin)->QueryInterface(
                plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID187), (LPVOID*) &mUSB) ) {
            mUSB = 0;
            continue;
        }

        break;
    }
    if (!mUSB) {
        fprintf(stderr, "[SCSI] Failed to create USB user client plugin for device\n");
        return false;
    }

    // Now yank the device away from the operating system
    mInterface = (*mmc)->GetSCSITaskDeviceInterface(mmc);
    if (!mInterface || kIOReturnSuccess != (*mInterface)->ObtainExclusiveAccess(mInterface)) {
        fprintf(stderr, "[SCSI] Can't get exclusive access to the device. If there's a disc in the drive, try ejecting it.\n");
        return false;
    }

    // We only use one active task at a time
    mTask = (*mInterface)->CreateSCSITask(mInterface);
#endif // __APPLE__
    return true;
}

inline bool TinySCSI::reEnumerate()
{
#ifdef __APPLE__
    OSStatus kr;

    kr = (*mUSB)->USBDeviceOpen(mUSB);
    if (kr) {
        fprintf(stderr, "[SCSI] Failed to open USB device (%08x)\n", (int)kr);
        return false;
    }

    kr = (*mUSB)->ResetDevice(mUSB);
    if (kr) {
        fprintf(stderr, "[SCSI] Failed to reset USB device (%08x)\n", (int)kr);
        return false;
    }

    kr = (*mUSB)->USBDeviceReEnumerate(mUSB, 0);
    if (kr) {
        fprintf(stderr, "[SCSI] Failed to re-enumerate USB device (%08x)\n", (int)kr);
        return false;
    }
#endif // __APPLE__

    return true;
}

inline bool TinySCSI::command(uint8_t* cdb, unsigned cdbLen, DataDirection dir, uint8_t* data, unsigned dataLen)
{
#ifdef __APPLE__
    memset(&result, 0, sizeof result);

    uint32_t transferDirection;
    SCSITaskSGElement sgElement;

    sgElement.address = (uintptr_t) data;
    sgElement.length = dataLen;

    switch (dir) {
        case IN:
            transferDirection = kSCSIDataTransfer_FromTargetToInitiator;
            break;
        case OUT:
            transferDirection = kSCSIDataTransfer_FromInitiatorToTarget;
            break;
        case NONE:
        default:
            transferDirection = kSCSIDataTransfer_NoDataTransfer;
    }

    if (kIOReturnSuccess != (*mTask)->ResetForNewTask(mTask) ||
        kIOReturnSuccess != (*mTask)->SetCommandDescriptorBlock(mTask, (UInt8*)cdb, cdbLen) ||
        kIOReturnSuccess != (*mTask)->SetScatterGatherEntries(mTask, &sgElement, 1, dataLen, transferDirection) ||
        kIOReturnSuccess != (*mTask)->SetTimeoutDuration(mTask, 30000)) {
        fprintf(stderr, "[SCSI] Failed to set up SCSI task\n");
        return false;
    }

    if (kIOReturnSuccess != (*mTask)->ExecuteTaskSync(mTask, &result.senseData, &result.taskStatus, &result.transferCount)) {
        fprintf(stderr, "[SCSI] Failed to execute SCSI task\n");
        return false;
    }

    SCSIServiceResponse serviceResponse = kSCSIServiceResponse_Request_In_Process;
    if (kIOReturnSuccess != (*mTask)->GetSCSIServiceResponse(mTask, &serviceResponse) ||
        serviceResponse != kSCSIServiceResponse_TASK_COMPLETE) {
        fprintf(stderr, "[SCSI] Task not complete after synchronous execute?\n");
        return false;
    }

    if (result.taskStatus != kSCSITaskStatus_GOOD) {
        fprintf(stderr, "[SCSI] Command error\n");
        return false;
    }

#endif // __APPLE__
    return true;
}
