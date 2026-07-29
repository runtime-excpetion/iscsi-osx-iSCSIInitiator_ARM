/*
 * Copyright (c) 2016, Nareg Sinenian
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
 * LIABLE FOR ANY DIRECT, INDIRECT, INConnectionIdentifierENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "iSCSIHBAInterface.h"
#include "iSCSIHBATypes.h"
#include "iSCSIPDUUser.h"
#include "iSCSIDextIPC.h"

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// Phase E: TCP engine & PDU relay for full-feature data path
#include "iSCSITCPEngine.h"
#include "iSCSIPDURelay.h"

#pragma mark - DEXT Session Tracking

/*! Maximum number of DEXT sessions tracked. */
#define kDEXTMaxSessions 16
/*! Maximum number of connections per DEXT session. */
#define kDEXTMaxConnectionsPerSession 8
/*! Maximum cached parameter values per session/connection. */
#define kDEXTMaxCachedParams 32
/*! Session identifier base for DEXT-mode sessions to avoid collision with kext IDs. */
#define kDEXTSessionIdBase ((SessionIdentifier)0x1000000000000000ULL)

/*! Tracks one DEXT-mode session's state and TCP socket. */
typedef struct {
    bool inUse;
    uint8_t dextSessionId;
    int tcpSocket;                                // primary TCP socket fd

    /*! TCP engine context for PDURelay data path (NULL before ActivateConnection). */
    struct TCPConnectionContext * tcpCtx;

    uint32_t connectionCount;
    ConnectionIdentifier connectionIds[kDEXTMaxConnectionsPerSession];
    int connectionSockets[kDEXTMaxConnectionsPerSession];

    char targetIQN[256];
    char portalAddress[NI_MAXHOST];
    char portalPort[NI_MAXSERV];
    char hostInterface[NI_MAXHOST];

    uint64_t cachedSessionParams[kDEXTMaxCachedParams];
    uint64_t cachedConnectionParams[kDEXTMaxConnectionsPerSession][kDEXTMaxCachedParams];
} DextSessionEntry;

/*! Converts from DEXT uint8_t session ID to SessionIdentifier (UInt64). */
static inline SessionIdentifier dextMakeSessionId(uint8_t dextSessionId) {
    return kDEXTSessionIdBase | (SessionIdentifier)dextSessionId;
}

/*! Extracts DEXT uint8_t session ID from a SessionIdentifier. */
static inline uint8_t dextSessionIdFromId(SessionIdentifier sid) {
    return (uint8_t)(sid & 0xFF);
}

/*! Finds the TCP socket fd associated with a connectionId in a session entry.
 *  This function only depends on DextSessionEntry (already defined above). */
static int dextConnectionSocket(DextSessionEntry *entry, ConnectionIdentifier connId) {
    for (uint32_t i = 0; i < entry->connectionCount && i < kDEXTMaxConnectionsPerSession; i++) {
        if (entry->connectionIds[i] == connId)
            return entry->connectionSockets[i];
    }
    return -1;
}

struct __iSCSIHBAInterface {

    /*! Allocator used to create this instance. */
    CFAllocatorRef allocator;

    /*! The iSCSI HBA service. */
    io_service_t service;

    /*! Connection to kernel user client. */
    io_connect_t connect;

    /*! Runloop source associated with this instance. */
    CFRunLoopSourceRef source;

    /*! Mach port used to receive notifications. */
    CFMachPortRef  notificationPort;

    /*! Callback that will handle kernel notifications. */
    iSCSIHBANotificationCallBack callback;

    /*! Notification data used when invoking the callback. */
    struct __iSCSIHBANotificationContext notifyContext;

    /*! DEXT mode support — when true, bypass kext and use TCP+DextIPC. */
    bool dextMode;

    /*! Opaque DextIPCRef for communicating with the DEXT. */
    void * dextIPC;

    /*! kqueue fd from the TCP engine for async I/O registration. */
    int tcpEngineKq;

    /*! Number of active DEXT-mode sessions. */
    int numDextSessions;

    /*! Session tracking table for DEXT mode. */
    DextSessionEntry dextSessions[kDEXTMaxSessions];
};

#pragma mark - DEXT Helper Functions

/*! Finds a DextSessionEntry by DEXT session ID. */
static DextSessionEntry * dextFindSession(iSCSIHBAInterfaceRef interface, uint8_t dextId) {
    for (int i = 0; i < kDEXTMaxSessions; i++) {
        if (interface->dextSessions[i].inUse && interface->dextSessions[i].dextSessionId == dextId)
            return &interface->dextSessions[i];
    }
    return NULL;
}

/*! Allocates a new DextSessionEntry. */
static DextSessionEntry * dextAllocSession(iSCSIHBAInterfaceRef interface) {
    for (int i = 0; i < kDEXTMaxSessions; i++) {
        if (!interface->dextSessions[i].inUse) {
            memset(&interface->dextSessions[i], 0, sizeof(DextSessionEntry));
            interface->dextSessions[i].inUse = true;
            interface->dextSessions[i].dextSessionId = (uint8_t)i;
            interface->dextSessions[i].tcpSocket = -1;
            for (int j = 0; j < kDEXTMaxConnectionsPerSession; j++)
                interface->dextSessions[i].connectionSockets[j] = -1;
            interface->numDextSessions++;
            return &interface->dextSessions[i];
        }
    }
    return NULL;
}

/*! Frees a DextSessionEntry and closes its TCP sockets. */
static void dextFreeSession(iSCSIHBAInterfaceRef interface, uint8_t dextId) {
    for (int i = 0; i < kDEXTMaxSessions; i++) {
        if (interface->dextSessions[i].inUse && interface->dextSessions[i].dextSessionId == dextId) {
            if (interface->dextSessions[i].tcpSocket >= 0)
                close(interface->dextSessions[i].tcpSocket);
            for (int j = 0; j < kDEXTMaxConnectionsPerSession; j++) {
                if (interface->dextSessions[i].connectionSockets[j] >= 0)
                    close(interface->dextSessions[i].connectionSockets[j]);
            }
            interface->dextSessions[i].inUse = false;
            interface->numDextSessions--;
            return;
        }
    }
}

/*! Gets the dext IPC from the interface (dextIPC field is stored as void*). */
static inline DextIPCRef dextGetIPC(iSCSIHBAInterfaceRef interface) {
    return (DextIPCRef)interface->dextIPC;
}

/*! Handles messages sent from the HBA. This is an internal handler that is called first
 *  to adhere to the required Mach callback prototype. The info parameter contains
 *  information about an iSCSIHBAInterface instance (which includes user-defined data). */
static void iSCSIHBANotificationHandler(CFMachPortRef port,void * msg,CFIndex size,void * info)
{
    // The parameter is a notification message
    iSCSIHBANotificationMessage * notificationMsg;
    if(!(notificationMsg = msg))
        return;

    // Process notification type and return if invalid
    enum iSCSIHBANotificationTypes type = (enum iSCSIHBANotificationTypes)notificationMsg->notificationType;
    
    if(type == kiSCSIHBANotificationInvalid)
        return;
    
    // Call the callback function with the message type and body
    iSCSIHBAInterfaceRef interface = (iSCSIHBAInterfaceRef)info;
    if(interface->callback)
        interface->callback(interface,type,msg,interface->notifyContext.info);
}

/*! Schedules execution of various tasks, including handling of kernel notifications
 *  on for the specified interface instance over the designated runloop.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param runLoop the runloop to schedule
 *  @param runLoopMode the execution mode for the runLoop. */
void iSCSIHBAInterfaceScheduleWithRunloop(iSCSIHBAInterfaceRef interface,
                                          CFRunLoopRef runLoop,
                                          CFStringRef runLoopMode)
{
    // DEXT mode: no kext notification Mach port to schedule
    if(interface->dextMode)
        return;

    if(interface->notificationPort && interface->source == NULL) {
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,interface->notificationPort,0);
        CFRunLoopAddSource(runLoop,source,runLoopMode);
        interface->source = source;
    }
}

/*! Unschedules execution of various tasks, including handling of kernel notifications
 *  on for the specified interface instance over the designated runloop.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param runLoop the runloop to schedule
 *  @param runLoopMode the execution mode for the runLoop. */
