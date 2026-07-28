#! /bin/bash

# Define targets
DAEMON=iscsid
TOOL=iscsictl
KEXT=iSCSIInitiator.kext
DEXT=iSCSI.dext
FRAMEWORK=iSCSI.framework
DAEMON_PLIST=com.github.iscsi-osx.iscsid.plist
MAN_TOOL=iscsictl.8
MAN_DAEMON=iscsid.8

# Define install path
DAEMON_DST=/usr/local/libexec
DAEMON_PLIST_DST=/Library/LaunchDaemons
DEXT_DST=/Library/DriverExtensions
FRAMEWORK_DST=/Library/Frameworks
TOOL_DST=/usr/local/bin
MAN_DST=/usr/share/man/man8
PREF_DST=/Library/Preferences
PREF_FILE=com.github.iscsi-osx.iSCSIInitiator.plist

# Get major version of the OS
OSX_MAJOR_VER=$(sw_vers -productVersion | awk -F '.' '{print $1}')
OSX_MINOR_VER=$(sw_vers -productVersion | awk -F '.' '{print $2}')
OS_VERSION_MAJOR=$((${OSX_MAJOR_VER} * 10000 + ${OSX_MINOR_VER}))
# macOS 11 (Big Sur) = major 20 or 11 depending on sw_vers output
IS_MODERN_MACOS=0
if [ "$OSX_MAJOR_VER" -ge 11 ] || [ "$OS_VERSION_MAJOR" -ge 200000 ]; then
    IS_MODERN_MACOS=1
fi

# Minor version of OS X Mavericks
OSX_MAVERICKS_MINOR_VER="9"

if [ "$OSX_MINOR_VER" -gt "$OSX_MAVERICKS_MINOR_VER" ]; then
    KEXT_DST=/Library/Extensions
else
    KEXT_DST=/System/Library/Extensions
fi

# Look for build products in places Xcode might place them.
for BUILD_PATH in \
            ../DerivedData/Build/Products/Debug \
            ../DerivedData/iSCSIInitiator/Build/Products/Debug \
            ~/Library/Developer/Xcode/DerivedData/iSCSIInitiator*/Build/Products/Debug \
            ; do
    if [ -d "${BUILD_PATH}" ]; then
        SOURCE_PATH="${BUILD_PATH}"
        break;
    fi
done

if [ X"" == X"${SOURCE_PATH}" ]; then
    echo "Unable to locate iSCSIInitiator binaries; did you run build.sh without errors?"
    exit 1
fi

# Copy kernel extension & load it (legacy — fails on macOS 11+ / Apple Silicon)
sudo mkdir -p $KEXT_DST
sudo cp -R $SOURCE_PATH/$KEXT $KEXT_DST/$KEXT
sudo chmod -R 755 $KEXT_DST/$KEXT
sudo chown -R root:wheel $KEXT_DST/$KEXT

# Copy & register DEXT (modern macOS — replaces kext)
sudo mkdir -p $DEXT_DST
sudo rm -rf $DEXT_DST/$DEXT
sudo cp -R $SOURCE_PATH/$DEXT $DEXT_DST/$DEXT
sudo chmod -R 755 $DEXT_DST/$DEXT
sudo chown -R root:wheel $DEXT_DST/$DEXT

if [ "$IS_MODERN_MACOS" -eq 1 ]; then
    echo "Registering System Extension (iSCSI.dext)..."
    sudo systemextensionsctl reset 2>/dev/null || true
    # Trigger user approval dialog via systemextensionsctl
    sudo systemextensionsctl developer mode 2>/dev/null || true
    echo "  DEXT installed to $DEXT_DST/$DEXT"
    echo "  IMPORTANT: On macOS 11+, approve the extension in"
    echo "  System Settings → Privacy & Security → Security"
fi

# Copy framework
sudo mkdir -p $FRAMEWORK_DST
sudo cp -R $SOURCE_PATH/$FRAMEWORK $FRAMEWORK_DST/$FRAMEWORK
sudo chown -R root:wheel $FRAMEWORK_DST/$FRAMEWORK
sudo chmod -R 755 $FRAMEWORK_DST/$FRAMEWORK

# Copy daemon & set permissions
sudo rm -f /var/logs/iscsid.log
sudo mkdir -p $DAEMON_DST
sudo cp $SOURCE_PATH/$DAEMON $DAEMON_DST/$DAEMON
sudo cp $SOURCE_PATH/$DAEMON_PLIST $DAEMON_PLIST_DST
sudo chmod -R 744 $DAEMON_DST/$DAEMON
sudo chown -R root:wheel $DAEMON_DST/$DAEMON
sudo chmod 644 $DAEMON_PLIST_DST/$DAEMON_PLIST

# Set rpath so iscsid can find iSCSI.framework
sudo install_name_tool -add_rpath /Library/Frameworks $DAEMON_DST/$DAEMON 2>/dev/null || true
sudo chown root:wheel $DAEMON_PLIST_DST/$DAEMON_PLIST

# Copy user tool
sudo mkdir -p $TOOL_DST
sudo cp $SOURCE_PATH/$TOOL $TOOL_DST/$TOOL
sudo chmod +x $TOOL_DST/$TOOL

# Set rpath so iscsictl can find iSCSI.framework in /Library/Frameworks
sudo install_name_tool -add_rpath /Library/Frameworks $TOOL_DST/$TOOL 2>/dev/null || true

# Copy man page
sudo mkdir -p $MAN_DST
sudo cp $SOURCE_PATH/$MAN_TOOL $MAN_DST
sudo cp $SOURCE_PATH/$MAN_DAEMON $MAN_DST

# Try to load kernel extension (legacy — fails on macOS 10.15+ / Apple Silicon)
if [ "$IS_MODERN_MACOS" -eq 0 ]; then
    sudo kextload $KEXT_DST/$KEXT 2>/dev/null || \
        echo "Warning: could not load kernel extension (expected on macOS 10.15+)"
fi

# Start daemon
sudo launchctl load $DAEMON_PLIST_DST/$DAEMON_PLIST 2>/dev/null || true
sudo launchctl start $DAEMON_PLIST 2>/dev/null || true

# Remove (old) configuration file
sudo rm -f $PREF_DST/$PREF_FILE

# Flush preferences cache
sudo killall cfprefsd

echo ""
echo "=== Installation Summary ==="
echo "Framework: $FRAMEWORK_DST/$FRAMEWORK"
echo "Daemon:    $DAEMON_DST/$DAEMON"
echo "CLI Tool:  $TOOL_DST/$TOOL"
if [ "$IS_MODERN_MACOS" -eq 1 ]; then
    echo "DEXT:      $DEXT_DST/$DEXT"
else
    echo "KEXT:      $KEXT_DST/$KEXT"
fi
