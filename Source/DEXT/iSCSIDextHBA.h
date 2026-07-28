/* iig(DriverKit-440) generated from iSCSIDextHBA.iig */

/* iSCSIDextHBA.iig:1-33 */
/*
 * Copyright (c) 2023, Nareg Sinenian
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <SCSIControllerDriverKit/IOUserSCSIParallelInterfaceController.h>  /* .iig include */
#include <DriverKit/IOBufferMemoryDescriptor.h>  /* .iig include */
#include <DriverKit/OSAction.h>  /* .iig include */
#include <DriverKit/OSDictionary.h>  /* .iig include */
#include <DriverKit/OSArray.h>  /* .iig include */
/* source class iSCSIDextHBA iSCSIDextHBA.iig:34-121 */

#if __DOCUMENTATION__
#define KERNEL IIG_KERNEL

class iSCSIDextHBA : public IOUserSCSIParallelInterfaceController
{
public:
    // IOService lifecycle
    virtual bool init() override;
    virtual void free() override;
    virtual kern_return_t Start(IOService * provider) override;
    virtual kern_return_t Stop(IOService * provider) override;

    // Create IOUserClient for daemon IPC connection
    virtual kern_return_t NewUserClient(uint32_t type, IOUserClient ** userClient) override;

    // ---- IOUserSCSIParallelInterfaceController overrides ----
    // Lifecycle
    virtual kern_return_t UserInitializeController() override;
    virtual kern_return_t UserStartController() override;

    // Target initialization
    virtual kern_return_t UserInitializeTargetForID(SCSITargetIdentifier targetID) override;

    // Feature queries
    virtual kern_return_t UserDoesHBAPerformAutoSense(bool * result) override;
    virtual kern_return_t UserDoesHBASupportMultiPathing(bool * result) override;

    // Task management functions
    virtual kern_return_t UserAbortTaskRequest(
        uint64_t theT, uint64_t theL, uint64_t theQ, uint32_t * response) override;
    virtual kern_return_t UserAbortTaskSetRequest(
        uint64_t theT, uint64_t theL, uint32_t * response) override;
    virtual kern_return_t UserClearACARequest(
        uint64_t theT, uint64_t theL, uint32_t * response) override;
    virtual kern_return_t UserClearTaskSetRequest(
        uint64_t theT, uint64_t theL, uint32_t * response) override;
    virtual kern_return_t UserLogicalUnitResetRequest(
        uint64_t theT, uint64_t theL, uint32_t * response) override;
    virtual kern_return_t UserTargetResetRequest(
        uint64_t theT, uint32_t * response) override;

    // Identity
    virtual kern_return_t UserReportInitiatorIdentifier(uint64_t * id) override;
    virtual kern_return_t UserReportHighestSupportedDeviceID(uint64_t * id) override;
    virtual kern_return_t UserReportMaximumTaskCount(uint32_t * count) override;
    virtual kern_return_t UserDoesHBAPerformDeviceManagement(bool * result) override;

    // Task processing and DMA
    virtual kern_return_t UserProcessParallelTask(
        SCSIUserParallelTask parallelRequest,
        uint32_t * response,
        OSAction * completion) override;
    virtual kern_return_t UserGetDMASpecification(
        uint64_t * maxTransferSize,
        uint32_t * alignment,
        uint8_t * numAddressBits,
        DMAOutputSegmentType * segmentType) override;
    virtual kern_return_t UserMapHBAData(uint32_t * uniqueTaskID) override;

    // Bundled task support
    virtual kern_return_t UserMapBundledParallelTaskCommandAndResponseBuffers(
        IOBufferMemoryDescriptor * parallelCommandIOMemoryDescriptor,
        IOBufferMemoryDescriptor * parallelResponseIOMemoryDescriptor) override;
    virtual void BundledParallelTaskCompletion(
        OSAction * action,
        const uint16_t parallelResponseSlotIndices[kMaxBundledParallelTasks],
        uint16_t parallelResponseSlotIndicesCount) override;
    virtual void UserProcessBundledParallelTasks(
        const uint16_t parallelRequestSlotIndices[kMaxBundledParallelTasks],
        uint16_t parallelRequestSlotIndicesCount,
        OSAction * completion) override;

    // Completion callback
    virtual void ParallelTaskCompletion(
        OSAction * action,
        SCSIUserParallelResponse response) override;