void iSCSIHBAInterfaceUnscheduleWithRunloop(iSCSIHBAInterfaceRef interface,
                                            CFRunLoopRef runLoop,
                                            CFStringRef runLoopMode)
{
    // DEXT mode: no kext notification Mach port to unschedule
    if(interface->dextMode)
        return;

    if(interface->notificationPort) {
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,interface->notificationPort,0);
        CFRunLoopRemoveSource(runLoop,source,runLoopMode);
    }
}

/*! Opens a connection to the iSCSI initiator.  A connection must be
 *  successfully opened before any of the supporting functions below can be
 *  called. */
iSCSIHBAInterfaceRef iSCSIHBAInterfaceCreate(CFAllocatorRef allocator,iSCSIHBANotificationCallBack callback,iSCSIHBANotificationContext * context)
{
    io_service_t service = IO_OBJECT_NULL;
    io_connect_t connect = IO_OBJECT_NULL;
    kern_return_t result = kIOReturnSuccess;
    CFMachPortRef notificationPort = NULL;
    
    iSCSIHBAInterfaceRef interface = CFAllocatorAllocate(allocator,sizeof(struct __iSCSIHBAInterface),0);
    if(!interface)
        return NULL;

    // Zero-initialize all fields (dextMode, dextIPC, etc.)
    // Without this, dextMode could be garbage and cause the DEXT path
    // to be taken even when no DEXT is loaded.
    memset(interface, 0, sizeof(struct __iSCSIHBAInterface));

	// Create a dictionary to match iSCSIkext
	CFMutableDictionaryRef matchingDict = NULL;
	matchingDict = IOServiceMatching(kiSCSIVirtualHBA_IOClassName);
    
    service = IOServiceGetMatchingService(kIOMasterPortDefault,matchingDict);
    
	// Check to see if the driver was found in the I/O registry
    // and open a connection to it.
    if(service != IO_OBJECT_NULL) {
        result = IOServiceOpen(service,mach_task_self(),0,&connect);
    }

	if(result == kIOReturnSuccess)
        result = IOConnectCallScalarMethod(connect,kiSCSIOpenInitiator,0,0,0,0);
    
    if(result == kIOReturnSuccess) {
        
        CFMachPortContext notificationContext;
        notificationContext.version = 0;
        notificationContext.info = (void *)interface;
        notificationContext.release = 0;
        notificationContext.retain  = 0;
        notificationContext.copyDescription = NULL;
        
        // Create a mach port to receive notifications from the kernel
        notificationPort = CFMachPortCreate(allocator,iSCSIHBANotificationHandler,&notificationContext,NULL);
        result = IOConnectSetNotificationPort(connect,0,CFMachPortGetPort(notificationPort),0);
    }
    
    if(result == kIOReturnSuccess) {
        interface->allocator = allocator;
        interface->service = service;
        interface->connect = connect;
        interface->notificationPort = notificationPort;
        interface->callback = callback;
        interface->source = NULL;
        memcpy(&interface->notifyContext,context,sizeof(struct __iSCSIHBANotificationContext));
        
        // Retain user-defined data if a callback was provided
        // (this may be NULL in which case we are not responsible)
        if(interface->notifyContext.retain)
            interface->notifyContext.retain(interface->notifyContext.info);
    }
    // Cleanup
    else {
        if(notificationPort)
            CFRelease(notificationPort);
        if(connect != IO_OBJECT_NULL)
            IOObjectRelease(connect);
        if(service != IO_OBJECT_NULL)
            IOObjectRelease(service);
        
        CFAllocatorDeallocate(allocator,interface);
        interface = NULL;
    }
    return interface;
}

#pragma mark - DEXT Mode Lifecycle

/*! Opens a DEXT-mode connection (no kext required).
 *  Creates an HBA interface that works entirely in user space,
 *  using DextIPC to communicate with the DriverKit extension
 *  and direct TCP sockets for data I/O. */
iSCSIHBAInterfaceRef iSCSIHBAInterfaceCreateDext(CFAllocatorRef allocator)
{
    iSCSIHBAInterfaceRef interface = CFAllocatorAllocate(allocator,sizeof(struct __iSCSIHBAInterface),0);
    if (!interface) return NULL;

    memset(interface, 0, sizeof(struct __iSCSIHBAInterface));
    interface->allocator     = allocator;
    interface->service       = IO_OBJECT_NULL;
    interface->connect       = IO_OBJECT_NULL;
    interface->source        = NULL;
    interface->notificationPort = NULL;
    interface->callback      = NULL;
    interface->dextMode      = true;
    interface->dextIPC       = NULL;
    interface->tcpEngineKq   = -1;
    interface->numDextSessions = 0;

    return interface;
}

/*! Enables DEXT mode on an existing HBA interface.
 *  Must be called after the interface is created and a DextIPC channel
 *  is available. After this call, session operations bypass the kext
 *  and use user-space TCP sockets + DextIPC instead. */
void iSCSIHBAInterfaceSetDextMode(iSCSIHBAInterfaceRef interface,
                                    void * dextIPC,
                                    int tcpEngineKq)
{
    if (!interface) return;
    interface->dextMode    = true;
    interface->dextIPC     = dextIPC;
    interface->tcpEngineKq = tcpEngineKq;
}

/*! Returns whether this HBA interface is operating in DEXT mode. */
bool iSCSIHBAInterfaceIsDextMode(iSCSIHBAInterfaceRef interface)
{
    return interface ? interface->dextMode : false;
}

/*! Closes a connection to the iSCSI initiator. */
void iSCSIHBAInterfaceRelease(iSCSIHBAInterfaceRef interface)
{
    if(!interface)
        return;

    // DEXT mode: cleanup DEXT sessions and release IPC, skip kext ops
    if(interface->dextMode) {
        for(int i = 0; i < kDEXTMaxSessions; i++) {
            if(interface->dextSessions[i].inUse) {
                // Phase E: free TCP engine context if session was activated
                if(interface->dextSessions[i].tcpCtx) {
                    TCPEngineCloseConnection(interface->dextSessions[i].tcpCtx);
                    free(interface->dextSessions[i].tcpCtx);
                    interface->dextSessions[i].tcpCtx = NULL;
                }
                if(interface->dextSessions[i].tcpSocket >= 0)
                    close(interface->dextSessions[i].tcpSocket);
                for(int j = 0; j < kDEXTMaxConnectionsPerSession; j++) {
                    if(interface->dextSessions[i].connectionSockets[j] >= 0)
                        close(interface->dextSessions[i].connectionSockets[j]);
                }
            }
        }
        if(interface->dextIPC) {
            DextIPCRelease(dextGetIPC(interface));
            interface->dextIPC = NULL;
        }
        interface->numDextSessions = 0;
        CFAllocatorDeallocate(interface->allocator,interface);
        return;
    }

    // Release runloop source is one exists
    if(interface->source)
        CFRelease(interface->source);

    // Close connection to the driver
    IOConnectCallScalarMethod(interface->connect,kiSCSICloseInitiator,0,0,0,0);

    // Clean up (now that we have a connection we no longer need the object)
    IOServiceClose(interface->connect);

    // Stop receiving HBA notifications
    if(interface->notificationPort) {
        CFRelease(interface->notificationPort);
    }

    // Release user-defined data if a callback was provided
    // (this may be NULL in which case we are not responsible)
    if(interface->notifyContext.release)
        interface->notifyContext.release(interface->notifyContext.info);

    CFAllocatorDeallocate(interface->allocator,interface);
}

/*! Allocates a new iSCSI session in the kernel and creates an associated
 *  connection to the target portal. Additional connections may be added to the
 *  session by calling iSCSIHBAInterfaceCreateConnection().
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param targetIQN the name of the target.
 *  @param portalAddress the portal address (IPv4/IPv6, or DNS name).
 *  @param portalPort the TCP port used to connect to the portal.
 *  @param hostInterface the name of the host interface adapter to use.
 *  @param portalSockaddr the BSD socket structure used to identify the target.
 *  @param hostSockaddr the BSD socket structure used to identify the host. This
 *  specifies the interface that the connection will be bound to.
 *  @param sessionId the session identifier for the new session (returned).
 *  @param connectionId the identifier of the new connection (returned).
 *  @return An error code if a valid session could not be created. */
