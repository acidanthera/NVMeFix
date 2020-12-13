NVMeFix Changelog
=================
#### v1.0.5
- Fixed quirks enabling per controller

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
