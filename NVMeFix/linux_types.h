//
// @file linux_types.h
//
// NVMeFix
//
// Copyright © 2019 acidanthera. All rights reserved.
//
// This program and the accompanying materials
// are licensed and made available under the terms and conditions of the BSD License
// which accompanies this distribution.  The full text of the license may be found at
// http://opensource.org/licenses/bsd-license.php
// THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
// WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.


#ifndef linux_types_h
#define linux_types_h

#include <stdint.h>

namespace linux_types {
#define USING_TY(p, sz) using p ## sz = uint ## sz ## _t;

USING_TY(__le, 16)
USING_TY(__le, 32)
USING_TY(__le, 64)
USING_TY(__u, 8)
USING_TY(__u, 16)
USING_TY(__u, 32)
USING_TY(__u, 64)

using kernel_ulong_t = unsigned long;
#undef USING_TY
};

#endif /* linux_types_h */