IOReturn iSCSIHBAInterfaceCreateSession(iSCSIHBAInterfaceRef interface,
                                        CFStringRef targetIQN,
                                        CFStringRef portalAddress,
                                        CFStringRef portalPort,
                                        CFStringRef hostInterface,
                                        const struct sockaddr_storage * remoteAddress,
                                        const struct sockaddr_storage * localAddress,
                                        SessionIdentifier * sessionId,
                                        ConnectionIdentifier * connectionId)
{
    // Check parameters
    if(!interface || !portalAddress || !portalPort || !hostInterface || !remoteAddress ||
       !localAddress || !sessionId || !connectionId)
        return kIOReturnBadArgument;

    // DEXT mode: create TCP socket and connect directly
    if(interface->dextMode) {
        int sock = (int)socket(remoteAddress->ss_family, SOCK_STREAM, 0);
        if(sock < 0)
            return kIOReturnIOError;

        // Set TCP_NODELAY to avoid Nagle delay during login
        int optval = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

        // Optional: bind to local address if specified
        if(localAddress && localAddress->ss_family != AF_UNSPEC) {
            struct sockaddr *localSA = (struct sockaddr *)localAddress;
            bind(sock, localSA, localAddress->ss_len > 0 ? localAddress->ss_len : sizeof(struct sockaddr_storage));
        }

        // Connect to target
        struct sockaddr *remoteSA = (struct sockaddr *)remoteAddress;
        socklen_t remoteLen = remoteAddress->ss_len > 0 ? remoteAddress->ss_len : sizeof(struct sockaddr_storage);
        if(connect(sock, remoteSA, remoteLen) < 0) {
            close(sock);
            return kIOReturnIOError;
        }

        // Allocate DEXT session slot
        DextSessionEntry *entry = dextAllocSession(interface);
        if(!entry) {
            close(sock);
            return kIOReturnNoMemory;
        }

        // Store connection info in session entry
        CFStringGetCString(targetIQN, entry->targetIQN, sizeof(entry->targetIQN), kCFStringEncodingASCII);
        CFStringGetCString(portalAddress, entry->portalAddress, sizeof(entry->portalAddress), kCFStringEncodingASCII);
        CFStringGetCString(portalPort, entry->portalPort, sizeof(entry->portalPort), kCFStringEncodingASCII);
        CFStringGetCString(hostInterface, entry->hostInterface, sizeof(entry->hostInterface), kCFStringEncodingASCII);

        // Store TCP socket as the first (primary) connection
        entry->tcpSocket = sock;
        entry->connectionCount = 1;
        entry->connectionIds[0] = 1;               // primary connection ID = 1
        entry->connectionSockets[0] = sock;

        *sessionId    = dextMakeSessionId(entry->dextSessionId);
        *connectionId = 1;

        return kIOReturnSuccess;
    }

    // Pack the input parameters into a single buffer to send to the kernel
    const int kNumParams = 6;
    void * params[kNumParams];
    size_t paramSize[kNumParams];
    
    // Add one for string lengths to copy the NULL character (CFGetStringLength
    // does not include the length of the NULL terminator)
    paramSize[0] = CFStringGetLength(targetIQN) + 1;
    paramSize[1] = CFStringGetLength(portalAddress) + 1;
    paramSize[2] = CFStringGetLength(portalPort) + 1;
    paramSize[3] = CFStringGetLength(hostInterface) + 1;
    paramSize[4] = sizeof(struct sockaddr_storage);
    paramSize[5] = sizeof(struct sockaddr_storage);
    
    // Populate parameters
    params[0] = malloc(paramSize[0]);
    params[1] = malloc(paramSize[1]);
    params[2] = malloc(paramSize[2]);
    params[3] = malloc(paramSize[3]);
    params[4] = (void*)remoteAddress;
    params[5] = (void*)localAddress;
    
    CFStringGetCString(targetIQN,params[0],paramSize[0],kCFStringEncodingASCII);
    CFStringGetCString(portalAddress,params[1],paramSize[1],kCFStringEncodingASCII);
    CFStringGetCString(portalPort,params[2],paramSize[2],kCFStringEncodingASCII);
    CFStringGetCString(hostInterface,params[3],paramSize[3],kCFStringEncodingASCII);
    
    // The input buffer will first have eight bytes to denote the length of
    // the portion that follows.  So for each of the six input parameters,
    // we'll have six UInt64 blocks that indicate the size up front.
    size_t header = kNumParams*sizeof(UInt64);
    size_t inputStructSize = header;
    
    CFIndex paramIdx = 0;
    while(paramIdx < kNumParams) {
        inputStructSize += paramSize[paramIdx];
        paramIdx++;
    }
    
    UInt8 * inputStruct = (UInt8*)malloc(inputStructSize);
    UInt8 * inputStructPos = inputStruct + header;

    paramIdx = 0;
    while(paramIdx < kNumParams) {
        memcpy(inputStructPos,params[paramIdx],paramSize[paramIdx]);
        inputStructPos += paramSize[paramIdx];
        
        UInt64 * header = (UInt64*)(inputStruct + sizeof(UInt64)*paramIdx);
        *header = paramSize[paramIdx];
        paramIdx++;
    }
    
    const UInt32 inputCnt = 1;
    UInt64 inputs[inputCnt];
    inputs[0] = kNumParams;
    
    const UInt32 expOutputCnt = 3;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;
    
    kern_return_t result =
        IOConnectCallMethod(interface->connect,kiSCSICreateSession,inputs,inputCnt,
                            inputStruct,inputStructSize,output,&outputCnt,0,0);
    
    // Free allocated memory
    free(params[0]);
    free(params[1]);
    free(params[2]);
    free(params[3]);
    free(inputStruct);
    
    if(result == kIOReturnSuccess && outputCnt == expOutputCnt) {
        *sessionId    = (UInt16)output[0];
        *connectionId = (UInt32)output[1];
        
        IOReturn error = (IOReturn)output[2];
        return error;
    }
    
    return result;
}

