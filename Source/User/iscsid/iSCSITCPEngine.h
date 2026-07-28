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
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ISCSI_TCP_ENGINE_H__
#define __ISCSI_TCP_ENGINE_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/*! Maximum number of TCP connections tracked by the engine. */
#define kTCPEngineMaxConnections    32

/*! Default TCP send/receive buffer size (64KB). */
#define kTCPEngineBufferSize        65536

/*! TCP connection timeout in seconds. */
#define kTCPEngineConnectTimeout    10

/*! Maximum data segment to buffer for send.
 *  Large enough to hold the biggest iSCSI burst. */
#define kTCPEngineMaxDataSegment    262144

#pragma mark - Connection Context

/*! Per-connection state tracked by the TCP engine. */
typedef struct TCPConnectionContext {
    /*! The BSD socket fd for this connection. */
    int fd;

    /*! Whether the connection is fully established. */
    bool connected;

    /*! Whether a connect() is in progress (non-blocking connect pending). */
    bool connecting;

    /*! iSCSI session this connection belongs to. */
    uint8_t sessionId;

    /*! iSCSI connection ID within the session. */
    uint32_t connectionId;

    /*! Peer address (for connection tracking). */
    struct sockaddr_storage peerAddr;

    /*! Send buffer. */
    uint8_t * sendBuf;

    /*! Total bytes in send buffer. */
    size_t sendLen;

    /*! Bytes already sent from send buffer. */
    size_t sendOffset;

    /*! Receive buffer (accumulates data across kqueue events). */
    uint8_t * recvBuf;

    /*! Bytes received and pending in recv buffer. */
    size_t recvLen;

    /*! Allocated size of recv buffer. */
    size_t recvAlloc;

    /*! Opaque user pointer for the daemon's per-connection state. */
    void * userData;
} TCPConnectionContext;

#pragma mark - Event Types

/*! Bitmask of event types passed to the callback. */
enum TCPEngineEventType {
    kTCPEventNone           = 0,

    /*! Connection was established (EVFILT_WRITE on connect). */
    kTCPEventConnected      = (1 << 0),

    /*! Connection failed (connect error or TCP RST). */
    kTCPEventConnectFailed  = (1 << 1),

    /*! Data is available to read (EVFILT_READ). */
    kTCPEventDataAvailable  = (1 << 2),

    /*! Connection closed by peer (EVFILT_READ with EOF). */
    kTCPEventDisconnected   = (1 << 3),

    /*! Send buffer drained (EVFILT_WRITE, space available). */
    kTCPEventCanSend        = (1 << 4),

    /*! Connection timed out. */
    kTCPEventTimeout        = (1 << 5),

    /*! All events mask. */
    kTCPEventAll            = 0x3F
};

#pragma mark - Callback

/*!
 * @brief Callback invoked when kqueue events are processed.
 * @param ctx  The connection context for the fd that generated the event.
 * @param events Bitmask of enum TCPEngineEventType.
 * @param context  Opaque user context passed to TCPEngineProcessEvents().
 */
typedef void (*TCPEngineCallback)(TCPConnectionContext * ctx,
                                   uint32_t events,
                                   void * context);

#pragma mark - API

/*!
 * @brief Creates a kqueue file descriptor for the TCP engine.
 * @return The kqueue fd, or -1 on error (errno set).
 */
int TCPEngineCreate(void);

/*!
 * @brief Destroys the kqueue and closes all tracked connections.
 * @param kq  The kqueue fd to destroy.
 */
void TCPEngineDestroy(int kq);

