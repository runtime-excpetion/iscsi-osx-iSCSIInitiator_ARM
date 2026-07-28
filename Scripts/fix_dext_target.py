#!/usr/bin/env python3
"""
Fix the iSCSI.dext Xcode target: add missing source files, fix build settings,
and ensure the DEXT group references are correct.
"""

import plistlib
import secrets
import os, sys

PBXPROJ = os.path.join(os.path.dirname(__file__), '..', 'iSCSIInitiator.xcodeproj', 'project.pbxproj')

def new_id():
    return secrets.token_hex(12).upper()


def main():
    with open(PBXPROJ, 'rb') as f:
        proj = plistlib.load(f)

    objs = proj['objects']
    root = objs[proj['rootObject']]
    main_group = objs[root['mainGroup']]

    # ── Find DEXT target and related objects ─────────────────────────
    dext_target = None
    dext_tid = None
    for tid, tgt in objs.items():
        if isinstance(tgt, dict) and tgt.get('name') == 'iSCSI.dext':
            dext_target = tgt
            dext_tid = tid
            break

    if not dext_target:
        print("ERROR: iSCSI.dext target not found!")
        sys.exit(1)

    # Find build phases
    sources_phase = None
    for phid in dext_target['buildPhases']:
        ph = objs.get(phid, {})
        if ph.get('isa') == 'PBXSourcesBuildPhase':
            sources_phase = ph
            sources_phase_id = phid
            break

    # ── Add missing file references ──────────────────────────────────
    existing_files = {}
    for oid, obj in objs.items():
        if isinstance(obj, dict) and obj.get('isa') == 'PBXFileReference':
            path = obj.get('path', '')
            existing_files[path] = oid

    NEW_FILE_REFS = {
        'Source/DEXT/iSCSIPDUEncoding.cpp': {
            'isa': 'PBXFileReference',
            'path': 'iSCSIPDUEncoding.cpp',
            'sourceTree': '<group>',
        },
        'Source/DEXT/iSCSIPDUEncoding.h': {
            'isa': 'PBXFileReference',
            'path': 'iSCSIPDUEncoding.h',
            'sourceTree': '<group>',
        },
    }

    new_ref_ids = {}
    for path, ref in NEW_FILE_REFS.items():
        if path not in existing_files:
            rid = new_id()
            objs[rid] = ref
            new_ref_ids[path] = rid
            existing_files[path] = rid
            print(f"  Added file ref: {path} -> {rid}")
        else:
            print(f"  Existing file ref: {path} -> {existing_files[path]}")

    # ── Build files for new sources ──────────────────────────────────
    existing_build_files = set()
    for phid in dext_target['buildPhases']:
        ph = objs.get(phid, {})
        if ph.get('isa') == 'PBXSourcesBuildPhase':
            for fid in ph.get('files', []):
                bf = objs.get(fid, {})
                existing_build_files.add(bf.get('fileRef', ''))

    NEW_BUILD_FILES = [
        ('iSCSIPDUEncoding.cpp', 'Source/DEXT/iSCSIPDUEncoding.cpp'),
        ('iSCSIDextHBA.iig', 'Source/DEXT/iSCSIDextHBA.iig'),
    ]

    new_bf_ids = {}
    for name, path in NEW_BUILD_FILES:
        ref_id = existing_files.get(path)
        if ref_id is None:
            print(f"  WARNING: no file ref for {path}")
            continue
        if ref_id in existing_build_files:
            print(f"  Build file already exists for {name}")
            continue
        bfid = new_id()
        objs[bfid] = {
            'isa': 'PBXBuildFile',
            'fileRef': ref_id,
        }
        new_bf_ids[name] = bfid
        # Add to sources phase
        sources_phase.setdefault('files', []).append(bfid)
        print(f"  Added build file: {name} -> {bfid}")

    # ── Fix DEXT group children ──────────────────────────────────────
    dext_group_id = None
    for cid in main_group.get('children', []):
        child = objs.get(cid, {})
        if child.get('isa') == 'PBXGroup' and child.get('name') == 'DEXT':
            dext_group_id = cid
            break

    if dext_group_id:
        dext_group = objs[dext_group_id]
        existing_children = set(dext_group.get('children', []))
        dext_paths = {p for p in existing_files if p.startswith('Source/DEXT/')}
        for path in sorted(dext_paths):
            rid = existing_files[path]
            if rid not in existing_children:
                dext_group.setdefault('children', []).append(rid)
                print(f"  Added to DEXT group: {path}")
    else:
        # Create DEXT group
        dext_group_id = new_id()
        dext_paths = sorted([p for p in existing_files if p.startswith('Source/DEXT/')])
        objs[dext_group_id] = {
            'isa': 'PBXGroup',
            'children': [existing_files[p] for p in dext_paths],
            'name': 'DEXT',
            'sourceTree': '<group>',
        }
        main_group.setdefault('children', []).append(dext_group_id)
        print(f"  Created DEXT group with {len(dext_paths)} children")

    # ── Fix build settings ──────────────────────────────────────────
    config_list = objs.get(dext_target['buildConfigurationList'], {})
    for cid in config_list.get('buildConfigurations', []):
        cfg = objs.get(cid, {})
        bs = cfg.setdefault('buildSettings', {})

        # Essential DriverKit settings
        bs['SDKROOT'] = 'driverkit'
        bs['PRODUCT_BUNDLE_IDENTIFIER'] = 'com.github.iscsi-osx.iSCSIInitiator.dext'
        bs['INFOPLIST_FILE'] = 'Source/DEXT/Info.plist'
        bs['CODE_SIGN_ENTITLEMENTS'] = 'Source/DEXT/iSCSIDext.entitlements'
        bs['SKIP_INSTALL'] = 'YES'
        bs['GCC_PREPROCESSOR_DEFINITIONS'] = ['$(inherited)']

        # Header search paths for DEXT (need iSCSIPDUShared.h from Kernel)
        bs['HEADER_SEARCH_PATHS'] = [
            '$(inherited)',
            '$(SRCROOT)/Source/DEXT',
            '$(SRCROOT)/Source/Kernel',
        ]

        # IIG-generated headers go to DERIVED_FILE_DIR; add it to include path
        bs['USER_HEADER_SEARCH_PATHS'] = [
            '$(inherited)',
            '$(DERIVED_FILE_DIR)',
        ]

        # Remove macOS-specific settings that break DriverKit builds
        for key in ['MACOSX_DEPLOYMENT_TARGET', 'DYLIB_CURRENT_VERSION',
                    'GCC_WARN_64_TO_32_BIT_CONVERSION', 'CLANG_WARN_BOOL_CONVERSION',
                    'CLANG_WARN_CONSTANT_CONVERSION', 'CLANG_WARN_DIRECT_OBJC_ISA_USAGE',
                    'CLANG_WARN_EMPTY_BODY', 'CLANG_WARN_ENUM_CONVERSION',
                    'CLANG_WARN_INT_CONVERSION', 'CLANG_WARN_OBJC_ROOT_CLASS',
                    'CLANG_WARN__DUPLICATE_METHOD_MATCH', 'CLANG_ENABLE_OBJC_ARC',
                    'CLANG_CXX_LANGUAGE_STANDARD', 'GCC_ENABLE_OBJC_EXCEPTIONS',
                    'WATCHOS_DEPLOYMENT_TARGET', 'IPHONEOS_DEPLOYMENT_TARGET',
                    'TVOS_DEPLOYMENT_TARGET']:
            bs.pop(key, None)

        if cfg.get('name') == 'Release':
            bs.pop('GCC_PREPROCESSOR_DEFINITIONS', None)

        print(f"  Fixed build settings for: {cfg.get('name')}")

    # ── Write back ───────────────────────────────────────────────────
    tmp = PBXPROJ + '.tmp'
    with open(tmp, 'wb') as f:
        plistlib.dump(proj, f)
    os.replace(tmp, PBXPROJ)
    print("\nDone! DEXT target fixed.")


if __name__ == '__main__':
    main()