/*! Releases an iSCSI session, including all connections associated with that
 *  session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the session qualifier part of the ISID.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceReleaseSession(iSCSIHBAInterfaceRef interface,
                                         SessionIdentifier sessionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId)
        return kIOReturnBadArgument;

    // DEXT mode: close TCP socket and free session entry
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        // Unregister from DEXT if IPC is connected
        if(interface->dextIPC) {
            DextIPCUnregisterSession(dextGetIPC(interface), dextId);
        }

        // Phase E: Clean up PDURelay + TCPEngine if session was activated.
        // This must happen before raw socket close because TCPEngineCloseConnection
        // closes the socket fd and frees its buffers.
        if(entry->tcpCtx != NULL) {
            PDURelayUnregisterSession(dextId);
            TCPEngineRemoveSocket(interface->tcpEngineKq, entry->tcpCtx->fd);
            TCPEngineCloseConnection(entry->tcpCtx);
            free(entry->tcpCtx);
            entry->tcpCtx = NULL;
            // Mark fds as closed (TCPEngineCloseConnection already closed them)
            entry->tcpSocket = -1;
            for(int j = 0; j < kDEXTMaxConnectionsPerSession; j++)
                entry->connectionSockets[j] = -1;
        }
        else {
            // Clean up TCP sockets directly (session was never activated)
            if(entry->tcpSocket >= 0)
                close(entry->tcpSocket);
            for(int j = 0; j < kDEXTMaxConnectionsPerSession; j++) {
                if(entry->connectionSockets[j] >= 0) {
                    close(entry->connectionSockets[j]);
                    entry->connectionSockets[j] = -1;
                }
            }
            entry->tcpSocket = -1;
        }

        dextFreeSession(interface, dextId);
        return kIOReturnSuccess;
    }

    // Tell the kernel to drop this session and all of its related resources
    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    return IOConnectCallScalarMethod(interface->connect,kiSCSIReleaseSession,&input,inputCnt,0,0);
}

/*! Sets parameter associated with a particular session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param parameter the parameter to set.
 *  @param paramVal the value for the specified parameter.
 *  @param paramSize the size, in bytes of paramVal.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceSetSessionParameter(iSCSIHBAInterfaceRef interface,
                                              SessionIdentifier sessionId,
                                              enum iSCSIHBASessionParameters parameter,
                                              void * paramVal,
                                              size_t paramSize)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || !paramVal || paramSize == 0)
        return kIOReturnBadArgument;

    // DEXT mode: cache locally and forward to DEXT via IPC
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        UInt64 paramValCopy = 0;
        memcpy(&paramValCopy,paramVal,paramSize);

        // Cache locally
        if((int)parameter < kDEXTMaxCachedParams)
            entry->cachedSessionParams[parameter] = paramValCopy;

        // Forward to DEXT if IPC connected
        if(interface->dextIPC) {
            DextIPCSetSessionParam(dextGetIPC(interface), dextId,
                                   (enum DextSessionParam)parameter, paramValCopy);
        }
        return kIOReturnSuccess;
    }

    UInt64 paramValCopy = 0;
    memcpy(&paramValCopy,paramVal,paramSize);

    const UInt32 inputCnt = 3;
    const UInt64 input[] = {sessionId,parameter,paramValCopy};

    return IOConnectCallScalarMethod(interface->connect,kiSCSISetSessionParameter,input,inputCnt,0,0);
}

/*! Gets parameter associated with a particular session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param parameter the parameter to get.
 *  @param paramVal the returned value for the specified parameter.
 *  @param paramSize the size, in bytes of paramVal.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetSessionParameter(iSCSIHBAInterfaceRef interface,
                                              SessionIdentifier sessionId,
                                              enum iSCSIHBASessionParameters parameter,
                                              void * paramVal,
                                              size_t paramSize)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || !paramVal || paramSize == 0)
        return kIOReturnBadArgument;

    // DEXT mode: return from local cache
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        if((int)parameter < kDEXTMaxCachedParams) {
            memcpy(paramVal, &entry->cachedSessionParams[parameter], paramSize);
            return kIOReturnSuccess;
        }
        return kIOReturnBadArgument;
    }

    const UInt32 inputCnt = 2;
    const UInt64 input[] = {sessionId,parameter};

    UInt32 outputCnt = 1;
    UInt64 output;

    kern_return_t error = IOConnectCallScalarMethod(interface->connect,kiSCSIGetSessionParameter,
                                                    input,inputCnt,&output,&outputCnt);

    if(error == kIOReturnSuccess)
        memcpy(paramVal,&output,paramSize);

    return error;
}

/*! Allocates an additional iSCSI connection for a particular session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the session to create a new connection for.
 *  @param portalAddress the portal address (IPv4/IPv6, or DNS name).
 *  @param portalPort the TCP port used to connect to the portal.
 *  @param hostInterface the name of the host interface adapter to use.
 *  @param portalSockaddr the BSD socket structure used to identify the target.
 *  @param hostSockaddr the BSD socket structure used to identify the host. This
 *  specifies the interface that the connection will be bound to.
 *  @param connectionId the identifier of the new connection.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceCreateConnection(iSCSIHBAInterfaceRef interface,
                                           SessionIdentifier sessionId,
                                           CFStringRef portalAddress,
                                           CFStringRef portalPort,
                                           CFStringRef hostInterface,
                                           const struct sockaddr_storage * remoteAddress,
                                           const struct sockaddr_storage * localAddress,
                                           ConnectionIdentifier * connectionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || !portalAddress || !portalPort || !hostInterface || !remoteAddress || !connectionId)
        return kIOReturnBadArgument;

    // DEXT mode: create additional TCP connection for MC
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        int sock = (int)socket(remoteAddress->ss_family, SOCK_STREAM, 0);
        if(sock < 0)
            return kIOReturnIOError;

        int optval = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

        if(localAddress && localAddress->ss_family != AF_UNSPEC) {
            struct sockaddr *localSA = (struct sockaddr *)localAddress;
            bind(sock, localSA, localAddress->ss_len > 0 ? localAddress->ss_len : sizeof(struct sockaddr_storage));
        }

        struct sockaddr *remoteSA = (struct sockaddr *)remoteAddress;
        socklen_t remoteLen = remoteAddress->ss_len > 0 ? remoteAddress->ss_len : sizeof(struct sockaddr_storage);
        if(connect(sock, remoteSA, remoteLen) < 0) {
            close(sock);
            return kIOReturnIOError;
        }

        // Find a free connection slot
        uint32_t newConnId = entry->connectionCount + 1;
        if(entry->connectionCount >= kDEXTMaxConnectionsPerSession) {
            close(sock);
            return kIOReturnNoSpace;
        }
        uint32_t idx = entry->connectionCount;
        entry->connectionIds[idx] = newConnId;
        entry->connectionSockets[idx] = sock;
        entry->connectionCount++;

        *connectionId = newConnId;
        return kIOReturnSuccess;
    }

    // Pack the input parameters into a single buffer to send to the kernel
    const int kNumParams = 5;

    void * params[kNumParams];
    size_t paramSize[kNumParams];
    
    // Add one for string lengths to copy the NULL character (CFGetStringLength
    // does not include the length of the NULL terminator)
    paramSize[0] = CFStringGetLength(portalAddress) + 1;
    paramSize[1] = CFStringGetLength(portalPort) + 1;
    paramSize[2] = CFStringGetLength(hostInterface) + 1;
    paramSize[3] = sizeof(struct sockaddr_storage);
    paramSize[4] = sizeof(struct sockaddr_storage);
    
    params[0] = malloc(paramSize[0]);
    params[1] = malloc(paramSize[1]);
    params[2] = malloc(paramSize[2]);
    params[3] = (void*)remoteAddress;
    params[4] = (void*)localAddress;
    
    CFStringGetCString(portalAddress,params[0],paramSize[0],kCFStringEncodingASCII);
    CFStringGetCString(portalPort,params[1],paramSize[1],kCFStringEncodingASCII);
    CFStringGetCString(hostInterface,params[2],paramSize[2],kCFStringEncodingASCII);
    
    // The input buffer will first have eight bytes to denote the length of
    // the portion that follows.  So for each of the six input parameters,
    // we'll have six UInt64 blocks that indicate the size up front.
    size_t header = kNumParams*sizeof(UInt64);
    size_t inputStructSize = header;
    
    CFIndex paramIdx = 0;
    while(paramIdx < kNumParams) {
        inputStructSize += paramSize[paramIdx];
        paramIdx++;
    }
    
    UInt8 * inputStruct = (UInt8*)malloc(inputStructSize);
    UInt8 * inputStructPos = inputStruct + header;
    
    paramIdx = 0;
    while(paramIdx < kNumParams) {
        memcpy(inputStructPos,params[paramIdx],paramSize[paramIdx]);
        inputStructPos += paramSize[paramIdx];
        
        UInt64 * header = (UInt64*)(inputStruct + sizeof(UInt64)*paramIdx);
        *header = paramSize[paramIdx];
        paramIdx++;
    }
    
    // Tell the kernel to drop this session and all of its related resources
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId,kNumParams};
    
    const UInt32 expOutputCnt = 2;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;
    
    kern_return_t result =
        IOConnectCallMethod(interface->connect,kiSCSICreateConnection,inputs,inputCnt,inputStruct,
                            inputStructSize,output,&outputCnt,0,0);
    
    // Free memory
    free(params[0]);
    free(params[1]);
    free(params[2]);
    
    if(result == kIOReturnSuccess && outputCnt == expOutputCnt) {
        *connectionId = (UInt32)output[0];
        IOReturn error = (IOReturn)output[1];
        
        return error;
    }

    return result;
}

/*! Frees a given iSCSI connection associated with a given session.
 *  The session should be logged out using the appropriate PDUs.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceReleaseConnection(iSCSIHBAInterfaceRef interface,
                                            SessionIdentifier sessionId,
                                            ConnectionIdentifier connectionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return kIOReturnBadArgument;

    // DEXT mode: close specific connection TCP socket
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        int sock = dextConnectionSocket(entry, connectionId);
        if(sock >= 0) {
            close(sock);
            // Clear the slot
            for(uint32_t i = 0; i < entry->connectionCount; i++) {
                if(entry->connectionIds[i] == connectionId) {
                    entry->connectionIds[i] = 0;
                    entry->connectionSockets[i] = -1;
                    break;
                }
            }
        }
        return kIOReturnSuccess;
    }

    // Tell kernel to drop this connection
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};

    return IOConnectCallScalarMethod(interface->connect,kiSCSIReleaseConnection,inputs,inputCnt,0,0);
}

/*! Sends data over a kernel socket associated with iSCSI.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param bhs the basic header segment to send over the connection.
 *  @param data the data segment of the PDU to send over the connection.
 *  @param length the length of the data block to send over the connection.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceSend(iSCSIHBAInterfaceRef interface,
                               SessionIdentifier sessionId,
                               ConnectionIdentifier connectionId,
                               iSCSIPDUInitiatorBHS * bhs,
                               void * data,
                               size_t length)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId || !bhs || (!data && length > 0))
        return kIOReturnBadArgument;

    // Set data segment length in the BHS (3-byte big-endian field)
    // Without this, targets see dataSegmentLength=0 and reject the PDU
    // with "Missing login parameters" because they ignore the data segment.
    bhs->dataSegmentLength[0] = (UInt8)((length >> 16) & 0xFF);
    bhs->dataSegmentLength[1] = (UInt8)((length >> 8) & 0xFF);
    bhs->dataSegmentLength[2] = (UInt8)(length & 0xFF);

    // DEXT mode: send BHS + data directly via TCP
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;
        int sock = dextConnectionSocket(entry, connectionId);
        if(sock < 0)
            return kIOReturnNotFound;

        ssize_t sent = send(sock, bhs, sizeof(iSCSIPDUInitiatorBHS), 0);
        if(sent != (ssize_t)sizeof(iSCSIPDUInitiatorBHS))
            return kIOReturnIOError;

        if(length > 0 && data) {
            // RFC 3720: data segment MUST be padded to 4-byte boundary
            size_t paddedLen = ((length + 3) / 4) * 4;
            if(paddedLen > length) {
                uint8_t padBytes[4] = {0, 0, 0, 0};
                struct iovec iov[2];
                iov[0].iov_base = data;
                iov[0].iov_len = length;
                iov[1].iov_base = padBytes;
                iov[1].iov_len = paddedLen - length;
                struct msghdr msg = {0};
                msg.msg_iov = iov;
                msg.msg_iovlen = 2;
                sent = sendmsg(sock, &msg, 0);
                if(sent != (ssize_t)paddedLen)
                    return kIOReturnIOError;
            } else {
                sent = send(sock, data, length, 0);
                if(sent != (ssize_t)length)
                    return kIOReturnIOError;
            }
        }
        return kIOReturnSuccess;
    }

    // Setup input scalar array
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId, connectionId};

    // Call kernel method to send (buffer) bhs and then data
    kern_return_t result;
    result = IOConnectCallStructMethod(interface->connect,kiSCSISendBHS,bhs,
                                       sizeof(iSCSIPDUInitiatorBHS),NULL,NULL);

    if(result != kIOReturnSuccess) {
        return result;
    }

    result = IOConnectCallMethod(interface->connect,kiSCSISendData,inputs,inputCnt,
                                               data,length,NULL,NULL,NULL,NULL);
    return result;
}

/*! Receives data over a kernel socket associated with iSCSI.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param bhs the basic header segment received over the connection.
 *  @param data the data segment of the PDU received over the connection.
 *  @param length the length of the data block received.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceReceive(iSCSIHBAInterfaceRef interface,
                                  SessionIdentifier sessionId,
                                  ConnectionIdentifier connectionId,
                                  iSCSIPDUTargetBHS * bhs,
                                  void ** data,
                                  size_t * length)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId || !bhs)
        return kIOReturnBadArgument;

    // DEXT mode: receive BHS + data directly via TCP
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry) {
            return kIOReturnBadArgument;
        }
        int sock = dextConnectionSocket(entry, connectionId);
        if(sock < 0) {
            return kIOReturnNotFound;
        }

        // Set a receive timeout for login phase (30 seconds)
        struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Receive BHS (use MSG_WAITALL to ensure full 48-byte read)
        size_t bhsLen = sizeof(iSCSIPDUTargetBHS);
        ssize_t received = recv(sock, bhs, bhsLen, MSG_WAITALL);
        if(received != (ssize_t)bhsLen) {
            return kIOReturnIOError;
        }

        // Calculate data segment length from BHS
        *length = iSCSIPDUGetDataSegmentLength((iSCSIPDUCommonBHS *)bhs);

        // If no data, done
        if(*length == 0) {
            *data = NULL;
            return kIOReturnSuccess;
        }

        // RFC 3720: data segment is padded to 4-byte boundary.
        // We must read the padded bytes to consume them, otherwise
        // leftover padding corrupts the next PDU's BHS read.
        size_t paddedLen = ((*length + 3) / 4) * 4;

        // Allocate and receive padded data segment
        // iSCSIPDUDataCreate already rounds up to 4-byte alignment
        *data = iSCSIPDUDataCreate(*length);
        if(!*data) {
            return kIOReturnIOError;
        }

        received = recv(sock, *data, paddedLen, MSG_WAITALL);
        if(received != (ssize_t)paddedLen) {
            iSCSIPDUDataRelease(data);
            return kIOReturnIOError;
        }
        return kIOReturnSuccess;
    }

    // KEXT path
    // Setup input scalar array
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};

    size_t bhsLength = sizeof(iSCSIPDUTargetBHS);

    // Call kernel method to determine how much data there is to receive
    // The inputs are the sesssion qualifier and connection ID
    // The output is the size of the buffer we need to allocate to hold the data
    kern_return_t result;
    result = IOConnectCallMethod(interface->connect,kiSCSIRecvBHS,inputs,inputCnt,NULL,0,
                                 NULL,NULL,bhs,&bhsLength);

    if(result != kIOReturnSuccess) {
        return result;
    }

    // Determine how much data to allocate for the data buffer
    *length = iSCSIPDUGetDataSegmentLength((iSCSIPDUCommonBHS *)bhs);

    // If no data, were done at this point
    if(*length == 0)
        return 0;

    *data = iSCSIPDUDataCreate(*length);

    if(*data == NULL)
        return kIOReturnIOError;

    // Call kernel method to get data from a receive buffer
    result = IOConnectCallMethod(interface->connect,kiSCSIRecvData,inputs,inputCnt,NULL,0,
                                 NULL,NULL,*data,length);

    if(result != kIOReturnSuccess) {
        iSCSIPDUDataRelease(data);
    }
    
    return result;
}

/*! Sets parameter associated with a particular connection.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param parameter the parameter to set.
 *  @param paramVal the value for the specified parameter.
 *  @param paramSize the size, in bytes of paramVal.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceSetConnectionParameter(iSCSIHBAInterfaceRef interface,
                                                 SessionIdentifier sessionId,
                                                 ConnectionIdentifier connectionId,
                                                 enum iSCSIHBAConnectionParameters parameter,
                                                 void * paramVal,
                                                 size_t paramSize)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId || !paramVal || paramSize == 0)
        return kIOReturnBadArgument;

    // DEXT mode: cache locally and forward to DEXT via IPC
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        UInt64 paramValCopy = 0;
        memcpy(&paramValCopy,paramVal,paramSize);

        // Cache locally
        uint32_t connIdx = (uint32_t)(connectionId - 1);
        if((int)parameter < kDEXTMaxCachedParams && connIdx < kDEXTMaxConnectionsPerSession)
            entry->cachedConnectionParams[connIdx][parameter] = paramValCopy;

        // Forward to DEXT if IPC connected
        if(interface->dextIPC) {
            DextIPCSetConnectionParam(dextGetIPC(interface), dextId,
                                      (uint32_t)connectionId,
                                      (enum DextConnectionParam)parameter, paramValCopy);
        }
        return kIOReturnSuccess;
    }

    UInt64 paramValCopy = 0;
    memcpy(&paramValCopy,paramVal,paramSize);

    const UInt32 inputCnt = 4;
    const UInt64 inputs[] = {sessionId,connectionId,parameter,paramValCopy};

    return IOConnectCallScalarMethod(interface->connect,kiSCSISetConnectionParameter,inputs,inputCnt,0,0);
}

/*! Gets parameter associated with a particular connection.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param parameter the parameter to get.
 *  @param paramVal the returned value for the specified parameter.
 *  @param paramSize the size, in bytes of paramVal.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetConnectionParameter(iSCSIHBAInterfaceRef interface,
                                                 SessionIdentifier sessionId,
                                                 ConnectionIdentifier connectionId,
                                                 enum iSCSIHBAConnectionParameters parameter,
                                                 void * paramVal,
                                                 size_t paramSize)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId  == kiSCSIInvalidConnectionId || !paramVal || paramSize == 0)
        return kIOReturnBadArgument;

    // DEXT mode: return from local cache
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        uint32_t connIdx = (uint32_t)(connectionId - 1);
        if((int)parameter < kDEXTMaxCachedParams && connIdx < kDEXTMaxConnectionsPerSession) {
            memcpy(paramVal, &entry->cachedConnectionParams[connIdx][parameter], paramSize);
            return kIOReturnSuccess;
        }
        return kIOReturnBadArgument;
    }

    const UInt32 inputCnt = 3;
    const UInt64 input[] = {sessionId,connectionId,parameter};

    UInt32 outputCnt = 1;
    UInt64 output;

    kern_return_t error = IOConnectCallScalarMethod(interface->connect,kiSCSIGetConnectionParameter,
                                                    input,inputCnt,&output,&outputCnt);

    if(error == kIOReturnSuccess)
        memcpy(paramVal,&output,paramSize);

    return error;
}

/*! Activates an iSCSI connection associated with a session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session associated with connection to activate.
 *  @param connectionId  connection to activate.
 *  @return error code inidicating result of operation. */
