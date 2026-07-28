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
 * iSCSITCPEngine.c — kqueue-based asynchronous TCP I/O engine.
 *
 * Replaces kernel sockets (kpi_socket) used by the kext. This engine
 * runs in the daemon process and provides non-blocking connect/send/recv
 * driven by kqueue events, delivered to the CFRunLoop via CFFileDescriptor.
 *
 * Each connection is represented by a TCPConnectionContext that holds
 * send/recv buffers and iSCSI session identifiers. The engine is used
 * for both the login/discovery phase (where login/text/logout PDUs are
 * sent/recv'd by the daemon) and the full-feature data phase (where SCSI
 * command PDUs are relayed between the DEXT and TCP).
 */

#include "iSCSITCPEngine.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <asl.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/tcp.h>

#pragma mark - Internal Helpers

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return errno;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return errno;
    return 0;
}

static int set_nodelay(int fd)
{
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1)
        return errno;
    return 0;
}

#pragma mark - API Implementation

int TCPEngineCreate(void)
{
    int kq = kqueue();
    if (kq == -1) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "TCPEngine: kqueue() failed: %s", strerror(errno));
    }
    return kq;
}

void TCPEngineDestroy(int kq)
{
    if (kq >= 0) {
        close(kq);
    }
}

errno_t TCPEngineCreateSocket(int * outFd)
{
    if (!outFd) return EINVAL;

    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        // Try IPv4 if IPv6 fails (e.g., no IPv6 stack)
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            asl_log(NULL, NULL, ASL_LEVEL_ERR,
                    "TCPEngine: socket() failed: %s", strerror(errno));
            return errno;
        }
    }

    // Make non-blocking
    errno_t err = set_nonblocking(fd);
    if (err) {
        close(fd);
        return err;
    }

    // Disable Nagle
    err = set_nodelay(fd);
    if (err) {
        close(fd);
        return err;
    }

    // Set socket to close-on-exec
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    *outFd = fd;
    return 0;
}

errno_t TCPEngineBindToInterface(int fd, const char * ifName)
{
#if defined(IP_BOUND_IF) && defined(IPV6_BOUND_IF)
    if (!ifName) return 0;

    unsigned int ifIndex = if_nametoindex(ifName);
    if (ifIndex == 0) {
        asl_log(NULL, NULL, ASL_LEVEL_WARNING,
                "TCPEngine: interface %s not found", ifName);
        return ENXIO;
    }

    // Determine socket family and apply appropriate option
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrLen) == 0) {
        if (addr.ss_family == AF_INET6) {
            setsockopt(fd, IPPROTO_IPV6, IPV6_BOUND_IF,
                       &ifIndex, sizeof(ifIndex));
        } else if (addr.ss_family == AF_INET) {
            setsockopt(fd, IPPROTO_IP, IP_BOUND_IF,
                       &ifIndex, sizeof(ifIndex));
        }
    }
#else
    (void)fd;
    (void)ifName;
#endif
    return 0;
}

errno_t TCPEngineConnect(int fd,
                          const struct sockaddr_storage * addr,
                          uint16_t port)
{
    if (!addr) return EINVAL;

    int ret = -1;

    if (addr->ss_family == AF_INET) {
        struct sockaddr_in * sin = (struct sockaddr_in *)addr;
        sin->sin_port = htons(port);

        ret = connect(fd, (struct sockaddr *)sin, sizeof(*sin));
    }
    else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)addr;
        sin6->sin6_port = htons(port);

        ret = connect(fd, (struct sockaddr *)sin6, sizeof(*sin6));
    }
    else {
        return EAFNOSUPPORT;
    }

    if (ret == 0) {
        // Connected immediately (rare for non-blocking, but possible)
        return 0;
    }

    if (errno == EINPROGRESS) {
        // Expected for non-blocking connect
        return EINPROGRESS;
    }

    // Real error
    asl_log(NULL, NULL, ASL_LEVEL_ERR,
            "TCPEngine: connect() failed: %s", strerror(errno));
    return errno;
}

