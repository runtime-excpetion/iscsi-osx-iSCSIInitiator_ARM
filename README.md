因为本人不是苹果开发者，没有对应证书，故无法编译KEXT，进行后续操作。其他人可以基于本项目进行编译。
**Update 28 February 2023** : Apple has not provided a mechanism for opening sockets from within system extensions (if you know of one do let me know). The porting of this initiator away from kernel extensions is facilitated by `IOUserSCSIParallelInterfaceController` in a system extension, but the opening / management of sockets are required for communication. Without a means to do so, re-architecture of the project is required (for which I do not have the bandwidth). Such an architecture would likely result in a performance penality. It's not clear what, if any, replacement Apple plans for the `kpi_socket` interface (in userland / System Extensions).

**Update 27 March 2021** : Further development is on hold until DriverKit 20.4 (Beta) is released, with support for `IOUserSCSIParallelInterfaceController`. This software will ultimately transition away from kernel extensions.


### Overview

iSCSI initiator is a software initiator for macOS. It allows machines running macOS to connect to iSCSI targets. It automatically detects and mounts logical units on which users can then create and mount volumes. For more information about the iSCSI standard, see IETF RFC3720.

This fork is a modified version of the [iscsi-osx/iSCSIInitiator](https://github.com/iscsi-osx/iSCSIInitiator) project, re-architected with a **DriverKit System Extension (DEXT)** approach to migrate away from the deprecated kernel extension (KEXT) model on modern macOS. The original project's kernel-mode virtual HBA (`iSCSIVirtualHBA`) has been replaced by a userspace daemon (`iscsid`) that manages TCP sockets directly, combined with an `IOUserSCSIParallelInterfaceController`-based DEXT for SCSI command transport. See the [Architecture](#architecture) section below for details.

> ⚠️ **Disclaimer**: This project has been modified with the assistance of DeepSeek AI. While extensive code review and bug fixes have been applied, there may still be undiscovered vulnerabilities or issues. Use at your own risk in production environments.


#### Key Features

- iSCSI initiator supporting RFC 3720
- CHAP authentication (unidirectional and bidirectional)
- iSNS and SendTargets discovery
- Multiple connections per session (MC/S)
- Header and data digest support (CRC32C)
- Persistent target configuration via preferences plist
- Command-line management tool (`iscsictl`)
- Automatic volume mounting via Disk Arbitration
- DriverKit System Extension for macOS 11+ (Apple Silicon & Intel)

#### Architecture

This fork introduces a new architecture to address the deprecation of kernel extensions on modern macOS:

```
┌─────────────────────────────────────────┐
│  macOS SCSI Layer (IOKit)               │
├─────────────────────────────────────────┤
│  iSCSI.dext (DriverKit)                 │
│  IOUserSCSIParallelInterfaceController  │
│  - SCSI CDB ↔ iSCSI PDU translation     │
│  - Communicates with daemon via IPC      │
├─────────────────────────────────────────┤
│  iscsid (Launch Daemon)                 │
│  - TCP socket management (userspace)    │
│  - Login / Discovery / Authentication   │
│  - PDU Relay & TCP Engine               │
│  - CRC32C digest verification           │
├─────────────────────────────────────────┤
│  iSCSI Framework (Shared Library)       │
│  - Preferences / Configuration          │
│  - Keychain integration                 │
├─────────────────────────────────────────┤
│  iscsictl (CLI Tool)                    │
│  - Target & portal management           │
└─────────────────────────────────────────┘
```

**Key architectural changes from the original:**

| Aspect | Original (KEXT) | This Fork (DEXT) |
|--------|----------------|-------------------|
| Socket management | Kernel `kpi_socket` | Userspace BSD sockets |
| SCSI transport | Kernel `IOSCSIParallelInterfaceController` | DriverKit `IOUserSCSIParallelInterfaceController` |
| Login / Auth | Mixed kernel/userspace | Fully userspace daemon |
| Data path | Kernel PDU encoding | Userspace PDU Relay + TCP Engine |

For detailed migration progress, see the `.claude/projects/.../memory/` directory.

### Installation Prerequisites

Builds of the kernel extension will not be signed and as a result macOS won't load them. Kext signing must therefore be disabled before attempting to install and load the kernel extension. Additionally, as of El Capitan, new security measures have been implemented that prevent the installation of files in certain protected system folders (unless the files are placed there by an appropriate installer). For this reason, it is important to follow the directions applicable to the relevant version of macOS **prior** to installation of the initiator.

##### macOS 10.10 and earlier (prior to El Capitan)
Run the following command at a terminal prompt:
 
    sudo nvram boot-args=kext-dev-mode=1

The kernel will load unsigned kernel extensions after a reboot.

##### macOS 10.11 and later

Run the following command at the Recover OS terminal window:

    csrutil disable

Follow the instructions in the [System Integrity Protection Guide](https://developer.apple.com/library/mac/documentation/Security/Conceptual/System_Integrity_Protection_Guide/KernelExtensions/KernelExtensions.html#//apple_ref/doc/uid/TP40016462-CH4-SW1) to access the Recover OS terminal window. Two reboots may be required during this process.

##### Apple Silicon / macOS 11+ with DEXT (this fork)

All modern macOS versions (11+) support DEXT. However, the DEXT requires code signing with an Apple Developer Program certificate. See [Known Issues](#known-issues) for details. The KEXT path remains available for Intel Macs only (builds are x86_64).

### Build & Install

#### Building from Source

```bash
# Build all components (DEXT, framework, daemon, CLI tool, KEXT)
cd Scripts && ./build.sh
```

Individual Xcode schemes can also be built:

```bash
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iSCSI.framework build CODE_SIGNING_ALLOWED=NO
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iscsid build CODE_SIGNING_ALLOWED=NO
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iscsictl build CODE_SIGNING_ALLOWED=NO
```

#### Installing

```bash
# Install all components to system
cd Scripts && sudo ./install.sh
```

This installs:
- `iSCSI.framework` → `/Library/Frameworks/`
- `iscsid` → `/usr/local/libexec/`
- `iscsictl` → `/usr/local/bin/`
- `iSCSI.dext` → `/Library/DriverExtensions/` (macOS 11+)
- `iSCSIInitiator.kext` → `/Library/Extensions/` (legacy)
- LaunchDaemon plist → `/Library/LaunchDaemons/`

#### Uninstalling

```bash
cd Scripts && sudo ./uninstall.sh
```

#### Building Release DMG

```bash
cd Distribution && ./package.sh
```

### Usage

#### Managing the Daemon

The daemon runs as a LaunchDaemon (`com.github.iscsi-osx.iscsid`) and restarts automatically on crash via `KeepAlive`.

```bash
# Check daemon status
sudo launchctl list | grep iscsi

# Start daemon
sudo launchctl load /Library/LaunchDaemons/com.github.iscsi-osx.iscsid.plist

# Stop daemon
sudo launchctl unload /Library/LaunchDaemons/com.github.iscsi-osx.iscsid.plist

# View daemon logs
sudo cat /var/log/iscsid
```

#### Target Discovery

```bash
# Add a discovery portal
sudo iscsictl add discovery portal 192.168.28.30:3260

# Discover available targets
sudo iscsictl discover targets
```

#### Target Configuration

```bash
# Add a target
sudo iscsictl add target iqn.2026-07.com.example:target-1

# Set CHAP authentication for a target
sudo iscsictl modify target iqn.2026-07.com.example:target-1 \
    set chap username initiator password mysecret123456

# Set target alias
sudo iscsictl modify target iqn.2026-07.com.example:target-1 \
    set alias "My iSCSI Target"

# View configured targets
sudo iscsictl list targets
```

#### Login & Logout

```bash
# Login to a specific target
sudo iscsictl login iqn.2026-07.com.example:target-1

# Login to a specific portal
sudo iscsictl login iqn.2026-07.com.example:target-1 \
    portal 192.168.28.30:3260

# Logout from a target
sudo iscsictl logout iqn.2026-07.com.example:target-1

# Logout from all targets
sudo iscsictl logout all
```

#### Verifying Connected Disks

```bash
# After successful login, verify disks are visible
diskutil list

# View detailed iSCSI session info
sudo iscsictl list sessions
```

### Configuration

Target configuration is persisted in `/Library/Preferences/com.github.iscsi-osx.iSCSIInitiator.plist`.

Initiator-wide settings can be configured via:

```bash
# Set initiator alias (iSCSI name)
sudo iscsictl modify initiator set alias "my-initiator"

# Set initiator CHAP (for bidirectional auth)
sudo iscsictl modify initiator set chap username myname password mysecret

# Set default error recovery level
sudo iscsictl modify initiator set error-recovery 0
```

### Known Issues

#### 1. KEXT only supports x86_64 (macOS ≥ 26 + Apple Silicon incompatible)

The kernel extension (`iSCSIInitiator.kext`) is compiled as x86_64 only. On macOS 26+ with Apple Silicon (arm64), kernel extensions are fully deprecated and cannot be loaded regardless of architecture. KEXT loading may work on Intel Macs with SIP disabled on older macOS versions (10.x–12.x).

#### 2. DEXT requires Apple Developer Program membership

The DriverKit extension (`iSCSI.dext`) has been implemented and builds successfully for arm64. However, DriverKit system extensions require code signing with a valid **Apple Developer Program** certificate ($99/year). Ad-hoc signing (`CODE_SIGN_IDENTITY=-`) is explicitly rejected by the DriverKit SDK:

```
error: Ad Hoc code signing is not allowed with SDK 'DriverKit 25.5'.
```

Additionally, the DEXT requires a provisioning profile with the `com.apple.developer.driverkit.userclient-access` entitlement, which is only available to paid developer accounts. Without a paid account, the DEXT cannot be loaded by macOS and the login process will complete (iSCSI negotiation succeeds) but no disks will appear in `diskutil list`.

**Workaround for development/testing:** use an Intel Mac with macOS 10.x–12.x and the x86_64 KEXT, or obtain an Apple Developer Program membership.

#### 3. macOS SIP must be disabled

System Integrity Protection (SIP) must be disabled to load unsigned kernel extensions. This is required regardless of whether you are using the KEXT or DEXT path, as the DEXT also requires reduced security to load development-signed extensions:

```bash
# In Recovery OS terminal:
csrutil disable
# Reboot
```

### Project Structure

```
iSCSIInitiator/
├── Scripts/              # Build, install, uninstall scripts
│   ├── build.sh
│   ├── install.sh
│   └── uninstall.sh
├── Source/
│   ├── DEXT/             # DriverKit system extension
│   │   ├── iSCSIDextHBA.cpp/h   # Virtual HBA implementation
│   │   ├── iSCSIDextUserClient.cpp/h  # IPC user client
│   │   └── iSCSIPDUEncoding.cpp/h    # PDU encode/decode
│   ├── Kernel/           # Legacy kernel extension
│   │   ├── iSCSIVirtualHBA.cpp/h
│   │   ├── iSCSIInitiator.cpp
│   │   └── crc32c.c
│   ├── User/
│   │   ├── iSCSI Framework/  # Shared framework
│   │   ├── iscsid/           # Launch daemon
│   │   │   ├── iSCSIDaemon.c       # Main daemon logic
│   │   │   ├── iSCSISession.c      # Login & negotiation
│   │   │   ├── iSCSIHBAInterface.c # HBA abstraction (DEXT/KEXT)
│   │   │   ├── iSCSIPDURelay.c     # PDU relay engine
│   │   │   ├── iSCSITCPEngine.c    # TCP event engine
│   │   │   └── iSCSIDextIPC.c      # DEXT IPC communication
│   │   └── iscsictl/         # CLI management tool
│   └── Shared/               # Shared headers & types
├── Distribution/             # DMG packaging
└── README.md
```

### Bug Fixes Applied (2026-07-28/29)

This fork includes extensive code review and fixes for the following vulnerability classes found in the original codebase:

- **Buffer overflow (×5):** `UInt16`/`enum`-typed stack variables used with `kCFNumberCFIndexType` (8 bytes on arm64) in `iSCSITypes.c`
- **NULL pointer dereference (×8):** `CFDictionaryGetValue` / `CFStringCompare` / `CFStringCreateCopy` / `CFNumberGetValue` called with unvalidated NULL inputs in `iSCSITypes.c` and `iSCSIPreferences.c`
- **RPATH configuration:** `iscsid` binary was missing `LC_RPATH` causing dyld failures when loading `iSCSI.framework`

### License

As with the original project, this software is distributed under the GNU General Public License v2. See the original repository for full license terms.

### Acknowledgments

- Original project: [iscsi-osx/iSCSIInitiator](https://github.com/iscsi-osx/iSCSIInitiator)
- iSCSI RFC 3720 specification
- Apple DriverKit & IOKit documentation
- Modifications assisted by DeepSeek AI


---

# 中文版 (Chinese Version)

## 概述

iSCSI Initiator 是 macOS 平台的软件 iSCSI 启动器，允许 macOS 计算机连接到 iSCSI 目标（Target）。它自动检测并挂载逻辑单元（LUN），用户可在其上创建和挂载卷。有关 iSCSI 标准的更多信息，请参阅 IETF RFC3720。

本项目是基于 [iscsi-osx/iSCSIInitiator](https://github.com/iscsi-osx/iSCSIInitiator) 的修改版本，采用 **DriverKit 系统扩展（DEXT）** 的新架构进行重构，以摆脱现代 macOS 上已弃用的内核扩展（KEXT）模型。原项目的内核模式虚拟 HBA（`iSCSIVirtualHBA`）被替换为以下两层：

- **iscsid 守护进程**：在用户空间直接管理 TCP socket，处理登录、发现和认证
- **iSCSI.dext**：基于 `IOUserSCSIParallelInterfaceController` 的 DriverKit 扩展，负责 SCSI 命令传输

> ⚠️ **免责声明**：本项目借助 DeepSeek AI 辅助修改。虽然已经进行了全面的代码审查和漏洞修复，但仍可能存在未被发现的安全隐患或缺陷。在生产环境中使用请自行承担风险。

### 主要功能

- 支持 RFC 3720 标准的 iSCSI 启动器
- CHAP 认证（单向和双向）
- iSNS 和 SendTargets 目标发现
- 每个会话支持多个连接（MC/S）
- 头部和数据摘要支持（CRC32C）
- 通过偏好设置 plist 持久化目标配置
- 命令行管理工具（`iscsictl`）
- 通过 Disk Arbitration 自动挂载卷
- 支持 macOS 11+ 的 DriverKit 系统扩展（Apple Silicon 和 Intel）

### 架构

本项目引入了新的架构，以应对现代 macOS 上内核扩展被弃用的问题：

```
┌─────────────────────────────────────────┐
│  macOS SCSI 层 (IOKit)                  │
├─────────────────────────────────────────┤
│  iSCSI.dext (DriverKit)                 │
│  IOUserSCSIParallelInterfaceController  │
│  - SCSI CDB ↔ iSCSI PDU 转换            │
│  - 通过 IPC 与守护进程通信               │
├─────────────────────────────────────────┤
│  iscsid (启动守护进程)                   │
│  - TCP socket 管理（用户空间）           │
│  - 登录 / 发现 / 认证                   │
│  - PDU 中继 & TCP 引擎                  │
│  - CRC32C 摘要校验                      │
├─────────────────────────────────────────┤
│  iSCSI Framework (共享库)               │
│  - 偏好设置 / 配置管理                   │
│  - 钥匙串集成                           │
├─────────────────────────────────────────┤
│  iscsictl (命令行工具)                   │
│  - 目标 & 门户管理                      │
└─────────────────────────────────────────┘
```

**与原项目相比的关键架构变更：**

| 方面 | 原项目 (KEXT) | 本修改版 (DEXT) |
|------|--------------|-----------------|
| Socket 管理 | 内核 `kpi_socket` | 用户空间 BSD socket |
| SCSI 传输 | 内核 `IOSCSIParallelInterfaceController` | DriverKit `IOUserSCSIParallelInterfaceController` |
| 登录 / 认证 | 内核 + 用户空间混合 | 完全用户空间守护进程 |
| 数据路径 | 内核 PDU 编解码 | 用户空间 PDU 中继 + TCP 引擎 |

### 安装前提

由于内核扩展未签名，macOS 不会加载它们。因此，在安装和加载内核扩展之前，必须先禁用 kext 签名验证。此外，从 El Capitan 开始，新的安全措施阻止在受保护的系统文件夹中安装文件。因此，**在安装启动器之前**，必须按照适用的 macOS 版本执行相应步骤。

##### macOS 10.10 及更早版本

在终端执行：

    sudo nvram boot-args=kext-dev-mode=1

重启后内核将加载未签名的内核扩展。

##### macOS 10.11 及更高版本

在恢复模式的终端窗口中执行：

    csrutil disable

请参阅 [System Integrity Protection Guide](https://developer.apple.com/library/mac/documentation/Security/Conceptual/System_Integrity_Protection_Guide/KernelExtensions/KernelExtensions.html#//apple_ref/doc/uid/TP40016462-CH4-SW1) 了解如何进入恢复模式终端。此过程可能需要两次重启。

##### Apple Silicon / macOS 11+ 使用 DEXT（本修改版）

所有现代 macOS 版本（11+）均支持 DEXT。但 DEXT 需要使用 Apple Developer Program 证书进行代码签名。详见[已知问题](#已知问题)。KEXT 路径仅适用于 Intel Mac（构建产物为 x86_64）。

### 构建和安装

#### 从源码构建

```bash
# 构建所有组件（DEXT、Framework、守护进程、CLI 工具、KEXT）
cd Scripts && ./build.sh
```

也可以单独构建各个 Xcode scheme：

```bash
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iSCSI.framework build CODE_SIGNING_ALLOWED=NO
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iscsid build CODE_SIGNING_ALLOWED=NO
xcodebuild -workspace iSCSIInitiator.xcodeproj/project.xcworkspace -scheme iscsictl build CODE_SIGNING_ALLOWED=NO
```

#### 安装

```bash
# 安装所有组件到系统
cd Scripts && sudo ./install.sh
```

安装位置：
- `iSCSI.framework` → `/Library/Frameworks/`
- `iscsid` → `/usr/local/libexec/`
- `iscsictl` → `/usr/local/bin/`
- `iSCSI.dext` → `/Library/DriverExtensions/`（macOS 11+）
- `iSCSIInitiator.kext` → `/Library/Extensions/`（旧版）
- LaunchDaemon plist → `/Library/LaunchDaemons/`

#### 卸载

```bash
cd Scripts && sudo ./uninstall.sh
```

#### 构建发布版 DMG

```bash
cd Distribution && ./package.sh
```

### 使用方法

#### 管理守护进程

守护进程作为 LaunchDaemon（`com.github.iscsi-osx.iscsid`）运行，崩溃后通过 `KeepAlive` 自动重启。

```bash
# 检查守护进程状态
sudo launchctl list | grep iscsi

# 启动守护进程
sudo launchctl load /Library/LaunchDaemons/com.github.iscsi-osx.iscsid.plist

# 停止守护进程
sudo launchctl unload /Library/LaunchDaemons/com.github.iscsi-osx.iscsid.plist

# 查看守护进程日志
sudo cat /var/log/iscsid
```

#### 发现目标

```bash
# 添加发现门户
sudo iscsictl add discovery portal 192.168.28.30:3260

# 发现可用目标
sudo iscsictl discover targets
```

#### 目标配置

```bash
# 添加目标
sudo iscsictl add target iqn.2026-07.com.example:target-1

# 为目标设置 CHAP 认证
sudo iscsictl modify target iqn.2026-07.com.example:target-1 \
    set chap username initiator password mysecret123456

# 设置目标别名
sudo iscsictl modify target iqn.2026-07.com.example:target-1 \
    set alias "我的 iSCSI 目标"

# 查看已配置的目标
sudo iscsictl list targets
```

#### 登录和登出

```bash
# 登录到指定目标
sudo iscsictl login iqn.2026-07.com.example:target-1

# 登录到指定门户
sudo iscsictl login iqn.2026-07.com.example:target-1 \
    portal 192.168.28.30:3260

# 从目标登出
sudo iscsictl logout iqn.2026-07.com.example:target-1

# 从所有目标登出
sudo iscsictl logout all
```

#### 验证磁盘连接

```bash
# 登录成功后，验证磁盘是否可见
diskutil list

# 查看详细的 iSCSI 会话信息
sudo iscsictl list sessions
```

### 配置

目标配置持久化保存在 `/Library/Preferences/com.github.iscsi-osx.iSCSIInitiator.plist`。

可以通过以下命令配置启动器全局设置：

```bash
# 设置启动器别名（iSCSI 名称）
sudo iscsictl modify initiator set alias "my-initiator"

# 设置启动器 CHAP（用于双向认证）
sudo iscsictl modify initiator set chap username myname password mysecret

# 设置默认错误恢复级别
sudo iscsictl modify initiator set error-recovery 0
```

### 已知问题

#### 1. KEXT 仅支持 x86_64（macOS ≥ 26 + Apple Silicon 不可用）

内核扩展（`iSCSIInitiator.kext`）仅编译为 x86_64 架构。在 macOS 26+ 的 Apple Silicon（arm64）上，内核扩展已完全弃用，无论架构如何都无法加载。在 SIP 已禁用的 Intel Mac 上运行较旧 macOS 版本（10.x–12.x）时，KEXT 可能可以正常加载。

#### 2. DEXT 需要 Apple Developer Program 会员资格

DriverKit 扩展（`iSCSI.dext`）已实现并可成功为 arm64 架构构建。但是，DriverKit 系统扩展需要使用有效的 **Apple Developer Program** 证书（$99/年）进行代码签名。DriverKit SDK 明确拒绝了 ad-hoc 签名（`CODE_SIGN_IDENTITY=-`）：

```
error: Ad Hoc code signing is not allowed with SDK 'DriverKit 25.5'.
```

此外，DEXT 需要包含 `com.apple.developer.driverkit.userclient-access` 权限的 provisioning profile，该权限仅对付费开发者账号开放。没有付费账号的情况下，DEXT 无法被 macOS 加载，登录过程可以完成（iSCSI 协商成功），但 `diskutil list` 中不会出现磁盘。

**开发和测试的替代方案：** 使用 Intel Mac + macOS 10.x–12.x + x86_64 KEXT，或获取 Apple Developer Program 会员资格。

#### 3. macOS SIP 必须禁用

必须禁用系统完整性保护（SIP）才能加载未签名的内核扩展。无论使用 KEXT 还是 DEXT 路径，都需要降低安全策略才能加载开发签名的扩展：

```bash
# 在恢复模式的终端中执行：
csrutil disable
# 然后重启
```

### 项目结构

```
iSCSIInitiator/
├── Scripts/              # 构建、安装、卸载脚本
│   ├── build.sh
│   ├── install.sh
│   └── uninstall.sh
├── Source/
│   ├── DEXT/             # DriverKit 系统扩展
│   │   ├── iSCSIDextHBA.cpp/h   # 虚拟 HBA 实现
│   │   ├── iSCSIDextUserClient.cpp/h  # IPC 用户客户端
│   │   └── iSCSIPDUEncoding.cpp/h    # PDU 编解码
│   ├── Kernel/           # 旧版内核扩展
│   │   ├── iSCSIVirtualHBA.cpp/h
│   │   ├── iSCSIInitiator.cpp
│   │   └── crc32c.c
│   ├── User/
│   │   ├── iSCSI Framework/  # 共享框架
│   │   ├── iscsid/           # 启动守护进程
│   │   │   ├── iSCSIDaemon.c       # 守护进程主逻辑
│   │   │   ├── iSCSISession.c      # 登录和协商
│   │   │   ├── iSCSIHBAInterface.c # HBA 抽象层（DEXT/KEXT）
│   │   │   ├── iSCSIPDURelay.c     # PDU 中继引擎
│   │   │   ├── iSCSITCPEngine.c    # TCP 事件引擎
│   │   │   └── iSCSIDextIPC.c      # DEXT IPC 通信
│   │   └── iscsictl/         # CLI 管理工具
│   └── Shared/               # 共享头文件和类型定义
├── Distribution/             # DMG 打包
└── README.md
```

### 已修复的漏洞（2026-07-28/29）

本修改版对原始代码库中发现的以下漏洞类别进行了全面的代码审查和修复：

- **栈溢出（×5）：** `iSCSITypes.c` 中 `UInt16`/`enum` 类型的栈变量与 arm64 上 8 字节的 `kCFNumberCFIndexType` 配合使用时发生越界写入
- **空指针解引用（×8）：** `iSCSITypes.c` 和 `iSCSIPreferences.c` 中 `CFDictionaryGetValue` / `CFStringCompare` / `CFStringCreateCopy` / `CFNumberGetValue` 在未验证 NULL 输入的情况下被调用
- **RPATH 配置错误：** `iscsid` 二进制文件缺少 `LC_RPATH`，导致 dyld 加载 `iSCSI.framework` 时失败

### 许可证

与原项目一致，本软件基于 GNU General Public License v2 分发。完整许可条款请参见原始仓库。

### 致谢

- 原始项目：[iscsi-osx/iSCSIInitiator](https://github.com/iscsi-osx/iSCSIInitiator)
- iSCSI RFC 3720 规范
- Apple DriverKit 与 IOKit 文档
- 修改过程中使用了 DeepSeek AI 辅助