IOReturn iSCSIHBAInterfaceActivateConnection(iSCSIHBAInterfaceRef interface,
                                             SessionIdentifier sessionId,
                                             ConnectionIdentifier connectionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return kIOReturnBadArgument;

    // DEXT mode: register session and connection with DEXT
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;

        if(!interface->dextIPC)
            return kIOReturnNotReady;

        DextIPCRef ipc = dextGetIPC(interface);

        // Register session with DEXT (filled from cached negotiated params)
        DextSessionConfig sessCfg;
        memset(&sessCfg, 0, sizeof(sessCfg));
        sessCfg.sessionId = dextId;
        sessCfg.targetId = 0;

        // Read the initial CmdSN from login-response cached parameter
        // (stored by iSCSINegotiateSession from the target's expCmdSN).
        uint32_t initialCmdSN = 0;
        iSCSIHBAInterfaceGetSessionParameter(interface, sessionId,
            kiSCSIHBASOInitialCmdSN, &initialCmdSN, sizeof(initialCmdSN));
        sessCfg.initialCmdSN = initialCmdSN;

        strncpy(sessCfg.targetName, entry->targetIQN, sizeof(sessCfg.targetName) - 1);
        // Other session params (burst lengths, etc.) are set via DextIPCSetSessionParam
        // during the negotiation phase before ActivateConnection.
        DextIPCRegisterSession(ipc, &sessCfg);

        // Activate the connection
        DextConnectionConfig connCfg;
        memset(&connCfg, 0, sizeof(connCfg));
        connCfg.sessionId = dextId;
        connCfg.connectionId = (uint32_t)connectionId;
        // initialExpStatSN, digest settings are set via DextIPCSetConnectionParam
        // during negotiation before activation.
        DextIPCActivateConnection(ipc, &connCfg);

        // Phase E: Register TCP socket with kqueue and PDURelay for data path.
        // After this point, SCSI PDUs are routed between DEXT ↔ TCP engine.
        int connSock = dextConnectionSocket(entry, connectionId);
        if(connSock < 0)
            connSock = entry->tcpSocket;

        if(connSock >= 0) {
            // Socket must be non-blocking for kqueue edge-triggered async I/O
            int flags = fcntl(connSock, F_GETFL, 0);
            if(flags != -1)
                fcntl(connSock, F_SETFL, flags | O_NONBLOCK);

            // Allocate and initialize TCP connection context for PDURelay
            TCPConnectionContext * ctx = (TCPConnectionContext *)calloc(1, sizeof(TCPConnectionContext));
            if(ctx) {
                errno_t err = TCPEngineAddSocket(interface->tcpEngineKq, connSock,
                                                  dextId, (uint32_t)connectionId, ctx);
                if(err == 0) {
                    ctx->connected = true;   // Already connected from login phase

                    // Register with PDU relay for TCP→DEXT data path routing
                    PDURelayRegisterSession(dextId, ctx);

                    // Read digest negotiation results from cached connection params.
                    // These were stored by iSCSIHBAInterfaceSetConnectionParameter
                    // during login negotiation before ActivateConnection.
                    uint32_t connIdx = 0;
                    for(uint32_t i = 0; i < entry->connectionCount; i++) {
                        if(entry->connectionIds[i] == (uint32_t)connectionId) {
                            connIdx = i;
                            break;
                        }
                    }
                    bool useHdrDig = (entry->cachedConnectionParams[connIdx][kiSCSIHBACOUseHeaderDigest] != 0);
                    bool useDatDig = (entry->cachedConnectionParams[connIdx][kiSCSIHBACOUseDataDigest] != 0);
                    PDURelaySetDigestFlags(dextId, useHdrDig, useDatDig);

                    // Switch session to full-feature phase forwarding
                    PDURelayActivateSession(dextId);

                    entry->tcpCtx = ctx;
                } else {
                    free(ctx);
                }
            }
        }

        return kIOReturnSuccess;
    }

    // Tell kernel to drop this connection
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};

    return IOConnectCallScalarMethod(interface->connect,kiSCSIActivateConnection,
                                                     inputs,inputCnt,NULL,NULL);
}

