/* iig(DriverKit-440 May 22 2026 10:59:06) generated from iSCSIDextUserClient.iig */

#undef	IIG_IMPLEMENTATION
#define	IIG_IMPLEMENTATION 	iSCSIDextUserClient.iig

#if KERNEL
#include <libkern/c++/OSString.h>
#else
#include <DriverKit/DriverKit.h>
#endif /* KERNEL */
#include <DriverKit/IOReturn.h>
#include "iSCSIDextUserClient.h"


#if __has_builtin(__builtin_load_member_function_pointer)
#define SimpleMemberFunctionCast(cfnty, self, func) (cfnty)__builtin_load_member_function_pointer(self, func)
#else
#define SimpleMemberFunctionCast(cfnty, self, func) ({ union { typeof(func) memfun; cfnty cfun; } pair; pair.memfun = func; pair.cfun; })
#endif


#if !KERNEL
extern OSMetaClass * gOSContainerMetaClass;
extern OSMetaClass * gOSDataMetaClass;
extern OSMetaClass * gOSNumberMetaClass;
extern OSMetaClass * gOSBooleanMetaClass;
extern OSMetaClass * gOSDictionaryMetaClass;
extern OSMetaClass * gOSArrayMetaClass;
extern OSMetaClass * gOSSetMetaClass;
extern OSMetaClass * gOSOrderedSetMetaClass;
extern OSMetaClass * gIODispatchQueueMetaClass;
extern OSMetaClass * gOSStringMetaClass;
extern OSMetaClass * gIOServiceStateNotificationDispatchSourceMetaClass;
extern OSMetaClass * gIOMemoryMapMetaClass;
extern OSMetaClass * gOSAction_IOUserClient_KernelCompletionMetaClass;
#endif /* !KERNEL */

#if !KERNEL

#define iSCSIDextUserClient_QueueNames  "" \
    "\037IOUserClientQueueExternalMethod"

#define iSCSIDextUserClient_MethodNames  "" \
    "\017_ExternalMethod"

#define iSCSIDextUserClientMetaClass_MethodNames  ""

struct OSClassDescription_iSCSIDextUserClient_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 1];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(iSCSIDextUserClient_QueueNames)];
    char               methodNames[sizeof(iSCSIDextUserClient_MethodNames)];
    char               metaMethodNames[sizeof(iSCSIDextUserClientMetaClass_MethodNames)];
};

const struct OSClassDescription_iSCSIDextUserClient_t
OSClassDescription_iSCSIDextUserClient =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_iSCSIDextUserClient_t),
        .name                    = "iSCSIDextUserClient",
        .superName               = "IOUserClient",
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 1,
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_iSCSIDextUserClient_t, methodOptions),
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_iSCSIDextUserClient_t, metaMethodOptions),
        .queueNamesSize       = sizeof(iSCSIDextUserClient_QueueNames),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_iSCSIDextUserClient_t, queueNames),
        .methodNamesSize         = sizeof(iSCSIDextUserClient_MethodNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_iSCSIDextUserClient_t, methodNames),
        .metaMethodNamesSize     = sizeof(iSCSIDextUserClientMetaClass_MethodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_iSCSIDextUserClient_t, metaMethodNames),
        .flags                   = 0*kOSClassCanRemote,
        .resv1                   = {0},
    },
    .methodOptions =
    {
        IOUserClient__ExternalMethod_ID,
        0x0000000000000000,
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = iSCSIDextUserClient_QueueNames,
    .methodNames     = iSCSIDextUserClient_MethodNames,
    .metaMethodNames = iSCSIDextUserClientMetaClass_MethodNames,
};

OSMetaClass * giSCSIDextUserClientMetaClass;

static kern_return_t
iSCSIDextUserClient_New(OSMetaClass * instance);

const OSClassLoadInformation
iSCSIDextUserClient_Class = 
{
    .description       = &OSClassDescription_iSCSIDextUserClient.base,
    .metaPointer       = &giSCSIDextUserClientMetaClass,
    .version           = 1,
    .instanceSize      = sizeof(iSCSIDextUserClient),

    .resv2             = {0},

    .New               = &iSCSIDextUserClient_New,
    .resv3             = {0},

};

extern const void * const
giSCSIDextUserClient_Declaration;
const void * const
giSCSIDextUserClient_Declaration
__attribute__((used,visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip"),no_sanitize("address")))
    = &iSCSIDextUserClient_Class;

static kern_return_t
iSCSIDextUserClient_New(OSMetaClass * instance)
{
    if (!new(instance) iSCSIDextUserClientMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
iSCSIDextUserClientMetaClass::New(OSObject * instance)
{
    if (!new(instance) iSCSIDextUserClient) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

#ifdef KERNEL
#define MESSAGE_CONTENT(__field) (messageContent->__field)
#else /* KERNEL */
#define MESSAGE_CONTENT(__field) (message->content.__field)
#endif /* KERNEL */

kern_return_t
iSCSIDextUserClient::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
iSCSIDextUserClient::_Dispatch(iSCSIDextUserClient * self, const IORPC rpc)
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
            ret = IOService::Start_Invoke(rpc, self, SimpleMemberFunctionCast(IOService::Start_Handler, *self, &iSCSIDextUserClient::Start_Impl));
            break;
        }
        case IOService_Stop_ID:
        {
            ret = IOService::Stop_Invoke(rpc, self, SimpleMemberFunctionCast(IOService::Stop_Handler, *self, &iSCSIDextUserClient::Stop_Impl));
            break;
        }

        default:
            ret = IOUserClient::_Dispatch(self, rpc);
            break;
    }

    return (ret);
}

#if KERNEL
kern_return_t
iSCSIDextUserClient::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
iSCSIDextUserClientMetaClass::Dispatch(const IORPC rpc)
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



