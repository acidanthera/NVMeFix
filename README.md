NVMeFix
=======

[![Build Status](https://travis-ci.com/acidanthera/NVMeFix.svg?branch=master)](https://travis-ci.com/acidanthera/NVMeFix)

NVMeFix is a set of patches for the Apple NVMe storage driver, IONVMeFamily.
Its goal is to improve compatibility with non-Apple SSDs. It may be used both on Apple and non-Apple
computers.

The following features are implemented:

- Autonomous Power State Transition to reduce idle power consumption of the controller.
- Host-driver active power state management.
- Workaround for timeout panics on certain controllers (VMware, Sumsung PM981).

Other incompatibilities with third-party SSDs may be addressed provided enough information is
submitted to the bugtracker.

Unfortunately, some issues cannot be fixed purely by a kernel-side driver. For example, MacBookPro
11,1 EFI includes an old version of NVMHCI DXE driver that causes a hang when resuming from
hibernaton with full disk encryption on.

Installation
------------

NVMeFix requires at least Lilu 1.4.1 and at least 10.14 system version. It may be compatible with
older systems, but has not been tested.

It may be installed to `/Library/Extensions`, or injected by the bootloader.

Configuration
-------------

`-nvmefdbg` enables detailed logging for `DEBUG` build.

`-nvmefoff` disables the kext.

Some SSDs misbehave when APST is on. NVMeFix attempts to detect broken motherboard and SSD
combinations and work around them. Motherboard is detected via IORegistry keys injected by Clover,
or NVRAM variables provided by OpenCore.

APST table entries specify minimum idle latency for the transition to occur. Maximum acceptable
latency is 100000 microseconds, and may be overriden via little-endian 8-byte property
`ps-max-latency-us` of parent PCI device (e.g.
`IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/RP06@1C,5/IOPP/SSD0@0`). If set to 0, APST
will be disabled completely.

Diagnostics
-----------

`RELEASE` build will only log high-level information about failures.

`DEBUG` build will additionally log used power states, detailed error messages, and attempt to
fetch APST status and table from the controller.

APST enable status is posted to the IONVMeController IORegistry entry `apst` key.

If active power management initialisation is successful, an `NVMePMProxy` entry will be created
in the IOPower IORegistry plane with IOPowerManagement dictionary.

Information about power states supported by the controller may be obtained e.g. using `smartmontools`.
For example, in the following output the controller reports 5 states, where the former three
high-power states will be used by NVMeFix for active power management, and the latter two may be
used for APST depending on `ps-max-latency-us`.

    Supported Power States
    St Op     Max   Active     Idle   RL RT WL WT  Ent_Lat  Ex_Lat
     0 +     9.00W       -        -    0  0  0  0        0       0
     1 +     4.60W       -        -    1  1  1  1        0       0
     2 +     3.80W       -        -    2  2  2  2        0       0
     3 -   0.0450W       -        -    3  3  3  3     2000    2000
     4 -   0.0040W       -        -    4  4  4  4     6000    8000

IONVMeFamily supports the following debug flag bitfield, which are passed either via `nvme` bootarg
or `debug.NVMe` sysctl:

    1: Log some events via kprintf
    2: Detailed event trace via kernel_debug with 0x61500xx debugid
    4: PRP-related event trace via kernel_debug with 0x61540xx debugid
    8: Force disable LPSR for Apple controllers
    16: Perform only PCI initialisation of NVMe controller
    32: Ignore initialisation errors
    128: Disable LPSR for Apple controllers
    512: Disable Unmap feature for IONVMeBlockStorageDevice

IONVMeFamily supports the following additional bootargs:

    nand-io-timeoutms: Timeout for NVMe requests in ms, 35 s by default
    enable-IO-log: Issue CORE_DEBUG_ENABLE_IOLOG ASP command (for Apple controllers)