/*! Activates all iSCSI connections associated with a session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session associated with connection to activate.
 *  @return error code inidicating result of operation. */
IOReturn iSCSIHBAInterfaceActivateAllConnections(iSCSIHBAInterfaceRef interface,
                                                 SessionIdentifier sessionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId)
        return kIOReturnBadArgument;

    // DEXT mode: register session and activate all connections
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;
        if(!interface->dextIPC)
            return kIOReturnNotReady;

        DextIPCRef ipc = dextGetIPC(interface);
        DextSessionConfig sessCfg;
        memset(&sessCfg, 0, sizeof(sessCfg));
        sessCfg.sessionId = dextId;
        sessCfg.targetId = 0;

        // Read the initial CmdSN from cached session parameter
        uint32_t initialCmdSN = 0;
        iSCSIHBAInterfaceGetSessionParameter(interface, sessionId,
            kiSCSIHBASOInitialCmdSN, &initialCmdSN, sizeof(initialCmdSN));
        sessCfg.initialCmdSN = initialCmdSN;

        strncpy(sessCfg.targetName, entry->targetIQN, sizeof(sessCfg.targetName) - 1);
        DextIPCRegisterSession(ipc, &sessCfg);

        for(uint32_t i = 0; i < entry->connectionCount; i++) {
            uint32_t connId = (uint32_t)entry->connectionIds[i];
            DextConnectionConfig connCfg;
            memset(&connCfg, 0, sizeof(connCfg));
            connCfg.sessionId = dextId;
            connCfg.connectionId = connId;
            DextIPCActivateConnection(ipc, &connCfg);

            // Phase E: Register TCP socket with kqueue and PDURelay for data path.
            int connSock = dextConnectionSocket(entry, connId);
            if(connSock < 0)
                connSock = entry->tcpSocket;

            if(connSock >= 0 && interface->tcpEngineKq >= 0) {
                int flags = fcntl(connSock, F_GETFL, 0);
                if(flags != -1)
                    fcntl(connSock, F_SETFL, flags | O_NONBLOCK);

                TCPConnectionContext * ctx = (TCPConnectionContext *)calloc(1, sizeof(TCPConnectionContext));
                if(ctx) {
                    errno_t err = TCPEngineAddSocket(interface->tcpEngineKq,
                                                     connSock, dextId, connId, ctx);
                    if(err == 0) {
                        ctx->connected = true;

                        PDURelayRegisterSession(dextId, ctx);

                        // Read digest negotiation results from cached connection params
                        bool useHdrDig = (entry->cachedConnectionParams[i][kiSCSIHBACOUseHeaderDigest] != 0);
                        bool useDatDig = (entry->cachedConnectionParams[i][kiSCSIHBACOUseDataDigest] != 0);
                        PDURelaySetDigestFlags(dextId, useHdrDig, useDatDig);

                        PDURelayActivateSession(dextId);

                        entry->tcpCtx = ctx;
                    } else {
                        free(ctx);
                    }
                }
            }
        }
        return kIOReturnSuccess;
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    return IOConnectCallScalarMethod(interface->connect,kiSCSIActivateAllConnections,
                                                     &input,inputCnt,NULL,NULL);
}