errno_t TCPEngineAddSocket(int kq, int fd,
                            uint8_t sessionId, uint32_t connectionId,
                            TCPConnectionContext * ctx)
{
    if (!ctx) return EINVAL;

    // Fill in context
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->sessionId = sessionId;
    ctx->connectionId = connectionId;
    ctx->connected = false;
    ctx->connecting = false;

    // Register EVFILT_READ and EVFILT_WRITE with kqueue
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, ctx);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR | EV_DISABLE,
           0, 0, ctx);

    if (kevent(kq, changes, 2, NULL, 0, NULL) == -1) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "TCPEngine: kevent add failed for fd %d: %s",
                fd, strerror(errno));
        return errno;
    }

    return 0;
}

errno_t TCPEngineRemoveSocket(int kq, int fd)
{
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

    if (kevent(kq, &change, 1, NULL, 0, NULL) == -1 && errno != ENOENT) {
        // ENOENT is OK if the filter wasn't registered
        asl_log(NULL, NULL, ASL_LEVEL_WARNING,
                "TCPEngine: kevent delete failed for fd %d: %s",
                fd, strerror(errno));
    }

    // Also try to remove EVFILT_WRITE (EV_DELETE on non-existent is OK)
    EV_SET(&change, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(kq, &change, 1, NULL, 0, NULL);

    return 0;
}

errno_t TCPEngineUpdateFilters(int kq, int fd,
                                bool readEnable, bool writeEnable)
{
    struct kevent changes[2];
    int nChanges = 0;

    EV_SET(&changes[nChanges], fd, EVFILT_READ,
           readEnable ? EV_ENABLE : EV_DISABLE, 0, 0, NULL);
    nChanges++;

    EV_SET(&changes[nChanges], fd, EVFILT_WRITE,
           writeEnable ? EV_ENABLE : EV_DISABLE, 0, 0, NULL);
    nChanges++;

    if (kevent(kq, changes, nChanges, NULL, 0, NULL) == -1) {
        return errno;
    }

    return 0;
}

errno_t TCPEngineSend(TCPConnectionContext * ctx,
                       const uint8_t * data, size_t len)
{
    if (!ctx || !data) return EINVAL;

    // If we can send directly and the send buffer is empty, try a direct write
    if (ctx->sendLen == 0 && ctx->connected) {
        ssize_t sent = write(ctx->fd, data, len);
        if (sent > 0) {
            if ((size_t)sent == len) {
                return 0; // Everything sent immediately
            }
            // Partial send — buffer the remainder
            data += sent;
            len -= sent;
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return errno;
        }
        // EAGAIN falls through to buffer
    }

    // Buffer the data for later sending
    size_t newLen = ctx->sendLen + len;
    if (newLen > kTCPEngineMaxDataSegment) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "TCPEngine: send buffer overflow for fd %d", ctx->fd);
        return ENOMEM;
    }

    uint8_t * newBuf = realloc(ctx->sendBuf, newLen);
    if (!newBuf) {
        return ENOMEM;
    }

    memcpy(newBuf + ctx->sendLen, data, len);
    ctx->sendBuf = newBuf;
    ctx->sendLen = newLen;

    return 0;
}

