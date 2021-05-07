//
// @file nvme_quirks.hpp
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


#ifndef nvme_quirks_hpp
#define nvme_quirks_hpp

#include <IOKit/IOService.h>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_util.hpp>

#include "linux_types.h"

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011-2014, Intel Corporation.
 */

/* linux/drivers/host/nvme.h */
namespace NVMe {

/*
 * List of workarounds for devices that required behavior not specified in
 * the standard.
 */
enum nvme_quirks : unsigned long long {
	NVME_QUIRK_NONE					= 0,
	/*
	 * Prefers I/O aligned to a stripe size specified in a vendor
	 * specific Identify field.
	 */
	NVME_QUIRK_STRIPE_SIZE			= (1 << 0),

	/*
	 * The controller doesn't handle Identify value others than 0 or 1
	 * correctly.
	 */
	NVME_QUIRK_IDENTIFY_CNS			= (1 << 1),

	/*
	 * The controller deterministically returns O's on reads to
	 * logical blocks that deallocate was called on.
	 */
	NVME_QUIRK_DEALLOCATE_ZEROES		= (1 << 2),

	/*
	 * The controller needs a delay before starts checking the device
	 * readiness, which is done by reading the NVME_CSTS_RDY bit.
	 */
	NVME_QUIRK_DELAY_BEFORE_CHK_RDY		= (1 << 3),

	/*
	 * APST should not be used.
	 */
	NVME_QUIRK_NO_APST			= (1 << 4),

	/*
	 * The deepest sleep state should not be used.
	 */
	NVME_QUIRK_NO_DEEPEST_PS		= (1 << 5),

	/*
	 * Supports the LighNVM command set if indicated in vs[1].
	 */
	NVME_QUIRK_LIGHTNVM			= (1 << 6),

	/*
	 * Set MEDIUM priority on SQ creation
	 */
	NVME_QUIRK_MEDIUM_PRIO_SQ		= (1 << 7),

	/*
	 * Ignore device provided subnqn.
	 */
	NVME_QUIRK_IGNORE_DEV_SUBNQN		= (1 << 8),

	/*
	 * Broken Write Zeroes.
	 */
	NVME_QUIRK_DISABLE_WRITE_ZEROES		= (1 << 9),

	/*
	 * Force simple suspend/resume path.
	 */
	NVME_QUIRK_SIMPLE_SUSPEND		= (1 << 10),

	/*
	 * Use only one interrupt vector for all queues
	 */
	NVME_QUIRK_SINGLE_VECTOR		= (1 << 11),

	/*
	 * Use non-standard 128 bytes SQEs.
	 */
	NVME_QUIRK_128_BYTES_SQES		= (1 << 12),

	/*
	 * Prevent tag overlap between queues
	 */
	NVME_QUIRK_SHARED_TAGS                  = (1 << 13),
};

template <typename T>
constexpr nvme_quirks operator|(T a, T b) {
    return static_cast<nvme_quirks>(static_cast<unsigned long long>(a) |
									static_cast<unsigned long long>(b));
}

template <typename T>
constexpr nvme_quirks operator|=(T& a, T b) {
	a = a | b;
	return a;
}

template <typename T>
constexpr nvme_quirks operator&(T a, T b) {
    return static_cast<nvme_quirks>(static_cast<unsigned long long>(a) &
									static_cast<unsigned long long>(b));
}

template <typename T>
constexpr nvme_quirks operator&=(T& a, T b) {
	a = a & b;
	return a;
}

using mn_ref_t = const char(&)[40];
using fr_ref_t = const char(&)[8];
nvme_quirks quirksForController(IOService*);
nvme_quirks quirksForController(uint16_t,mn_ref_t,fr_ref_t);
}

template <typename T>
static bool propertyFromParent(IOService* controller, const char* name, T& prop) {
	assertf(controller->metaCast("IONVMeController"), "Controller has wrong type");

	auto parent = controller->getParentEntry(gIOServicePlane);
	if (!parent || !parent->metaCast("IOPCIDevice")) {
		DBGLOG("quirks", "Controller parent is not an IOPCIDevice");
		return false;
	}

	auto data = parent->getProperty(name);
	if (data)
		return WIOKit::getOSDataValue(data, name, prop);
	else
		DBGLOG("nvmef", "Property %s not found for parent service", name);

	return true;
}

#endif /* nvme_quirks_hpp */
