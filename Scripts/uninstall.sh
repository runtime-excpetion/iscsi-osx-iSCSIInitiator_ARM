#! /bin/bash

# Define targets
DAEMON=iscsid
TOOL=iscsictl
KEXT=iSCSIInitiator.kext
DEXT=iSCSI.dext
FRAMEWORK=iSCSI.framework
DAEMON_PLIST=com.github.iscsi-osx.iscsid.plist
DAEMON_PLIST_NOEXT=com.github.iscsi-osx.iscsid
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

# Minor version of OS X Mavericks
OSX_MAVERICKS_MINOR_VER="9"

if [ "$OSX_MINOR_VER" -gt "$OSX_MAVERICKS_MINOR_VER" ]; then
    KEXT_DST=/Library/Extensions
else
    KEXT_DST=/System/Library/Extensions
fi

# Stop, unload and remove launch daemon
sudo launchctl stop $DAEMON_PLIST_NOEXT 2>/dev/null || true

if [ -f "$DAEMON_PLIST_DST/$DAEMON_PLIST" ]; then
    sudo launchctl unload "$DAEMON_PLIST_DST/$DAEMON_PLIST"
fi

sudo rm -f "$DAEMON_PLIST_DST/$DAEMON_PLIST"
sudo rm -f /usr/sbin/$DAEMON # Old location
sudo rm -f /System/Library/LaunchDaemons/$DAEMON_PLIST # Old location
sudo rm -f "$DAEMON_DST/$DAEMON"

# Unload & remove kernel extension (legacy)
if [ -d "$KEXT_DST/$KEXT" ]; then
    sudo kextunload "$KEXT_DST/$KEXT" 2>/dev/null || true
fi
sudo rm -rf "$KEXT_DST/$KEXT"

# Unregister & remove DEXT (modern macOS)
DEXT_BUNDLE_ID="com.github.iscsi-osx.iSCSIInitiator.dext"
echo "Unregistering System Extension ($DEXT_BUNDLE_ID)..."
sudo systemextensionsctl reset 2>/dev/null || true
sudo rm -rf "$DEXT_DST/$DEXT"

# Remove user tools
sudo rm -f /usr/bin/$TOOL # Old location
sudo rm -f "$TOOL_DST/$TOOL"

# Remove framework
sudo rm -Rf "$FRAMEWORK_DST/$FRAMEWORK"

# Remove man pages
sudo rm -f "$MAN_DST/$MAN_DAEMON"
sudo rm -f "$MAN_DST/$MAN_TOOL"

# Remove configuration file
sudo rm -f "$PREF_DST/$PREF_FILE"

# Forget package receipt
PKG_RSP="$(pkgutil --pkgs=com.github.iscsi-osx.iSCSIInitiator 2>/dev/null)"
if [ "$PKG_RSP" == "com.github.iscsi-osx.iSCSIInitiator" ]; then
    sudo pkgutil --forget com.github.iscsi-osx.iSCSIInitiator
fi

# Flush preferences cache
sudo killall cfprefsd 2>/dev/null || true

echo ""
echo "=== Uninstall Complete ==="