    // Non-pure virtual overrides
    virtual kern_return_t UserGetDataBuffer(
        SCSIDeviceIdentifier targetID,
        uint64_t controllerTaskID,
        IOBufferMemoryDescriptor ** buffer) override;
    virtual kern_return_t UserReportHBAHighestLogicalUnitNumber(uint64_t * value) override;
    virtual kern_return_t UserDoesHBASupportSCSIParallelFeature(
        uint32_t theValue, bool * result) override;
    virtual kern_return_t UserCreateTargetForID(
        SCSIDeviceIdentifier targetID, OSDictionary * targetDict) override;
    virtual kern_return_t UserDestroyTargetForID(SCSITargetIdentifier targetID) override;
    virtual kern_return_t UserReportHBAConstraints(OSDictionary * constraints) override;
    virtual kern_return_t UserTargetPresentForID(
        SCSIDeviceIdentifier targetID, bool * result) override;
};

#undef KERNEL
#else /* __DOCUMENTATION__ */

/* generated class iSCSIDextHBA iSCSIDextHBA.iig:34-121 */


#define iSCSIDextHBA_Start_Args \
        IOService * provider

#define iSCSIDextHBA_Stop_Args \
        IOService * provider

#define iSCSIDextHBA_NewUserClient_Args \
        uint32_t type, \
        IOUserClient ** userClient

#define iSCSIDextHBA_UserInitializeController_Args \


#define iSCSIDextHBA_UserStartController_Args \


#define iSCSIDextHBA_UserInitializeTargetForID_Args \
        SCSITargetIdentifier targetID

#define iSCSIDextHBA_UserDoesHBAPerformAutoSense_Args \
        bool * result

#define iSCSIDextHBA_UserDoesHBASupportMultiPathing_Args \
        bool * result

#define iSCSIDextHBA_UserAbortTaskRequest_Args \
        uint64_t theT, \
        uint64_t theL, \
        uint64_t theQ, \
        uint32_t * response

#define iSCSIDextHBA_UserAbortTaskSetRequest_Args \
        uint64_t theT, \
        uint64_t theL, \
        uint32_t * response

#define iSCSIDextHBA_UserClearACARequest_Args \
        uint64_t theT, \
        uint64_t theL, \
        uint32_t * response

#define iSCSIDextHBA_UserClearTaskSetRequest_Args \
        uint64_t theT, \
        uint64_t theL, \
        uint32_t * response

#define iSCSIDextHBA_UserLogicalUnitResetRequest_Args \
        uint64_t theT, \
        uint64_t theL, \
        uint32_t * response

#define iSCSIDextHBA_UserTargetResetRequest_Args \
        uint64_t theT, \
        uint32_t * response

#define iSCSIDextHBA_UserReportInitiatorIdentifier_Args \
        uint64_t * id

#define iSCSIDextHBA_UserReportHighestSupportedDeviceID_Args \
        uint64_t * id

#define iSCSIDextHBA_UserReportMaximumTaskCount_Args \
        uint32_t * count

#define iSCSIDextHBA_UserDoesHBAPerformDeviceManagement_Args \
        bool * result

#define iSCSIDextHBA_UserProcessParallelTask_Args \
        SCSIUserParallelTask parallelRequest, \
        uint32_t * response, \
        OSAction * completion

#define iSCSIDextHBA_UserGetDMASpecification_Args \
        uint64_t * maxTransferSize, \
        uint32_t * alignment, \
        uint8_t * numAddressBits, \
        DMAOutputSegmentType * segmentType

#define iSCSIDextHBA_UserMapHBAData_Args \
        uint32_t * uniqueTaskID

#define iSCSIDextHBA_UserMapBundledParallelTaskCommandAndResponseBuffers_Args \
        IOBufferMemoryDescriptor * parallelCommandIOMemoryDescriptor, \
        IOBufferMemoryDescriptor * parallelResponseIOMemoryDescriptor

#define iSCSIDextHBA_BundledParallelTaskCompletion_Args \
        OSAction * action, \
        const unsigned short * parallelResponseSlotIndices, \
        uint16_t parallelResponseSlotIndicesCount

#define iSCSIDextHBA_UserProcessBundledParallelTasks_Args \
        const unsigned short * parallelRequestSlotIndices, \
        uint16_t parallelRequestSlotIndicesCount, \
        OSAction * completion