/*! Dectivates an iSCSI connection associated with a session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session associated with connection to deactivate.
 *  @param connectionId  connection to deactivate.
 *  @return error code inidicating result of operation. */
IOReturn iSCSIHBAInterfaceDeactivateConnection(iSCSIHBAInterfaceRef interface,
                                               SessionIdentifier sessionId,
                                               ConnectionIdentifier connectionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return kIOReturnBadArgument;

    // DEXT mode: deactivate via DEXT IPC
    if(interface->dextMode) {
        if(!interface->dextIPC)
            return kIOReturnNotReady;
        DextIPCRef ipc = dextGetIPC(interface);
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextIPCDeactivateConnection(ipc, dextId, (uint32_t)connectionId);
        return kIOReturnSuccess;
    }

    // Tell kernel to drop this connection
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};

    return IOConnectCallScalarMethod(interface->connect,kiSCSIDeactivateConnection,
                                                     inputs,inputCnt,NULL,NULL);
}

/*! Dectivates all iSCSI sessions associated with a session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session associated with connections to deactivate.
 *  @return error code inidicating result of operation. */
IOReturn iSCSIHBAInterfaceDeactivateAllConnections(iSCSIHBAInterfaceRef interface,
                                                   SessionIdentifier sessionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId)
        return kIOReturnBadArgument;

    // DEXT mode: deactivate all via DEXT IPC
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;
        if(!interface->dextIPC)
            return kIOReturnNotReady;

        DextIPCRef ipc = dextGetIPC(interface);
        for(uint32_t i = 0; i < entry->connectionCount; i++) {
            DextIPCDeactivateConnection(ipc, dextId, (uint32_t)entry->connectionIds[i]);
        }
        return kIOReturnSuccess;
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    return IOConnectCallScalarMethod(interface->connect,kiSCSIDeactivateAllConnections,
                                                     &input,inputCnt,NULL,NULL);
}

/*! Gets the first connection (the lowest connectionId) for the
 *  specified session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId obtain an connectionId for this session.
 *  @param connectionId the identifier of the connection.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetConnection(iSCSIHBAInterfaceRef interface,
                                        SessionIdentifier sessionId,
                                        ConnectionIdentifier * connectionId)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || !connectionId)
        return kIOReturnBadArgument;

    // DEXT mode: return first connection from tracking
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry || entry->connectionCount == 0)
            return kIOReturnBadArgument;
        *connectionId = entry->connectionIds[0];
        return kIOReturnSuccess;
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    const UInt32 expOutputCnt = 1;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;

    kern_return_t result =
        IOConnectCallScalarMethod(interface->connect,kiSCSIGetConnection,&input,inputCnt,
                                  output,&outputCnt);

    if(result == kIOReturnSuccess && outputCnt == expOutputCnt)
        *connectionId = (UInt32)output[0];

    return result;
}

/*! Gets the connection count for the specified session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId obtain the connection count for this session.
 *  @param numConnections the connection count.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetNumConnections(iSCSIHBAInterfaceRef interface,
                                            SessionIdentifier sessionId,
                                            UInt32 * numConnections)
{
    // Check parameters
    if(!interface || sessionId == kiSCSIInvalidSessionId || !numConnections)
        return kIOReturnBadArgument;

    // DEXT mode: return from tracking
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;
        *numConnections = entry->connectionCount;
        return kIOReturnSuccess;
    }
    
    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;
    
    const UInt32 expOutputCnt = 1;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;
    
    kern_return_t result = IOConnectCallScalarMethod(
        interface->connect,kiSCSIGetNumConnections,&input,inputCnt,output,&outputCnt);
    
    if(result == kIOReturnSuccess && outputCnt == expOutputCnt)
        *numConnections = (UInt32)output[0];

    return result;
}

/*! Looks up the session identifier associated with a particular target name.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param targetIQN the IQN name of the target (e.q., iqn.2015-01.com.example)
 *  @return sessionId the session identifier. */
SessionIdentifier iSCSIHBAInterfaceGetSessionIdForTargetIQN(iSCSIHBAInterfaceRef interface,
                                                            CFStringRef targetIQN)
{
    if(!interface || !targetIQN)
        return kiSCSIInvalidSessionId;

    // DEXT mode: search local session table by target name
    if(interface->dextMode) {
        char nameBuf[256];
        if(!CFStringGetCString(targetIQN, nameBuf, sizeof(nameBuf), kCFStringEncodingASCII))
            return kiSCSIInvalidSessionId;
        for(int i = 0; i < kDEXTMaxSessions; i++) {
            if(interface->dextSessions[i].inUse &&
               strncmp(interface->dextSessions[i].targetIQN, nameBuf, sizeof(interface->dextSessions[i].targetIQN)) == 0) {
                return dextMakeSessionId(interface->dextSessions[i].dextSessionId);
            }
        }
        return kiSCSIInvalidSessionId;
    }

    const UInt32 expOutputCnt = 1;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;

    const int targetIQNBufferSize = (int)CFStringGetLength(targetIQN)+1;
    char * targetIQNBuffer = (char *)malloc(targetIQNBufferSize);
    if(!CFStringGetCString(targetIQN,targetIQNBuffer,targetIQNBufferSize,kCFStringEncodingASCII))
    {
        free(targetIQNBuffer);
        return kiSCSIInvalidSessionId;
    }

    kern_return_t result = IOConnectCallMethod(
        interface->connect,
        kiSCSIGetSessionIdForTargetIQN,0,0,
        targetIQNBuffer,
        targetIQNBufferSize,
        output,&outputCnt,0,0);

    free(targetIQNBuffer);

    if(result == kIOReturnSuccess && outputCnt == expOutputCnt)
        return (SessionIdentifier)output[0];

    return kiSCSIInvalidSessionId;
}

/*! Looks up the connection identifier associated with a particular portal address.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId the session identifier.
 *  @param portalAddress the address passed to iSCSIHBAInterfaceCreateSession() or
 *  iSCSIHBAInterfaceCreateConnection() when the connection was created.
 *  @return the associated connection identifier. */
ConnectionIdentifier iSCSIHBAInterfaceGetConnectionIdForPortalAddress(iSCSIHBAInterfaceRef interface,
                                                                      SessionIdentifier sessionId,
                                                                      CFStringRef portalAddress)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId || !portalAddress)
        return kIOReturnBadArgument;

    // DEXT mode: search session's connection for matching portal address
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kiSCSIInvalidConnectionId;
        char addrBuf[NI_MAXHOST];
        if(!CFStringGetCString(portalAddress, addrBuf, sizeof(addrBuf), kCFStringEncodingASCII))
            return kiSCSIInvalidConnectionId;
        if(strncmp(entry->portalAddress, addrBuf, sizeof(entry->portalAddress)) == 0 && entry->connectionCount > 0)
            return entry->connectionIds[0];
        return kiSCSIInvalidConnectionId;
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    const UInt32 expOutputCnt = 1;
    UInt64 output[expOutputCnt];
    UInt32 outputCnt = expOutputCnt;

    const int portalAddressBufferSize = (int)CFStringGetLength(portalAddress)+1;
    char * portalAddressBuffer = (char*)malloc(portalAddressBufferSize);
    if(!CFStringGetCString(portalAddress,portalAddressBuffer,portalAddressBufferSize,kCFStringEncodingASCII))
    {
        free(portalAddressBuffer);
        return kIOReturnBadArgument;
    }

    kern_return_t result =
        IOConnectCallMethod(interface->connect,kiSCSIGetConnectionIdForPortalAddress,
                            &input,inputCnt,
                            portalAddressBuffer,
                            portalAddressBufferSize,
                            output,&outputCnt,0,0);

    free(portalAddressBuffer);

    if(result != kIOReturnSuccess || outputCnt != expOutputCnt)
        return kiSCSIInvalidConnectionId;

    return (ConnectionIdentifier)output[0];
}

