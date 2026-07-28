# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build & Install

```bash
# Build all components (framework, kext, daemon, CLI tool)
cd Scripts && ./build.sh

# Install to system (requires SIP disabled for kext loading)
cd Scripts && sudo ./install.sh

# Uninstall
cd Scripts && sudo ./uninstall.sh
```

- Each of the 4 Xcode schemes can also be built individually: `iSCSI.framework`, `iSCSI.kext`, `iscsid`, `iscsictl`
- Release DMG is built with `cd Distribution && ./package.sh`

## Architecture Overview

Kernel-mode iSCSI initiator for macOS (RFC 3720) with pending migration to DriverKit/System Extensions. A software virtual Host Bus Adapter (HBA) presents iSCSI LUNs to macOS via IOKit's `IOSCSIParallelInterfaceController`.

### Kernel Layer (`Source/Kernel/`)

- **iSCSIInitiator** — IOService entry point; acts as nub for the virtual HBA
- **iSCSIVirtualHBA** — Core virtual HBA (extends `IOSCSIParallelInterfaceController`). Manages sessions/connections, converts SCSI CDBs to/from iSCSI PDUs, communicates over kernel BSD sockets (`kpi_socket`). Session/connection state tracked via `iSCSISession`/`iSCSIConnection` structures
- **iSCSIHBAUserClient** — IOKit user client for daemon ↔ kernel communication
- **iSCSIPDUKernel** — Kernel-side PDU encoding/decoding
- **iSCSITaskQueue** — SCSI task queuing and timeout management
- **iSCSIIOEventSource** — Software interrupt source driving workloop processing
- **crc32c** — CRC32C checksum for data integrity
- **iSCSIKernelClasses.h** — Name-mangling macros to prevent IORegistry namespace collisions

### User Layer (`Source/User/`)

- **iSCSI Framework** (`Source/User/iSCSI Framework/`) — Shared library providing:
  - `iSCSIPreferences` — Reading/writing initiator configuration
  - `iSCSIDaemonInterface` — IPC between iscsictl and iscsid (Mach ports)
  - `iSCSIDA` — Disk Arbitration integration for automatic volume mounting
  - `iSCSIKeychain` — CHAP secret storage
  - `iSCSITypes` — Shared type definitions and helpers
  - `iSCSIIORegistry` — IORegistry interaction helpers
  - `iSCSIAuthRights` — Authorization rights for privileged operations
  - `iSCSIUtils` — String/network utility functions

- **iscsid** (`Source/User/iscsid/`) — Launch daemon (`com.github.iscsi-osx.iscsid`). Handles:
  - Session management (login/logout, parameter negotiation)
  - Target discovery (iSNS, SendTargets)
  - Authentication (CHAP)
  - PDU processing in user space (login, text, logout PDUs)
  - Kernel interface for session/connection lifecycle

- **iscsictl** (`Source/User/iscsictl/`) — CLI tool for managing targets, discovery portals, and initiator configuration

### Key Shared Definitions

- `iSCSIPDUShared.h` — PDU header structures (BHS, initiator/target variants) and opcode enums shared between kernel and user space
- `iSCSIDaemonInterfaceShared.h` — Mach IPC message types between daemon and framework
- `iSCSITypesShared.h` — Common type definitions across layers
- `iSCSIRFC3720Defaults.h` — RFC 3720 default values for parameters
- `iSCSIRFC3720Keys.h` — Text-negotiation key definitions

### Data Flow

```
SCSI Layer (kernel) → iSCSIVirtualHBA → kernel socket → iSCSI Target
                           ↑↓
                    iSCSIHBAUserClient  (IOKit user client)
                           ↑↓
                        iscsid (daemon) ←→ iSCSI Framework
                           ↑↓
                       iscsictl (CLI tool)
```

SCSI CDBs from macOS IOKit SCSI stack enter `iSCSIVirtualHBA::ProcessParallelTask()`, are packaged into iSCSI PDUs (with command sequence numbering, data segmentation), and sent over kernel TCP sockets. Response PDUs arrive via `iSCSIIOEventSource` workloop events and are processed back into SCSI completions. The userspace daemon handles login, discovery, authentication, and parameter negotiation, then activates connections for full-feature phase.