#define iSCSIDextHBA_ParallelTaskCompletion_Args \
        OSAction * action, \
        SCSIUserParallelResponse response

#define iSCSIDextHBA_UserGetDataBuffer_Args \
        SCSIDeviceIdentifier targetID, \
        uint64_t controllerTaskID, \
        IOBufferMemoryDescriptor ** buffer

#define iSCSIDextHBA_UserReportHBAHighestLogicalUnitNumber_Args \
        uint64_t * value

#define iSCSIDextHBA_UserDoesHBASupportSCSIParallelFeature_Args \
        uint32_t theValue, \
        bool * result

#define iSCSIDextHBA_UserCreateTargetForID_Args \
        SCSIDeviceIdentifier targetID, \
        OSDictionary * targetDict

#define iSCSIDextHBA_UserDestroyTargetForID_Args \
        SCSITargetIdentifier targetID

#define iSCSIDextHBA_UserReportHBAConstraints_Args \
        OSDictionary * constraints

#define iSCSIDextHBA_UserTargetPresentForID_Args \
        SCSIDeviceIdentifier targetID, \
        bool * result

#define iSCSIDextHBA_Methods \
\
public:\
\
    virtual kern_return_t\
    Dispatch(const IORPC rpc) APPLE_KEXT_OVERRIDE;\
\
    static kern_return_t\
    _Dispatch(iSCSIDextHBA * self, const IORPC rpc);\
\
\
protected:\
    /* _Impl methods */\
\
    kern_return_t\
    Start_Impl(IOService_Start_Args);\
\
    kern_return_t\
    Stop_Impl(IOService_Stop_Args);\
\
    kern_return_t\
    NewUserClient_Impl(IOService_NewUserClient_Args);\
\
    kern_return_t\
    UserInitializeController_Impl(IOUserSCSIParallelInterfaceController_UserInitializeController_Args);\
\
    kern_return_t\
    UserStartController_Impl(IOUserSCSIParallelInterfaceController_UserStartController_Args);\
\
    kern_return_t\
    UserInitializeTargetForID_Impl(IOUserSCSIParallelInterfaceController_UserInitializeTargetForID_Args);\
\
    kern_return_t\
    UserDoesHBAPerformAutoSense_Impl(IOUserSCSIParallelInterfaceController_UserDoesHBAPerformAutoSense_Args);\
\
    kern_return_t\
    UserDoesHBASupportMultiPathing_Impl(IOUserSCSIParallelInterfaceController_UserDoesHBASupportMultiPathing_Args);\
\
    kern_return_t\
    UserAbortTaskRequest_Impl(IOUserSCSIParallelInterfaceController_UserAbortTaskRequest_Args);\
\
    kern_return_t\
    UserAbortTaskSetRequest_Impl(IOUserSCSIParallelInterfaceController_UserAbortTaskSetRequest_Args);\
\
    kern_return_t\
    UserClearACARequest_Impl(IOUserSCSIParallelInterfaceController_UserClearACARequest_Args);\
\
    kern_return_t\
    UserClearTaskSetRequest_Impl(IOUserSCSIParallelInterfaceController_UserClearTaskSetRequest_Args);\
\
    kern_return_t\
    UserLogicalUnitResetRequest_Impl(IOUserSCSIParallelInterfaceController_UserLogicalUnitResetRequest_Args);\
\
    kern_return_t\
    UserTargetResetRequest_Impl(IOUserSCSIParallelInterfaceController_UserTargetResetRequest_Args);\
\
    kern_return_t\
    UserReportInitiatorIdentifier_Impl(IOUserSCSIParallelInterfaceController_UserReportInitiatorIdentifier_Args);\
\
    kern_return_t\
    UserReportHighestSupportedDeviceID_Impl(IOUserSCSIParallelInterfaceController_UserReportHighestSupportedDeviceID_Args);\
\
    kern_return_t\
    UserReportMaximumTaskCount_Impl(IOUserSCSIParallelInterfaceController_UserReportMaximumTaskCount_Args);\
\
    kern_return_t\
    UserDoesHBAPerformDeviceManagement_Impl(IOUserSCSIParallelInterfaceController_UserDoesHBAPerformDeviceManagement_Args);\
\
    kern_return_t\
    UserProcessParallelTask_Impl(IOUserSCSIParallelInterfaceController_UserProcessParallelTask_Args);\
