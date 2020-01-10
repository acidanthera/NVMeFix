//
// @file nvme_apst.cpp
//
// NVMeFix
//
// Copyright © 2020 acidanthera. All rights reserved.
//
// This program and the accompanying materials
// are licensed and made available under the terms and conditions of the BSD License
// which accompanies this distribution.  The full text of the license may be found at
// http://opensource.org/licenses/bsd-license.php
// THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
// WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express device driver
 * Portions Copyright (c) 2011-2014, Intel Corporation.
 */

#include "Log.hpp"
#include "NVMeFixPlugin.hpp"

bool NVMeFixPlugin::enableAPST(ControllerEntry& entry, const NVMe::nvme_id_ctrl* ctrl) {
#ifdef DEBUG
	if (APSTenabled(entry, entry.apste) == kIOReturnSuccess)
		DBGLOG(Log::APST, "APST status %d", entry.apste);
#endif

	if (entry.apstAllowed()) {
		DBGLOG("apst", "Configuring APST");
		auto res = configureAPST(entry, ctrl);
		if (res != kIOReturnSuccess) {
			DBGLOG("nvmef", "Failed to configure APST with 0x%x", res);
			entry.apste = false;
		} else /* Assume we turn APST on without double checking in RELEASE builds */
			entry.apste = true;
	} else
		DBGLOG(Log::APST, "Not configuring APST (it is already enabled or quirks prohibit it)");

#ifdef DEBUG
	if (APSTenabled(entry, entry.apste) == kIOReturnSuccess) {
		DBGLOG(Log::APST, "APST status %d", entry.apste);
	}
	if (entry.apste && dumpAPST(entry, ctrl->npss))
		DBGLOG(Log::APST, "Failed to dump APST table");
#endif

	entry.controller->setProperty("apst", entry.apste);
	return entry.apste;
}

/* linux/drivers/nvme/host/core.c:nvme_configure_apst */
IOReturn NVMeFixPlugin::configureAPST(ControllerEntry& entry, const NVMe::nvme_id_ctrl* ctrl) {
	assert(ctrl);
	assert(entry.controller);

	if (!ctrl->apsta) {
		SYSLOG(Log::APST, "APST unsupported by this controller");
		return kIOReturnUnsupported;
	}
	if (ctrl->npss > 31) {
		SYSLOG(Log::APST, "Invalid NPSS");
		return kIOReturnUnsupported;
	}

	auto ret = kIOReturnSuccess;
	auto apstDesc = IOBufferMemoryDescriptor::withCapacity(sizeof(NVMe::nvme_feat_auto_pst),
														   kIODirectionOut);

	if (!apstDesc) {
		SYSLOG(Log::APST, "Failed to create APST table descriptor");
		return kIOReturnNoResources;
	}

	auto apstTable = reinterpret_cast<NVMe::nvme_feat_auto_pst*>(apstDesc->getBytesNoCopy());
	int max_ps {-1};

	if (apstTable) {
		memset(apstTable, '\0', sizeof(*apstTable));

		uint64_t target {0};
		uint64_t max_lat_us {0};

		/*
		* Walk through all states from lowest- to highest-power.
		* According to the spec, lower-numbered states use more
		* power.  NPSS, despite the name, is the index of the
		* lowest-power state, not the number of states.
		*/
		for (int state = ctrl->npss; state >= 0; state--) {
			if (target) {
				apstTable->entries[state] = target;
				DBGLOG(Log::APST, "Set entry %d to 0x%llx", state, target);
			}

			/*
			 * Don't allow transitions to the deepest state
			 * if it's quirked off.
			 */
			if (state == ctrl->npss && (entry.quirks & NVMe::nvme_quirks::NVME_QUIRK_NO_DEEPEST_PS))
				continue;

			/*
			 * Is this state a useful non-operational state for
			 * higher-power states to autonomously transition to?
			 */
			if (!(ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE))
				continue;

			uint64_t exit_latency_us = ctrl->psd[state].exit_lat;
			if (exit_latency_us > entry.ps_max_latency_us)
				continue;

			uint64_t total_latency_us = exit_latency_us + ctrl->psd[state].entry_lat;
			/*
			 * This state is good.  Use it as the APST idle
			 * target for higher power states.
			 */
			uint64_t transition_ms = total_latency_us + 19;
			transition_ms /= 20;
			if (transition_ms > (1ull << 24) - 1)
				transition_ms = (1ull << 24) - 1;

			target = (state << 3ull) | (transition_ms << 8ull);

			if (max_ps == -1)
				max_ps = state;

			if (total_latency_us > max_lat_us)
				max_lat_us = total_latency_us;
		}

		if (max_ps == -1)
			DBGLOG(Log::APST, "No non-operational states are available");
		else
			DBGLOG(Log::APST, "APST enabled: max PS = %d, max round-trip latency = %lluus\n",
				max_ps, max_lat_us);
	} else
		SYSLOG(Log::APST, "Failed to get table buffer");

	if (max_ps != -1) {
		uint32_t dword11 {1};
		ret = NVMeFeatures(entry, NVMe::NVME_FEAT_AUTO_PST, &dword11, apstDesc, nullptr, true);
	}

	if (apstDesc)
		apstDesc->release();
	return ret;
}

IOReturn NVMeFixPlugin::APSTenabled(ControllerEntry& entry, bool& enabled) {
	uint32_t res {};
	auto ret = NVMeFeatures(entry, NVMe::NVME_FEAT_AUTO_PST, nullptr, nullptr, &res, false);

	if (ret != kIOReturnSuccess)
		DBGLOG(Log::APST, "Failed to get features");
	else
		enabled = res;
	return ret;
}

IOReturn NVMeFixPlugin::dumpAPST(ControllerEntry& entry, int npss) {
	assert(entry.controller);

	auto ret = kIOReturnSuccess;
	auto apstDesc = IOBufferMemoryDescriptor::withCapacity(sizeof(NVMe::nvme_feat_auto_pst),
														   kIODirectionIn);
	if (!apstDesc) {
		SYSLOG(Log::APST, "Failed to create APST table descriptor");
		return kIOReturnNoResources;
	}

	auto apstTable = static_cast<NVMe::nvme_feat_auto_pst*>(apstDesc->getBytesNoCopy());
	memset(apstTable, '\0', apstDesc->getLength());

	if (NVMeFeatures(entry, NVMe::NVME_FEAT_AUTO_PST, nullptr, apstDesc, nullptr, false) !=
		kIOReturnSuccess) {
		DBGLOG(Log::APST, "Failed to get features");
		goto fail;
	}

	for (int state = npss; state >= 0; state--)
		DBGLOG(Log::APST, "entry %d : 0x%llx", state, apstTable->entries[state]);

fail:
	if (apstDesc)
		apstDesc->release();
	return ret;
}