errno_t TCPEngineProcessEvents(int kq,
                                TCPEngineCallback callback,
                                void * context)
{
    if (!callback) return EINVAL;

    struct kevent events[kTCPEngineMaxConnections];
    struct timespec timeout = { 0, 0 }; // Non-blocking poll

    int nEvents = kevent(kq, NULL, 0, events, kTCPEngineMaxConnections, &timeout);
    if (nEvents == -1) {
        if (errno == EINTR) return 0;
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "TCPEngine: kevent() failed: %s", strerror(errno));
        return errno;
    }

    for (int i = 0; i < nEvents; i++) {
        struct kevent * ev = &events[i];
        TCPConnectionContext * ctx = (TCPConnectionContext *)ev->udata;
        if (!ctx) continue;

        uint32_t eventMask = kTCPEventNone;

        if (ev->flags & EV_EOF) {
            // Connection closed by peer
            eventMask |= kTCPEventDisconnected;
        }
        else if (ev->filter == EVFILT_READ) {
            // Data available to read
            eventMask |= kTCPEventDataAvailable;

            // In edge-triggered mode, read all available data now
            size_t newAlloc = ctx->recvAlloc + (size_t)ev->data + 4096;
            if (newAlloc > kTCPEngineMaxDataSegment * 4) {
                newAlloc = kTCPEngineMaxDataSegment * 4;
            }

            uint8_t * newBuf = realloc(ctx->recvBuf, newAlloc);
            if (newBuf) {
                // Read as much as possible
                ssize_t nRead = read(ctx->fd,
                                     newBuf + ctx->recvLen,
                                     newAlloc - ctx->recvLen);
                if (nRead > 0) {
                    ctx->recvBuf = newBuf;
                    ctx->recvLen += (size_t)nRead;
                    ctx->recvAlloc = newAlloc;
                } else if (nRead == 0) {
                    // EOF
                    eventMask |= kTCPEventDisconnected;
                    free(newBuf);
                    if (ctx->recvBuf) free(ctx->recvBuf);
                    ctx->recvBuf = NULL;
                    ctx->recvLen = 0;
                    ctx->recvAlloc = 0;
                } else {
                    // EAGAIN is expected in edge-triggered mode
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                                "TCPEngine: read fd %d failed: %s",
                                ctx->fd, strerror(errno));
                    }
                    free(newBuf);
                }
            }
        }
        else if (ev->filter == EVFILT_WRITE) {
            if (!ctx->connected && !ctx->connecting) {
                // Connect completed – check SO_ERROR
                int so_error = 0;
                socklen_t errlen = sizeof(so_error);
                if (getsockopt(ctx->fd, SOL_SOCKET, SO_ERROR,
                               &so_error, &errlen) == 0 && so_error == 0) {
                    ctx->connected = true;
                    eventMask |= kTCPEventConnected;

                    // Disable write filter until we have data to send
                    TCPEngineUpdateFilters(kq, ctx->fd, true, false);
                } else {
                    if (so_error == 0) so_error = ECONNREFUSED;
                    ctx->connected = false;
                    eventMask |= kTCPEventConnectFailed;
                }
                continue; // Don't try to send data yet
            }

            // Socket is writable — flush send buffer
            if (ctx->sendLen > ctx->sendOffset) {
                ssize_t nWritten = write(ctx->fd,
                                         ctx->sendBuf + ctx->sendOffset,
                                         ctx->sendLen - ctx->sendOffset);
                if (nWritten > 0) {
                    ctx->sendOffset += (size_t)nWritten;
                    if (ctx->sendOffset >= ctx->sendLen) {
                        // All sent — free buffer and disable write filter
                        free(ctx->sendBuf);
                        ctx->sendBuf = NULL;
                        ctx->sendLen = 0;
                        ctx->sendOffset = 0;
                        eventMask |= kTCPEventCanSend;
                        TCPEngineUpdateFilters(kq, ctx->fd, true, false);
                    }
                } else if (nWritten < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    asl_log(NULL, NULL, ASL_LEVEL_ERR,
                            "TCPEngine: write fd %d failed: %s",
                            ctx->fd, strerror(errno));
                    eventMask |= kTCPEventDisconnected;
                }
            }
        }

        if (eventMask != kTCPEventNone) {
            callback(ctx, eventMask, context);
        }
    }

    return 0;
}

void TCPEngineCloseConnection(TCPConnectionContext * ctx)
{
    if (!ctx) return;

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    if (ctx->sendBuf) {
        free(ctx->sendBuf);
        ctx->sendBuf = NULL;
    }
    ctx->sendLen = 0;
    ctx->sendOffset = 0;

    if (ctx->recvBuf) {
        free(ctx->recvBuf);
        ctx->recvBuf = NULL;
    }
    ctx->recvLen = 0;
    ctx->recvAlloc = 0;

    ctx->connected = false;
    ctx->connecting = false;
    ctx->userData = NULL;
}
