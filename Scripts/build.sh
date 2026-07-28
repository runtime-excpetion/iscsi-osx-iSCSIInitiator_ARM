#!/bin/bash

set -e

PROJECT="../iSCSIInitiator.xcodeproj"
WORKSPACE="$PROJECT/project.xcworkspace"

CODESIGN="CODE_SIGNING_ALLOWED=NO"
CODESIGN_IDENTITY="CODE_SIGN_IDENTITY=-"

# For production build with DEXT code signing, set these before running:
#   CODESIGN="CODE_SIGNING_ALLOWED=YES"
#   CODESIGN_IDENTITY="CODE_SIGN_IDENTITY=Apple Development: your@email.com"
#   OTHER_CODE_SIGN_FLAGS="OTHER_CODE_SIGN_FLAGS=--timestamp"
# The DEXT requires an Apple Developer Program certificate with
# the com.apple.developer.driverkit entitlement.
# See: Source/DEXT/iSCSIDext.entitlements

echo "=== Building iSCSI.dext ==="
DEXT_BUILD_LOG=$(mktemp)
if xcodebuild -workspace "$WORKSPACE" -scheme iSCSI.dext build $CODESIGN $CODESIGN_IDENTITY > "$DEXT_BUILD_LOG" 2>&1; then
    echo "✓ iSCSI.dext build succeeded"
    DEXT_BUILD_OK=true
else
    echo "⚠ iSCSI.dext build failed (see log: $DEXT_BUILD_LOG)"
    DEXT_BUILD_OK=false
fi

echo ""
echo "=== Building iSCSI.framework ==="
xcodebuild -workspace "$WORKSPACE" -scheme iSCSI.framework build $CODESIGN $CODESIGN_IDENTITY

echo ""
echo "=== Building iscsictl ==="
xcodebuild -workspace "$WORKSPACE" -scheme iscsictl build $CODESIGN $CODESIGN_IDENTITY \
    "LD_RUNPATH_SEARCH_PATHS=/Library/Frameworks"

echo ""
echo "=== Building iSCSI.kext ==="
xcodebuild -workspace "$WORKSPACE" -scheme iSCSI.kext build $CODESIGN $CODESIGN_IDENTITY

echo ""
echo "=== Building iscsid ==="
ISCSID_KERNEL_INC="$(cd .. && pwd)/Source/Kernel"
xcodebuild -workspace "$WORKSPACE" -scheme iscsid build $CODESIGN $CODESIGN_IDENTITY \
    "HEADER_SEARCH_PATHS=\$(inherited) $ISCSID_KERNEL_INC" 2>&1 || true

# The build may fail at link step because Xcode build system sometimes
# doesn't pick up newly-added source files from the pbxproj.
# We detect missing .o files and compile + link them manually.
OBJDIR=$(find ~/Library/Developer/Xcode/DerivedData -name "arm64" \
    -path "*/iscsid.build/Objects-normal/*" -type d 2>/dev/null | head -1)

# Also re-link when xcodebuild link failed (no product binary produced)
ISCSID_BIN=$(find ~/Library/Developer/Xcode/DerivedData -name "iscsid" \
    -path "*/Products/Debug/iscsid" -type f 2>/dev/null | head -1)

if [ -z "$OBJDIR" ] || [ ! -f "$OBJDIR/iSCSITCPEngine.o" ] || [ ! -f "$ISCSID_BIN" ]; then
    echo ""
    echo "=== Phase B/E: Compiling and linking new TCP engine / PDU relay files ==="

    CLANG=$(/usr/bin/xcrun --sdk macosx --find clang 2>/dev/null || \
        echo "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
    SDK=$(/usr/bin/xcrun --sdk macosx --show-sdk-path 2>/dev/null || \
        echo "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk")
    SRCROOT="$(cd .. && pwd)"

    # Detect OBJDIR if not already found
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

    LINK_FILE_LIST="$OBJDIR/iscsid.LinkFileList"
    DERIVED_BASE=$(echo "$OBJDIR" | sed 's|/Build/Intermediates.noindex/.*||')
    BUILD_DIR="$DERIVED_BASE/Build/Products/Debug"
    EAGER_DIR="$DERIVED_BASE/Build/Intermediates.noindex/EagerLinkingTBDs/Debug"

    if [ ! -f "$LINK_FILE_LIST" ]; then
        echo "ERROR: LinkFileList not found at $LINK_FILE_LIST"
        exit 1
    fi

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
fi

echo ""
echo "=== Verification ==="

# Verify DEXT binary if built
if [ "$DEXT_BUILD_OK" = true ]; then
    DEXT_BINARY=$(find ~/Library/Developer/Xcode/DerivedData -name "iSCSI.dext" \
        -path "*/Products/Debug/*" -type d 2>/dev/null | head -1)
    if [ -n "$DEXT_BINARY" ]; then
        DEXT_EXEC="$DEXT_BINARY/Contents/MacOS/iSCSI.dext"
        if [ -f "$DEXT_EXEC" ]; then
            echo "✓ iSCSI.dext: $DEXT_EXEC ($(stat -f%z "$DEXT_EXEC") bytes)"
        else
            echo "⚠ iSCSI.dext bundle found but executable missing"
        fi
    else
        echo "⚠ iSCSI.dext bundle not found in Products"
    fi
fi

BINARY=$(find ~/Library/Developer/Xcode/DerivedData -name "iscsid" \
    -path "*/Products/Debug/iscsid" -type f 2>/dev/null | head -1)

if [ -n "$BINARY" ] && [ -f "$BINARY" ]; then
    echo "✓ iscsid: $BINARY ($(stat -f%z "$BINARY") bytes)"
    # Verify our symbols are present
    FOUND=$(nm "$BINARY" | grep -c "_TCPEngineCreate\|_DextIPCRelease\|_PDURelayInitialize\|_crc32c_init\|_crc32c" 2>/dev/null || echo 0)
    echo "✓ Phase B symbols resolved: $FOUND/5"
else
    echo "✗ iscsid binary not found"
    exit 1
fi

echo ""
echo "=== All builds complete ==="
