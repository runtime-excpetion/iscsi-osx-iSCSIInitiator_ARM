/* iig(DriverKit-440 May 22 2026 10:59:06) generated from iSCSIDextHBA.iig */

#undef	IIG_IMPLEMENTATION
#define	IIG_IMPLEMENTATION 	iSCSIDextHBA.iig

#if KERNEL
#include <libkern/c++/OSString.h>
#else
#include <DriverKit/DriverKit.h>
#endif /* KERNEL */
#include <DriverKit/IOReturn.h>
#include "iSCSIDextHBA.h"


#if __has_builtin(__builtin_load_member_function_pointer)
#define SimpleMemberFunctionCast(cfnty, self, func) (cfnty)__builtin_load_member_function_pointer(self, func)
#else
#define SimpleMemberFunctionCast(cfnty, self, func) ({ union { typeof(func) memfun; cfnty cfun; } pair; pair.memfun = func; pair.cfun; })
#endif


#if !KERNEL
extern OSMetaClass * gOSDataMetaClass;
extern OSMetaClass * gOSNumberMetaClass;
extern OSMetaClass * gOSBooleanMetaClass;
extern OSMetaClass * gOSSetMetaClass;
extern OSMetaClass * gOSOrderedSetMetaClass;
extern OSMetaClass * gIODispatchQueueMetaClass;
extern OSMetaClass * gIOUserClientMetaClass;
extern OSMetaClass * gIOServiceStateNotificationDispatchSourceMetaClass;
extern OSMetaClass * gOSStringMetaClass;
extern OSMetaClass * gIOMemoryMapMetaClass;
extern OSMetaClass * gOSAction_IOUserSCSIParallelInterfaceController_UserCompleteBundledParallelTaskMetaClass;
extern OSMetaClass * gOSAction_IOUserSCSIParallelInterfaceController_UserCompleteParallelTaskMetaClass;
#endif /* !KERNEL */

#if !KERNEL

#define iSCSIDextHBA_QueueNames  "" \
    "\016AuxiliaryQueue"

#define iSCSIDextHBA_MethodNames  "" \
    "\025UserCreateTargetForID"

#define iSCSIDextHBAMetaClass_MethodNames  ""

struct OSClassDescription_iSCSIDextHBA_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 1];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(iSCSIDextHBA_QueueNames)];
    char               methodNames[sizeof(iSCSIDextHBA_MethodNames)];
    char               metaMethodNames[sizeof(iSCSIDextHBAMetaClass_MethodNames)];
};

const struct OSClassDescription_iSCSIDextHBA_t
OSClassDescription_iSCSIDextHBA =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_iSCSIDextHBA_t),
        .name                    = "iSCSIDextHBA",
        .superName               = "IOUserSCSIParallelInterfaceController",
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 1,
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_iSCSIDextHBA_t, methodOptions),
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_iSCSIDextHBA_t, metaMethodOptions),
        .queueNamesSize       = sizeof(iSCSIDextHBA_QueueNames),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_iSCSIDextHBA_t, queueNames),
        .methodNamesSize         = sizeof(iSCSIDextHBA_MethodNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_iSCSIDextHBA_t, methodNames),
        .metaMethodNamesSize     = sizeof(iSCSIDextHBAMetaClass_MethodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_iSCSIDextHBA_t, metaMethodNames),
        .flags                   = 0*kOSClassCanRemote,
        .resv1                   = {0},
    },
    .methodOptions =
    {
        IOUserSCSIParallelInterfaceController_UserCreateTargetForID_ID,
        0x0000000000000000,
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = iSCSIDextHBA_QueueNames,
    .methodNames     = iSCSIDextHBA_MethodNames,
    .metaMethodNames = iSCSIDextHBAMetaClass_MethodNames,
};

OSMetaClass * giSCSIDextHBAMetaClass;

static kern_return_t
iSCSIDextHBA_New(OSMetaClass * instance);

const OSClassLoadInformation
iSCSIDextHBA_Class = 
{
    .description       = &OSClassDescription_iSCSIDextHBA.base,
    .metaPointer       = &giSCSIDextHBAMetaClass,
    .version           = 1,
    .instanceSize      = sizeof(iSCSIDextHBA),

    .resv2             = {0},

    .New               = &iSCSIDextHBA_New,
    .resv3             = {0},

};