/*!
 * @brief Create a non-blocking TCP socket suitable for async connect.
 * @param outFd  On success, the new socket fd.
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineCreateSocket(int * outFd);

/*!
 * @brief Bind a socket to a specific local interface (optional).
 * @param fd  The socket fd.
 * @param ifName  The interface name (e.g., "en0"), or NULL for any.
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineBindToInterface(int fd, const char * ifName);

/*!
 * @brief Initiate a non-blocking TCP connect.
 *
 * After calling this, add the fd to the kqueue with EVFILT_WRITE.
 * When the write filter fires, check for connection success via SO_ERROR.
 *
 * @param fd  The socket fd to connect.
 * @param addr  The target address (IPv4 or IPv6).
 * @param port  The target port (host byte order).
 * @return 0 if connect succeeded immediately, EINPROGRESS if pending,
 *         or another errno on failure.
 */
errno_t TCPEngineConnect(int fd, const struct sockaddr_storage * addr, uint16_t port);

/*!
 * @brief Add a socket fd to the kqueue.
 *
 * Registers EVFILT_READ and EVFILT_WRITE filters. The filter flags
 * can be adjusted later with TCPEngineUpdateFilters().
 *
 * @param kq  The kqueue fd.
 * @param fd  The socket fd to monitor.
 * @param sessionId  iSCSI session ID for this connection.
 * @param connectionId  iSCSI connection ID within the session.
 * @param ctx  Pointer to a TCPConnectionContext to associate. The caller
 *             must ensure this remains valid until TCPEngineRemoveSocket().
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineAddSocket(int kq, int fd,
                           uint8_t sessionId, uint32_t connectionId,
                           TCPConnectionContext * ctx);

/*!
 * @brief Remove a socket from the kqueue and stop monitoring it.
 * @param kq  The kqueue fd.
 * @param fd  The socket fd to remove.
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineRemoveSocket(int kq, int fd);

/*!
 * @brief Update the EVFILT_READ/EVFILT_WRITE filter flags for a socket.
 *
 * Useful to enable/disable write filter when the send buffer is
 * empty/full (edge-triggered mode).
 *
 * @param kq  The kqueue fd.
 * @param fd  The socket fd.
 * @param readEnable  true to enable EVFILT_READ, false to disable.
 * @param writeEnable  true to enable EVFILT_WRITE, false to disable.
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineUpdateFilters(int kq, int fd,
                                bool readEnable, bool writeEnable);

/*!
 * @brief Queue data for sending on a connection.
 *
 * Data is copied into the connection's send buffer. The EVFILT_WRITE
 * filter is enabled so the engine can flush the buffer when the socket
 * is writable.
 *
 * @param ctx  The connection context.
 * @param data  Pointer to data to send.
 * @param len  Length of data in bytes.
 * @return 0 on success, or ENOMEM if buffer is full.
 */
errno_t TCPEngineSend(TCPConnectionContext * ctx,
                       const uint8_t * data, size_t len);

/*!
 * @brief Process pending kqueue events.
 *
 * This should be called from the kqueue CFFileDescriptor callback
 * (when kevent fd is ready). It reads all pending events and invokes
 * the callback for each connection that has events.
 *
 * @param kq  The kqueue fd.
 * @param callback  Function to call for each event.
 * @param context  Opaque context passed to the callback.
 * @return 0 on success, or an errno value.
 */
errno_t TCPEngineProcessEvents(int kq,
                                TCPEngineCallback callback,
                                void * context);

/*!
 * @brief Get the connection context associated with a socket fd.
 *
 * Looks up the context from the kevent udata field. Only valid for
 * sockets that have been added via TCPEngineAddSocket().
 *
 * @param ctx  The pointer to retrieve context into.
 * @param kev  A kevent struct returned by kevent().
 * @return 0 on success, ENOENT if no context is associated.
 */
static inline TCPConnectionContext *
TCPEngineGetContext(struct kevent * kev)
{
    return (TCPConnectionContext *)kev->udata;
}

/*!
 * @brief Close a connection and release its buffers.
 * @param ctx  The connection context to close.
 */
void TCPEngineCloseConnection(TCPConnectionContext * ctx);

#endif /* __ISCSI_TCP_ENGINE_H__ */
