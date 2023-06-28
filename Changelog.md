NVMeFix Changelog
=================
#### v1.1.1
- Added constants for macOS 14 support
- Fixed macOS 14 compatibility

#### v1.1.0
- Added constants for macOS 13 support

#### v1.0.9
- Added constants for macOS 12 support
- Fixed macOS 12 compatibility

#### v1.0.8
- Fixed applying quirks based on the disk name and serial
- Make Kingston A2000 quirk specific to S5Z42105

#### v1.0.7
- Fixed symbol solving on macOS 11.3
- Added `-nvmefaspm` boot argument to force ASPM L1 on all NVMe SSDs

#### v1.0.6
- Added APST workaround for Kingston A2000

#### v1.0.5
- Fixed quirks enabling per controller
- Fixed initialisation on 10.15+

#### v1.0.4
- Added MacKernelSDK with Xcode 12 compatibility

#### v1.0.3
- Fix re-enabling APST after sleep (1.0.2 regression)
- Added constants for 11.0 support (no full compatibility provided)

#### v1.0.2
- Prevent timeout panic on certain controllers (VMware, Samsung PM981)
- Only enable active NVMe power management for controllers that do not support APST

#### v1.0.1
- Add OpenCore support for quirk autodetection

#### v1.0.0
- Initial release