/*! Gets an array of session identifiers for each session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionIds an array of session identifiers.  This MUST be large
 *  enough to hold the maximum number of sessions (kiSCSIMaxSessions).
 *  @param sessionCount number of session identifiers.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetSessionIds(iSCSIHBAInterfaceRef interface,
                                        SessionIdentifier * sessionIds,
                                        UInt16 * sessionCount)
{
    if(!interface || !sessionIds || !sessionCount)
        return kIOReturnBadArgument;

    // DEXT mode: return from session tracking table
    if(interface->dextMode) {
        UInt16 cnt = 0;
        for(int i = 0; i < kDEXTMaxSessions && (int)cnt < kDEXTMaxSessions; i++) {
            if(interface->dextSessions[i].inUse) {
                sessionIds[cnt++] = dextMakeSessionId(interface->dextSessions[i].dextSessionId);
            }
        }
        *sessionCount = cnt;
        return kIOReturnSuccess;
    }

    const UInt32 expOutputCnt = 1;
    UInt64 output;
    UInt32 outputCnt = expOutputCnt;

    *sessionCount = 0;
    size_t outputStructSize = sizeof(SessionIdentifier)*kiSCSIMaxSessions;

    kern_return_t result =
        IOConnectCallMethod(interface->connect,kiSCSIGetSessionIds,0,0,0,0,
                            &output,&outputCnt,sessionIds,&outputStructSize);

    if(result == kIOReturnSuccess && outputCnt == expOutputCnt)
        *sessionCount = (UInt16)output;

    return result;
}

/*! Gets an array of connection identifiers for each session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session identifier.
 *  @param connectionIds an array of connection identifiers for the session.
 *  @param connectionCount number of connection identifiers.
 *  @return error code indicating result of operation. */
IOReturn iSCSIHBAInterfaceGetConnectionIds(iSCSIHBAInterfaceRef interface,
                                           SessionIdentifier sessionId,
                                           ConnectionIdentifier * connectionIds,
                                           UInt32 * connectionCount)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId || !connectionIds || !connectionCount)
        return kIOReturnBadArgument;

    // DEXT mode: return from session tracking
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry)
            return kIOReturnBadArgument;
        *connectionCount = 0;
        for(uint32_t i = 0; i < entry->connectionCount && (int)i < kDEXTMaxConnectionsPerSession; i++) {
            connectionIds[i] = entry->connectionIds[i];
            (*connectionCount)++;
        }
        return kIOReturnSuccess;
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    const UInt32 expOutputCnt = 1;
    UInt64 output;
    UInt32 outputCnt = expOutputCnt;

    *connectionCount = 0;
    size_t outputStructSize = sizeof(ConnectionIdentifier)*kiSCSIMaxConnectionsPerSession;

    kern_return_t result =
        IOConnectCallMethod(interface->connect,kiSCSIGetConnectionIds,&input,inputCnt,0,0,
                            &output,&outputCnt,connectionIds,&outputStructSize);

    if(result == kIOReturnSuccess && outputCnt == expOutputCnt)
        *connectionCount = (UInt32)output;

    return result;
}

/*! Creates a string containing the target IQN associated with a session.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session identifier.
 *  @param targetIQN the name of the target.
 *  @param size the size of the targetIQNCString buffer.
 *  @return error code indicating result of operation. */
CFStringRef iSCSIHBAInterfaceCreateTargetIQNForSessionId(iSCSIHBAInterfaceRef interface,
                                                         SessionIdentifier sessionId)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId)
        return NULL;

    // DEXT mode: return cached target IQN
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry || strlen(entry->targetIQN) == 0)
            return NULL;
        return CFStringCreateWithCString(kCFAllocatorDefault, entry->targetIQN, kCFStringEncodingASCII);
    }

    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;

    const char targetIQN[NI_MAXHOST];
    size_t targetIQNLength = NI_MAXHOST;

    kern_return_t result = IOConnectCallMethod(interface->connect,kiSCSICreateTargetIQNForSessionId,
                                               &input,inputCnt,0,0,0,0,
                                               (void*)targetIQN,&targetIQNLength);
    if(result != kIOReturnSuccess)
        return NULL;

    return CFStringCreateWithCString(kCFAllocatorDefault,targetIQN,kCFStringEncodingASCII);
}

/*! Creates a string containing the address of the portal associated with
 *  the specified connection.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session identifier.
 *  @param connectionId connection identifier.
 *  @return a string containing the portal address, or NULL if the session or
 *  connection was invalid. */
CFStringRef iSCSIHBAInterfaceCreatePortalAddressForConnectionId(iSCSIHBAInterfaceRef interface,
                                                                SessionIdentifier sessionId,
                                                                ConnectionIdentifier connectionId)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return NULL;

    // DEXT mode: return cached portal address
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry || strlen(entry->portalAddress) == 0)
            return NULL;
        return CFStringCreateWithCString(kCFAllocatorDefault, entry->portalAddress, kCFStringEncodingASCII);
    }

    const UInt32 inputCnt = 2;
    UInt64 input[] = {sessionId,connectionId};

    const char portalAddress[NI_MAXHOST];
    size_t portalAddressLength = NI_MAXHOST;

    kern_return_t result = IOConnectCallMethod(interface->connect,kiSCSIGetPortalAddressForConnectionId,
                                               input,inputCnt,0,0,0,0,
                                               (void *)portalAddress,&portalAddressLength);
    if(result != kIOReturnSuccess)
        return NULL;

    return CFStringCreateWithCString(kCFAllocatorDefault,portalAddress,kCFStringEncodingASCII);
}

/*! Creates a string containing the TCP port of the portal associated with
 *  the specified connection.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session identifier.
 *  @param connectionId connection identifier.
 *  @return a string containing the TCP port of the portal, or NULL if the
 *  session or connection was invalid. */
CFStringRef iSCSIHBAInterfaceCreatePortalPortForConnectionId(iSCSIHBAInterfaceRef interface,
                                                             SessionIdentifier sessionId,
                                                             ConnectionIdentifier connectionId)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return NULL;

    // DEXT mode: return cached portal port
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry || strlen(entry->portalPort) == 0)
            return NULL;
        return CFStringCreateWithCString(kCFAllocatorDefault, entry->portalPort, kCFStringEncodingASCII);
    }

    const UInt32 inputCnt = 2;
    UInt64 input[] = {sessionId,connectionId};

    const char portalPort[NI_MAXSERV];
    size_t portalPortLength = NI_MAXSERV;

    kern_return_t result = IOConnectCallMethod(interface->connect,kiSCSIGetPortalPortForConnectionId,
                                               input,inputCnt,0,0,0,0,
                                               (void *)portalPort,&portalPortLength);
    if(result != kIOReturnSuccess)
        return NULL;

    return CFStringCreateWithCString(kCFAllocatorDefault,portalPort,kCFStringEncodingASCII);
}

/*! Creates a string containing the host interface used for the connection.
 *  @param interface an instance of an iSCSIHBAInterface.
 *  @param sessionId session identifier.
 *  @param connectionId connection identifier.
 *  @return a string containing the host interface name, or NULL if the
 *  session or connection was invalid. */
CFStringRef iSCSIHBAInterfaceCreateHostInterfaceForConnectionId(iSCSIHBAInterfaceRef interface,
                                                                SessionIdentifier sessionId,
                                                                ConnectionIdentifier connectionId)
{
    if(!interface || sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return NULL;

    // DEXT mode: return cached host interface
    if(interface->dextMode) {
        uint8_t dextId = dextSessionIdFromId(sessionId);
        DextSessionEntry *entry = dextFindSession(interface, dextId);
        if(!entry || strlen(entry->hostInterface) == 0)
            return NULL;
        return CFStringCreateWithCString(kCFAllocatorDefault, entry->hostInterface, kCFStringEncodingASCII);
    }

    const UInt32 inputCnt = 2;
    UInt64 input[] = {sessionId,connectionId};


    const char hostInterface[NI_MAXHOST];
    size_t hostInterfaceLength = NI_MAXHOST;

    kern_return_t result = IOConnectCallMethod(interface->connect,kiSCSIGetHostInterfaceForConnectionId,
                                               input,inputCnt,0,0,0,0,
                                               (void *)hostInterface,&hostInterfaceLength);
    if(result != kIOReturnSuccess)
        return NULL;

    return CFStringCreateWithCString(kCFAllocatorDefault,hostInterface,kCFStringEncodingASCII);
}
