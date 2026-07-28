#!/usr/bin/env python3
"""
Add iSCSI.dext target to the Xcode project (iSCSIInitiator.xcodeproj).
Reads the existing pbxproj, adds DEXT target + build phases + configs,
and writes back.
"""

import plistlib
import secrets
import sys
import os

PBXPROJ = os.path.join(os.path.dirname(__file__), '..', 'iSCSIInitiator.xcodeproj', 'project.pbxproj')
SRCROOT = 'Source/DEXT'

def new_id():
    """Generate a 24-char uppercase hex ID matching Xcode conventions."""
    return secrets.token_hex(12).upper()


def main():
    with open(PBXPROJ, 'rb') as f:
        proj = plistlib.load(f)

    objects = proj['objects']
    root = objects[proj['rootObject']]
    main_group = objects[root['mainGroup']]

    # Check if DEXT target already exists
    for tid in root.get('targets', []):
        tgt = objects.get(tid, {})
        if tgt.get('name') == 'iSCSI.dext':
            print("iSCSI.dext target already exists, skipping.")
            return

    # Check if DEXT group already exists
    dext_group_id = None
    for child_id in main_group.get('children', []):
        child = objects.get(child_id, {})
        if child.get('path') == 'Source/DEXT' or child.get('name') == 'DEXT':
            dext_group_id = child_id
            break

    # ── File References ──────────────────────────────────────────────
    # Source files (to be compiled)
    FILE_REFS = {
        'iSCSIDextHBA.iig':  {'isa': 'PBXFileReference', 'path': 'iSCSIDextHBA.iig',      'sourceTree': '<group>'},
        'iSCSIDextHBA.cpp':  {'isa': 'PBXFileReference', 'path': 'iSCSIDextHBA.cpp',      'sourceTree': '<group>'},
        'iSCSIPDUEncoding.h':{'isa': 'PBXFileReference', 'path': 'iSCSIPDUEncoding.h',    'sourceTree': '<group>'},
        'iSCSIPDUEncoding.cpp':{'isa':'PBXFileReference', 'path': 'iSCSIPDUEncoding.cpp',  'sourceTree': '<group>'},
        'Info.plist':        {'isa': 'PBXFileReference', 'path': 'Info.plist',             'sourceTree': '<group>',
                              'lastKnownFileType': 'text.plist.xml', 'explicitFileType': 'text.plist.xml'},
        'iSCSIDext.entitlements': {'isa': 'PBXFileReference', 'path': 'iSCSIDext.entitlements', 'sourceTree': '<group>',
                                   'lastKnownFileType': 'text.xml'},
    }

    ref_ids = {}
    for name, ref in FILE_REFS.items():
        rid = new_id()
        ref_ids[name] = rid
        objects[rid] = ref

    # Product reference
    product_id = new_id()
    objects[product_id] = {
        'isa': 'PBXFileReference',
        'path': 'iSCSI.dext',
        'sourceTree': 'BUILT_PRODUCTS_DIR',
        'explicitFileType': 'wrapper.system-extension',
    }

    # ── Build Files ──────────────────────────────────────────────────
    BUILD_FILES = {
        'iSCSIDextHBA.iig': {},
        'iSCSIDextHBA.cpp': {},
        'iSCSIPDUEncoding.cpp': {},
    }

    bf_ids = {}
    for name, _ in BUILD_FILES.items():
        bid = new_id()
        bf_ids[name] = bid
        objects[bid] = {
            'isa': 'PBXBuildFile',
            'fileRef': ref_ids[name],
        }

    # ── Group ─────────────────────────────────────────────────────────
    if dext_group_id is None:
        dext_group_id = new_id()
        objects[dext_group_id] = {
            'isa': 'PBXGroup',
            'children': sorted(ref_ids.values()),
            'name': 'DEXT',
            'sourceTree': '<group>',
        }
        main_group.setdefault('children', []).append(dext_group_id)
    else:
        existing = set(objects[dext_group_id].get('children', []))
        for rid in ref_ids.values():
            if rid not in existing:
                objects[dext_group_id].setdefault('children', []).append(rid)

    # ── Build Phases ──────────────────────────────────────────────────
    # Sources
    source_files_ids = [bf_ids['iSCSIDextHBA.iig'], bf_ids['iSCSIDextHBA.cpp'], bf_ids['iSCSIPDUEncoding.cpp']]
    sources_phase_id = new_id()
    objects[sources_phase_id] = {
        'isa': 'PBXSourcesBuildPhase',
        'buildActionMask': 2147483647,
        'files': sorted(source_files_ids),
        'runOnlyForDeploymentPostprocessing': 0,
    }

    # Frameworks (SCSIControllerDriverKit)
    # For DriverKit, frameworks are specified via OSBundleLibraries in Info.plist,
    # but Xcode needs a link phase. We create an empty frameworks phase.
    frameworks_phase_id = new_id()
    objects[frameworks_phase_id] = {
        'isa': 'PBXFrameworksBuildPhase',
        'buildActionMask': 2147483647,
        'files': [],
        'runOnlyForDeploymentPostprocessing': 0,
    }

    # ── Build Configurations ──────────────────────────────────────────
    common_settings = {
        'ALWAYS_SEARCH_USER_PATHS': 'NO',
        'CLANG_ENABLE_MODULES': 'YES',
        'CODE_SIGNING_ALLOWED': 'NO',
        'CODE_SIGN_IDENTITY': '-',
        'COPY_PHASE_STRIP': 'NO',
        'DEBUG_INFORMATION_FORMAT': 'dwarf',
        'ENABLE_STRICT_OBJC_MSGSEND': 'YES',
        'ENABLE_TESTABILITY': 'YES',
        'GCC_C_LANGUAGE_STANDARD': 'compiler-default',
        'GCC_DYNAMIC_NO_PIC': 'NO',
        'GCC_OPTIMIZATION_LEVEL': '0',
        'GCC_PREPROCESSOR_DEFINITIONS': ['DEBUG=1', '$(inherited)'],
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'NO',
        'IPHONEOS_DEPLOYMENT_TARGET': '',
        'MACH_O_TYPE': 'mh_bundle',
        'MODULES': 'YES',
        'ONLY_ACTIVE_ARCH': 'YES',
        'PRODUCT_BUNDLE_IDENTIFIER': 'com.github.iscsi-osx.iSCSIInitiator.dext',
        'PRODUCT_NAME': 'iSCSI.dext',
        'PROVISIONING_PROFILE_SPECIFIER': '',
        'SDKROOT': 'driverkit',
        'SKIP_INSTALL': 'YES',
        'SUPPORTED_PLATFORMS': 'driverkit',
        'TARGETED_DEVICE_FAMILY': '',
        'WATCHOS_DEPLOYMENT_TARGET': '',
    }
    # Note: IIG files need special handling. The DriverKit IIG compiler
    # (iig) processes .iig files into .h and _impl_gen.cpp.
    # We configure Xcode to use the IIG toolchain for .iig files.

    # Remove macOS-specific settings
    for key in ['MACOSX_DEPLOYMENT_TARGET', 'DYLIB_CURRENT_VERSION',
                'GCC_WARN_64_TO_32_BIT_CONVERSION', 'CLANG_WARN_BOOL_CONVERSION']:
        common_settings.pop(key, None)

    debug_config_id = new_id()
    objects[debug_config_id] = {
        'isa': 'XCBuildConfiguration',
        'name': 'Debug',
        'buildSettings': dict(common_settings),
    }

    release_settings = dict(common_settings)
    release_settings['COPY_PHASE_STRIP'] = 'YES'
    release_settings['DEBUG_INFORMATION_FORMAT'] = 'dwarf-with-dsym'
    release_settings['GCC_OPTIMIZATION_LEVEL'] = 's'
    release_settings.pop('GCC_DYNAMIC_NO_PIC', None)
    release_settings.pop('GCC_SYMBOLS_PRIVATE_EXTERN', None)
    release_settings.pop('GCC_PREPROCESSOR_DEFINITIONS', None)
    release_settings.pop('ENABLE_TESTABILITY', None)

    release_config_id = new_id()
    objects[release_config_id] = {
        'isa': 'XCBuildConfiguration',
        'name': 'Release',
        'buildSettings': release_settings,
    }

    # Configuration list
    config_list_id = new_id()
    objects[config_list_id] = {
        'isa': 'XCConfigurationList',
        'buildConfigurations': [debug_config_id, release_config_id],
        'defaultConfigurationIsVisible': 0,
        'defaultConfigurationName': 'Release',
    }

    # ── Target ────────────────────────────────────────────────────────
    target_id = new_id()
    objects[target_id] = {
        'isa': 'PBXNativeTarget',
        'buildConfigurationList': config_list_id,
        'buildPhases': [sources_phase_id, frameworks_phase_id],
        'buildRules': [],
        'dependencies': [],
        'name': 'iSCSI.dext',
        'productName': 'iSCSI.dext',
        'productReference': product_id,
        'productType': 'com.apple.product-type.system-extension',
    }

    # Add to root targets list
    root.setdefault('targets', []).append(target_id)

    # ── Write back ────────────────────────────────────────────────────
    tmp = PBXPROJ + '.tmp'
    with open(tmp, 'wb') as f:
        plistlib.dump(proj, f)
    os.replace(tmp, PBXPROJ)
    print(f"Added iSCSI.dext target to {PBXPROJ}")
    print(f"  target ID: {target_id}")
    print(f"  references: {len(ref_ids)} files")
    print(f"  build files: {len(bf_ids)} sources")


if __name__ == '__main__':
    main()
