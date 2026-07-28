# Package parameters
NAME="iSCSI Initiator for macOS"
BUNDLE_ID="com.github.iscsi-osx.iSCSIInitiator"
VERSION="1.0.0-beta8"

# Output of final DMG
RELEASE="../Release"

# DMG parameters
DMG_BASE_NAME="iSCSIInitiator"
DMG_SIZE=15000

# XCode temporary build path for release binaries
XCODE_RELEASE_BUILD_DIR="tmp"

# Temporary path with package and DMG components
TMP_ROOT="../tmp"
TMP_PACKAGE_DIR="../tmp/Packages"

# Kernel extension path (built)
KEXT_PATH=../$XCODE_RELEASE_BUILD_DIR/Release/iSCSIInitiator.kext
DAEMON_PATH=../$XCODE_RELEASE_BUILD_DIR/Release/iscsid
TOOL_PATH=../$XCODE_RELEASE_BUILD_DIR/Release/iscsictl
DEXT_PATH=../$XCODE_RELEASE_BUILD_DIR/Release/iSCSI.dext

# Location of installer and uninstaller scripts
INSTALLER_SCRIPT="Scripts/Installer"
UNINSTALLER_SCRIPT="Scripts/Uninstaller"

# Dialog title for installer and uninstaller scripts
INSTALLER_TITLE="iSCSI Initiator Installer"
UNINSTALLER_TITLE="iSCSI Initiator Uninstaller"

# Output path of intaller and uninstaller packages
INSTALLER_PATH="../tmp/Packages/Installer.pkg"
UNINSTALLER_PATH="../tmp/Packages/Uninstaller.pkg"

# Path of installer and uninstaller distribution XML files
INSTALLER_DIST_XML="Resources/Installer.xml"
UNINSTALLER_DIST_XML="Resources/Uninstaller.xml"

# Requirements
REQUIREMENTS_PATH="Resources/Requirements.plist"

SRCROOT="$(cd .. && pwd)"

# Release build of all components
echo "=== Building iSCSI.framework ==="
xcodebuild -workspace ../iSCSIInitiator.xcodeproj/project.xcworkspace \
            -scheme iSCSI.framework -configuration release BUILD_DIR=$XCODE_RELEASE_BUILD_DIR

echo ""
echo "=== Building iSCSI.dext ==="
xcodebuild -workspace ../iSCSIInitiator.xcodeproj/project.xcworkspace \
           -scheme iSCSI.dext -configuration release BUILD_DIR=$XCODE_RELEASE_BUILD_DIR \
           CODE_SIGNING_ALLOWED=NO 2>&1 || echo "Warning: DEXT build may require code signing"

echo ""
echo "=== Building iSCSI.kext ==="
xcodebuild -workspace ../iSCSIInitiator.xcodeproj/project.xcworkspace \
           -scheme iSCSI.kext -configuration release BUILD_DIR=$XCODE_RELEASE_BUILD_DIR

echo ""
echo "=== Building iscsid ==="
ISCSID_KERNEL_INC="$SRCROOT/Source/Kernel"
xcodebuild -workspace ../iSCSIInitiator.xcodeproj/project.xcworkspace \
           -scheme iscsid -configuration release BUILD_DIR=$XCODE_RELEASE_BUILD_DIR \
           "HEADER_SEARCH_PATHS=\$(inherited) $ISCSID_KERNEL_INC" 2>&1 || true

# The build may fail at link step because Xcode project doesn't include
# iSCSITCPEngine.c, iSCSIDextIPC.c, iSCSIPDURelay.c, crc32c.c in the
# iscsid target. Detect and handle this by manually compiling and linking.
OBJDIR=$(find ~/Library/Developer/Xcode/DerivedData -name "arm64" \
    -path "*/iscsid.build/Objects-normal/*" -type d 2>/dev/null | head -1)

ISCSID_BIN="../$XCODE_RELEASE_BUILD_DIR/Release/iscsid"