\
    kern_return_t\
    UserGetDMASpecification_Impl(IOUserSCSIParallelInterfaceController_UserGetDMASpecification_Args);\
\
    kern_return_t\
    UserMapHBAData_Impl(IOUserSCSIParallelInterfaceController_UserMapHBAData_Args);\
\
    kern_return_t\
    UserMapBundledParallelTaskCommandAndResponseBuffers_Impl(IOUserSCSIParallelInterfaceController_UserMapBundledParallelTaskCommandAndResponseBuffers_Args);\
\
    void\
    BundledParallelTaskCompletion_Impl(IOUserSCSIParallelInterfaceController_BundledParallelTaskCompletion_Args);\
\
    void\
    UserProcessBundledParallelTasks_Impl(IOUserSCSIParallelInterfaceController_UserProcessBundledParallelTasks_Args);\
\
    void\
    ParallelTaskCompletion_Impl(IOUserSCSIParallelInterfaceController_ParallelTaskCompletion_Args);\
\
    kern_return_t\
    UserGetDataBuffer_Impl(IOUserSCSIParallelInterfaceController_UserGetDataBuffer_Args);\
\
    kern_return_t\
    UserReportHBAHighestLogicalUnitNumber_Impl(IOUserSCSIParallelInterfaceController_UserReportHBAHighestLogicalUnitNumber_Args);\
\
    kern_return_t\
    UserDoesHBASupportSCSIParallelFeature_Impl(IOUserSCSIParallelInterfaceController_UserDoesHBASupportSCSIParallelFeature_Args);\
\
    kern_return_t\
    UserCreateTargetForID_Impl(IOUserSCSIParallelInterfaceController_UserCreateTargetForID_Args);\
\
    kern_return_t\
    UserDestroyTargetForID_Impl(IOUserSCSIParallelInterfaceController_UserDestroyTargetForID_Args);\
\
    kern_return_t\
    UserReportHBAConstraints_Impl(IOUserSCSIParallelInterfaceController_UserReportHBAConstraints_Args);\
\
    kern_return_t\
    UserTargetPresentForID_Impl(IOUserSCSIParallelInterfaceController_UserTargetPresentForID_Args);\
\
\
public:\
    /* _Invoke methods */\
\


#define iSCSIDextHBA_KernelMethods \
\
protected:\
    /* _Impl methods */\
\


#define iSCSIDextHBA_VirtualMethods \
\
public:\
\
    virtual bool\
    init(\
) APPLE_KEXT_OVERRIDE;\
\
    virtual void\
    free(\
) APPLE_KEXT_OVERRIDE;\
\


#if !KERNEL

extern OSMetaClass          * giSCSIDextHBAMetaClass;
extern const OSClassLoadInformation iSCSIDextHBA_Class;

class iSCSIDextHBAMetaClass : public OSMetaClass
{
public:
    virtual kern_return_t
    New(OSObject * instance) override;
    virtual kern_return_t
    Dispatch(const IORPC rpc) override;
};

#endif /* !KERNEL */

#if !KERNEL

class  iSCSIDextHBAInterface : public OSInterface
{
public:
};

struct iSCSIDextHBA_IVars;
struct iSCSIDextHBA_LocalIVars;

class iSCSIDextHBA : public IOUserSCSIParallelInterfaceController, public iSCSIDextHBAInterface
{
#if !KERNEL
    friend class iSCSIDextHBAMetaClass;
#endif /* !KERNEL */

#if !KERNEL
public:
#ifdef iSCSIDextHBA_DECLARE_IVARS
iSCSIDextHBA_DECLARE_IVARS
#else /* iSCSIDextHBA_DECLARE_IVARS */
    union
    {
        iSCSIDextHBA_IVars * ivars;
        iSCSIDextHBA_LocalIVars * lvars;
    };
#endif /* iSCSIDextHBA_DECLARE_IVARS */
#endif /* !KERNEL */

#if !KERNEL
    static OSMetaClass *
    sGetMetaClass() { return giSCSIDextHBAMetaClass; };
#endif /* KERNEL */

    using super = IOUserSCSIParallelInterfaceController;

#if !KERNEL
    iSCSIDextHBA_Methods
    iSCSIDextHBA_VirtualMethods
#endif /* !KERNEL */

};
#endif /* !KERNEL */


#endif /* !__DOCUMENTATION__ */

/* iSCSIDextHBA.iig:123- */