extern const void * const
giSCSIDextHBA_Declaration;
const void * const
giSCSIDextHBA_Declaration
__attribute__((used,visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip"),no_sanitize("address")))
    = &iSCSIDextHBA_Class;

static kern_return_t
iSCSIDextHBA_New(OSMetaClass * instance)
{
    if (!new(instance) iSCSIDextHBAMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
iSCSIDextHBAMetaClass::New(OSObject * instance)
{
    if (!new(instance) iSCSIDextHBA) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

#ifdef KERNEL
#define MESSAGE_CONTENT(__field) (messageContent->__field)
#else /* KERNEL */
#define MESSAGE_CONTENT(__field) (message->content.__field)
#endif /* KERNEL */

kern_return_t
iSCSIDextHBA::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
iSCSIDextHBA::_Dispatch(iSCSIDextHBA * self, const IORPC rpc)
{
    kern_return_t ret = kIOReturnUnsupported;
#ifdef KERNEL
    IORPCMessage * msg = rpc.kernelContent;
#else /* KERNEL */
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);
#endif /* KERNEL */

    switch (msg->msgid)
    {
        case IOService_Start_ID:
        {
            ret = IOService::Start_Invoke(rpc, self, SimpleMemberFunctionCast(IOService::Start_Handler, *self, &iSCSIDextHBA::Start_Impl));
            break;
        }
        case IOService_Stop_ID:
        {
            ret = IOService::Stop_Invoke(rpc, self, SimpleMemberFunctionCast(IOService::Stop_Handler, *self, &iSCSIDextHBA::Stop_Impl));
            break;
        }
        case IOService_NewUserClient_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOService::NewUserClient_Invoke(rpc, self, SimpleMemberFunctionCast(IOService::NewUserClient_Handler, *self, &iSCSIDextHBA::NewUserClient_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserInitializeController_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserInitializeController_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserInitializeController_Handler, *self, &iSCSIDextHBA::UserInitializeController_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserStartController_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserStartController_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserStartController_Handler, *self, &iSCSIDextHBA::UserStartController_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserInitializeTargetForID_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserInitializeTargetForID_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserInitializeTargetForID_Handler, *self, &iSCSIDextHBA::UserInitializeTargetForID_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserDoesHBAPerformAutoSense_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserDoesHBAPerformAutoSense_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserDoesHBAPerformAutoSense_Handler, *self, &iSCSIDextHBA::UserDoesHBAPerformAutoSense_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserDoesHBASupportMultiPathing_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserDoesHBASupportMultiPathing_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserDoesHBASupportMultiPathing_Handler, *self, &iSCSIDextHBA::UserDoesHBASupportMultiPathing_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserAbortTaskRequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserAbortTaskRequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserAbortTaskRequest_Handler, *self, &iSCSIDextHBA::UserAbortTaskRequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserAbortTaskSetRequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserAbortTaskSetRequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserAbortTaskSetRequest_Handler, *self, &iSCSIDextHBA::UserAbortTaskSetRequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserClearACARequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserClearACARequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserClearACARequest_Handler, *self, &iSCSIDextHBA::UserClearACARequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserClearTaskSetRequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserClearTaskSetRequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserClearTaskSetRequest_Handler, *self, &iSCSIDextHBA::UserClearTaskSetRequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserLogicalUnitResetRequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserLogicalUnitResetRequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserLogicalUnitResetRequest_Handler, *self, &iSCSIDextHBA::UserLogicalUnitResetRequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserTargetResetRequest_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserTargetResetRequest_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserTargetResetRequest_Handler, *self, &iSCSIDextHBA::UserTargetResetRequest_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserReportInitiatorIdentifier_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserReportInitiatorIdentifier_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserReportInitiatorIdentifier_Handler, *self, &iSCSIDextHBA::UserReportInitiatorIdentifier_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserReportHighestSupportedDeviceID_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserReportHighestSupportedDeviceID_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserReportHighestSupportedDeviceID_Handler, *self, &iSCSIDextHBA::UserReportHighestSupportedDeviceID_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserReportMaximumTaskCount_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserReportMaximumTaskCount_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserReportMaximumTaskCount_Handler, *self, &iSCSIDextHBA::UserReportMaximumTaskCount_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserDoesHBAPerformDeviceManagement_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserDoesHBAPerformDeviceManagement_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserDoesHBAPerformDeviceManagement_Handler, *self, &iSCSIDextHBA::UserDoesHBAPerformDeviceManagement_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserProcessParallelTask_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserProcessParallelTask_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserProcessParallelTask_Handler, *self, &iSCSIDextHBA::UserProcessParallelTask_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserGetDMASpecification_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserGetDMASpecification_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserGetDMASpecification_Handler, *self, &iSCSIDextHBA::UserGetDMASpecification_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserMapHBAData_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserMapHBAData_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserMapHBAData_Handler, *self, &iSCSIDextHBA::UserMapHBAData_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserMapBundledParallelTaskCommandAndResponseBuffers_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserMapBundledParallelTaskCommandAndResponseBuffers_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserMapBundledParallelTaskCommandAndResponseBuffers_Handler, *self, &iSCSIDextHBA::UserMapBundledParallelTaskCommandAndResponseBuffers_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_BundledParallelTaskCompletion_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::BundledParallelTaskCompletion_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::BundledParallelTaskCompletion_Handler, *self, &iSCSIDextHBA::BundledParallelTaskCompletion_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserProcessBundledParallelTasks_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserProcessBundledParallelTasks_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserProcessBundledParallelTasks_Handler, *self, &iSCSIDextHBA::UserProcessBundledParallelTasks_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_ParallelTaskCompletion_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::ParallelTaskCompletion_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::ParallelTaskCompletion_Handler, *self, &iSCSIDextHBA::ParallelTaskCompletion_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserGetDataBuffer_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserGetDataBuffer_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserGetDataBuffer_Handler, *self, &iSCSIDextHBA::UserGetDataBuffer_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserReportHBAHighestLogicalUnitNumber_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserReportHBAHighestLogicalUnitNumber_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserReportHBAHighestLogicalUnitNumber_Handler, *self, &iSCSIDextHBA::UserReportHBAHighestLogicalUnitNumber_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserDoesHBASupportSCSIParallelFeature_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserDoesHBASupportSCSIParallelFeature_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserDoesHBASupportSCSIParallelFeature_Handler, *self, &iSCSIDextHBA::UserDoesHBASupportSCSIParallelFeature_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserCreateTargetForID_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserCreateTargetForID_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserCreateTargetForID_Handler, *self, &iSCSIDextHBA::UserCreateTargetForID_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserDestroyTargetForID_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserDestroyTargetForID_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserDestroyTargetForID_Handler, *self, &iSCSIDextHBA::UserDestroyTargetForID_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserReportHBAConstraints_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserReportHBAConstraints_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserReportHBAConstraints_Handler, *self, &iSCSIDextHBA::UserReportHBAConstraints_Impl));
            break;
        }
        case IOUserSCSIParallelInterfaceController_UserTargetPresentForID_ID:
#if !KERNEL
        if (self->IsRemote())
        {
            ret = self->OSMetaClassBase::Dispatch(rpc);
            break;
        }
        else
#endif /* !KERNEL */
        {
            ret = IOUserSCSIParallelInterfaceController::UserTargetPresentForID_Invoke(rpc, self, SimpleMemberFunctionCast(IOUserSCSIParallelInterfaceController::UserTargetPresentForID_Handler, *self, &iSCSIDextHBA::UserTargetPresentForID_Impl));
            break;
        }

        default:
            ret = IOUserSCSIParallelInterfaceController::_Dispatch(self, rpc);
            break;
    }

    return (ret);
}

#if KERNEL
kern_return_t
iSCSIDextHBA::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
iSCSIDextHBAMetaClass::Dispatch(const IORPC rpc)
{
#endif /* !KERNEL */

    kern_return_t ret = kIOReturnUnsupported;
#ifdef KERNEL
    IORPCMessage * msg = rpc.kernelContent;
#else /* KERNEL */
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);
#endif /* KERNEL */

    switch (msg->msgid)
    {

        default:
            ret = OSMetaClassBase::Dispatch(rpc);
            break;
    }

    return (ret);
}