if [ -z "$OBJDIR" ] || [ ! -f "$OBJDIR/iSCSITCPEngine.o" ] || [ ! -f "$ISCSID_BIN" ]; then
    echo ""
    echo "=== Phase B/E: Compiling and linking new TCP engine / PDU relay files ==="

    CLANG=$(/usr/bin/xcrun --sdk macosx --find clang 2>/dev/null || \
        echo "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
    SDK=$(/usr/bin/xcrun --sdk macosx --show-sdk-path 2>/dev/null || \
        echo "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk")

    # Re-detect OBJDIR if xcodebuild ran above
    if [ -z "$OBJDIR" ]; then
        OBJDIR=$(find ~/Library/Developer/Xcode/DerivedData -name "arm64" \
            -path "*/iscsid.build/Objects-normal/*" -type d 2>/dev/null | head -1)
    fi

    if [ -z "$OBJDIR" ]; then
        echo "ERROR: Cannot find object directory for iscsid. Did xcodebuild run?"
        exit 1
    fi

    for f in iSCSITCPEngine iSCSIDextIPC iSCSIPDURelay crc32c; do
        echo "  Compiling $f.c..."
        if [ "$f" = "crc32c" ]; then
            SRC_FILE="$SRCROOT/Source/Kernel/crc32c.c"
        else
            SRC_FILE="$SRCROOT/Source/User/iscsid/$f.c"
        fi
        "$CLANG" -c \
            -target arm64-apple-macos10.13 \
            -isysroot "$SDK" \
            -O0 \
            -I"$SRCROOT/Source/User/iscsid" \
            -I"$SRCROOT/Source/User/iSCSI Framework" \
            -I"$SRCROOT/Source/Shared/DextDaemonIPC" \
            -I"$SRCROOT/Source/Kernel" \
            -fobjc-arc \
            "$SRC_FILE" \
            -o "$OBJDIR/${f}.o"
    done

    echo ""
    echo "=== Re-linking iscsid with new object files ==="

    DERIVED_BASE=$(echo "$OBJDIR" | sed 's|/Build/Intermediates.noindex/.*||')
    # xcodebuild was called with BUILD_DIR=tmp, products are at tmp/Release/
    BUILD_DIR="$SRCROOT/$XCODE_RELEASE_BUILD_DIR/Release"
    EAGER_DIR="$DERIVED_BASE/Build/Intermediates.noindex/EagerLinkingTBDs/Release"

    LINK_FILE_LIST="$OBJDIR/iscsid.LinkFileList"

    if [ ! -f "$LINK_FILE_LIST" ]; then
        echo "ERROR: LinkFileList not found at $LINK_FILE_LIST"
        exit 1
    fi

    # Link to DerivedData product dir first
    "$CLANG" \
        -Xlinker -reproducible \
        -target arm64-apple-macos10.13 \
        -isysroot "$SDK" \
        -O0 \
        -L"$BUILD_DIR" \
        -L"$EAGER_DIR" \
        -F"$BUILD_DIR" \
        -F"$EAGER_DIR" \
        @"$LINK_FILE_LIST" \
        "$OBJDIR/iSCSITCPEngine.o" \
        "$OBJDIR/iSCSIDextIPC.o" \
        "$OBJDIR/iSCSIPDURelay.o" \
        "$OBJDIR/crc32c.o" \
        -Xlinker -rpath -Xlinker /Library/Frameworks \
        -rdynamic \
        -Xlinker -no_deduplicate \
        -fobjc-arc \
        -fobjc-link-runtime \
        -framework SystemConfiguration \
        -framework iSCSI \
        -framework Security \
        -framework DiskArbitration \
        -framework CoreFoundation \
        -framework IOKit \
        -o "$BUILD_DIR/iscsid"

    # Copy to package BUILD_DIR
    cp "$BUILD_DIR/iscsid" "../$XCODE_RELEASE_BUILD_DIR/Release/iscsid"
fi

echo ""
echo "=== Building iscsictl ==="
xcodebuild -workspace ../iSCSIInitiator.xcodeproj/project.xcworkspace \
            -scheme iscsictl -configuration release BUILD_DIR=$XCODE_RELEASE_BUILD_DIR \
            "LD_RUNPATH_SEARCH_PATHS=/Library/Frameworks"

# Verify all required binaries exist
echo ""
echo "=== Verification ==="
MISSING=0
for bin in "$DAEMON_PATH" "$TOOL_PATH" "$KEXT_PATH"; do
    if [ -e "$bin" ]; then
        echo "  ✓ $(basename $bin)"
    else
        echo "  ✗ $bin missing"
        MISSING=$((MISSING+1))
    fi
done

# DEXT may not produce .dext bundle without signing; check for executable
if [ -d "$DEXT_PATH" ]; then
    echo "  ✓ iSCSI.dext"
elif [ -f "$DEXT_PATH/Contents/MacOS/iSCSI.dext" ]; then
    echo "  ✓ iSCSI.dext (executable)"
else
    echo "  ⚠ iSCSI.dext not found (expected without code signing)"
fi

if [ $MISSING -gt 0 ]; then
    echo "ERROR: $MISSING required binary(s) missing, aborting package"
    exit 1
fi

echo ""
echo "=== Packaging ==="

# Create folder for pkg output (.pkg files for DMG)
mkdir -p $TMP_PACKAGE_DIR
mkdir -p $RELEASE

# Package the installer
pkgbuild --root ../$XCODE_RELEASE_BUILD_DIR/Release \
    --identifier $BUNDLE_ID \
    --install-location /tmp/ \
    --scripts $INSTALLER_SCRIPT \
    --version $VERSION \
    $INSTALLER_PATH.tmp

# Package the uninstaller
pkgbuild --nopayload \
    --identifier $BUNDLE_ID \
    --scripts $UNINSTALLER_SCRIPT \
    --version $VERSION \
    $UNINSTALLER_PATH.tmp


# Put packages inside a product archive
productbuild --distribution $INSTALLER_DIST_XML \
--package-path $TMP_PACKAGE_DIR \
--product $REQUIREMENTS_PATH \
$INSTALLER_PATH

productbuild --distribution $UNINSTALLER_DIST_XML \
--package-path $TMP_PACKAGE_DIR \
--product $REQUIREMENTS_PATH \
$UNINSTALLER_PATH

# Cleanup temporary packages, leaving final pacakges for DMG
rm $INSTALLER_PATH.tmp
rm $UNINSTALLER_PATH.tmp

# Build the DMG
hdiutil create -srcfolder $TMP_PACKAGE_DIR -volname "$NAME" -fs HFS+ \
-fsargs "-c c=64,a=16,e=16" -format UDRW -size ${DMG_SIZE}k $TMP_ROOT/$DMG_BASE_NAME.dmg

# Load the DMG
device=$(hdiutil attach -readwrite -noverify -noautoopen $TMP_ROOT/$DMG_BASE_NAME.dmg | \
egrep '^/dev/' | sed 1q | awk '{print $1}')

sleep 2

# Modify DMG style
echo '
tell application "Finder"
tell disk "'${NAME}'"
open
set current view of container window to icon view
set toolbar visible of container window to false
set statusbar visible of container window to false
set the bounds of container window to {400, 100, 885, 430}
set theViewOptions to the icon view options of container window
set arrangement of theViewOptions to not arranged
set icon size of theViewOptions to 72
update without registering applications
delay 5
close
end tell
end tell
' | osascript

# Set permissions & compress
chmod -Rf go-w /Volumes/"${NAME}"
sync
sync

hdiutil detach -force ${device} 2>/dev/null || hdiutil detach ${device} 2>/dev/null || \
    echo "Warning: could not detach ${device}, will try convert anyway"

# Create final output
rm -f $RELEASE/$DMG_BASE_NAME-$VERSION.dmg
hdiutil convert $TMP_ROOT/$DMG_BASE_NAME.dmg -format UDZO -imagekey zlib-level=9 -o $RELEASE/$DMG_BASE_NAME-$VERSION.dmg

# Cleanup
rm -r $TMP_ROOT

echo ""
echo "=== Package complete: $RELEASE/$DMG_BASE_NAME-$VERSION.dmg ==="
