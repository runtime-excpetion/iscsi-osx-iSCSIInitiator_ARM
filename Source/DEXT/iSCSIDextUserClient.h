/* iig(DriverKit-440) generated from iSCSIDextUserClient.iig */

/* iSCSIDextUserClient.iig:1-42 */
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

/*
 * iSCSIDextUserClient.iig — IOUserClient subclass for daemon IPC.
 *
 * This user client bridges the daemon's IOConnectCallMethod calls
 * to the DEXT's session/connection state and SCSI data path.
 *
 * The daemon opens this user client via IOServiceOpen on the
 * iSCSIDextHBA service, then uses IOConnectCallMethod with
 * DaemonCommandType selectors to manage sessions, connections,
 * and forward iSCSI PDUs.
 */

#include <DriverKit/IOUserClient.h>  /* .iig include */

/* source class iSCSIDextUserClient iSCSIDextUserClient.iig:43-49 */

#if __DOCUMENTATION__
#define KERNEL IIG_KERNEL

class iSCSIDextUserClient : public IOUserClient
{
public:
    virtual bool init() override;
    virtual void free() override;
    virtual kern_return_t Start(IOService * provider) override;
    virtual kern_return_t Stop(IOService * provider) override;
};

#undef KERNEL
#else /* __DOCUMENTATION__ */

/* generated class iSCSIDextUserClient iSCSIDextUserClient.iig:43-49 */


#define iSCSIDextUserClient_Start_Args \
        IOService * provider

#define iSCSIDextUserClient_Stop_Args \
        IOService * provider

#define iSCSIDextUserClient_Methods \
\
public:\
\
    virtual kern_return_t\
    Dispatch(const IORPC rpc) APPLE_KEXT_OVERRIDE;\
\
    static kern_return_t\
    _Dispatch(iSCSIDextUserClient * self, const IORPC rpc);\
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
\
public:\
    /* _Invoke methods */\
\


#define iSCSIDextUserClient_KernelMethods \
\
protected:\
    /* _Impl methods */\
\


#define iSCSIDextUserClient_VirtualMethods \
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

extern OSMetaClass          * giSCSIDextUserClientMetaClass;
extern const OSClassLoadInformation iSCSIDextUserClient_Class;

class iSCSIDextUserClientMetaClass : public OSMetaClass
{
public:
    virtual kern_return_t
    New(OSObject * instance) override;
    virtual kern_return_t
    Dispatch(const IORPC rpc) override;
};

#endif /* !KERNEL */

#if !KERNEL

class  iSCSIDextUserClientInterface : public OSInterface
{
public:
};

struct iSCSIDextUserClient_IVars;
struct iSCSIDextUserClient_LocalIVars;

class iSCSIDextUserClient : public IOUserClient, public iSCSIDextUserClientInterface
{
#if !KERNEL
    friend class iSCSIDextUserClientMetaClass;
#endif /* !KERNEL */

#if !KERNEL
public:
#ifdef iSCSIDextUserClient_DECLARE_IVARS
iSCSIDextUserClient_DECLARE_IVARS
#else /* iSCSIDextUserClient_DECLARE_IVARS */
    union
    {
        iSCSIDextUserClient_IVars * ivars;
        iSCSIDextUserClient_LocalIVars * lvars;
    };
#endif /* iSCSIDextUserClient_DECLARE_IVARS */
#endif /* !KERNEL */

#if !KERNEL
    static OSMetaClass *
    sGetMetaClass() { return giSCSIDextUserClientMetaClass; };
#endif /* KERNEL */

    using super = IOUserClient;

#if !KERNEL
    iSCSIDextUserClient_Methods
    iSCSIDextUserClient_VirtualMethods
#endif /* !KERNEL */

};
#endif /* !KERNEL */


#endif /* !__DOCUMENTATION__ */

/* iSCSIDextUserClient.iig:51- */
