# SPDX-License-Identifier: Apache-2.0
"""Tilapia full-OTA safeguards for the repurposed cache partition."""


def FullOTA_InstallBegin(info):
    # Legacy recoveries can mount CAC as /cache. It now contains Product.
    # Refuse writes if any known System/Product mount cannot be removed.
    for mount_point in ("/cache", "/product", "/system", "/system_root"):
        info.script.AppendExtra(
            'if is_mounted("{0}") then\n'
            '  assert(unmount("{0}"));\n'
            'endif;\n'
            'assert(!is_mounted("{0}"));'.format(mount_point))
