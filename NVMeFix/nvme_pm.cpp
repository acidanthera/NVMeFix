//
// @file nvme_pm.cpp
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

#include <Library/LegacyIOService.h>
#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <kern/assert.h>

#include "NVMeFixPlugin.hpp"

/**
* For Apple Controllers, AppleNVMeController toggles self-refresh for low-power states, and
* completely ignores PCI PM. For generic controllers, IONVMeController uses PCI PM and ignores
* NVMe Power Management features.
* We implement active power management by attaching our own IOService to PM root and registering
* the operational power states of the controller. We intercept activityTickle method of the relevant
* IONVMeController to tickle our service, and use NVMe power management feature to set the
* corresponding state.
* Our PM should operate transparently w.r.t PCI link PM and APST. APST is still useful, because typical
* idle intervals for APST transitions are in order of hundreds of milliseconds, while IOPM has seconds
* resolution; PCI link power management is still used by IONVMeController.
* As we never transition to idle states, we need not freeze the command queue, so we don't have to
* touch the internal state of IONVMe.
*/
bool NVMeFixPlugin::PM::init(ControllerEntry& entry, const NVMe::nvme_id_ctrl* ctrl, bool apste) {
	unsigned op {};

	entry.pm = new NVMePMProxy();
	if (entry.pm && !entry.pm->init()) {
		DBGLOG("pm", "Failed to init IOService");
		return false;
	}
	static_cast<NVMePMProxy*>(entry.pm)->entry = &entry;

	if (entry.quirks & NVMe::NVME_QUIRK_SIMPLE_SUSPEND) {
		SYSLOG("pm", "Using PCI PM due to quirk");
		return false;
	}

	for (int state = ctrl->npss; state >= 0; state--)
		if (!(ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE))
			op++;

	if (op <= 1) {
		SYSLOG("pm", "Controller declares too few power states, using PCI PM");
		return false;
	}

	DBGLOG("pm", "npss 0x%x", ctrl->npss);

	entry.nstates = 1 /* off */ + op;

	entry.powerStates = new IOPMPowerState[entry.nstates];
	if (!entry.powerStates) {
		SYSLOG("pm", "Failed to allocate power state buffer");
		return false;
	}

	entry.powerStates[0] = {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	/**
	 * Linux has a different model: they save state upon suspend and switch to
	 * npss state; upon resume, they restore last state. They also reset ps
	 * when they fail to set or get it, but it's unclear how that would ever
	 * occur given that NVMe 1.4 spec 5.21.1.2 only mentions error when trying
	 * to set unsupported state.
	 */
	size_t idx {1};
	for (int state = ctrl->npss; state >= 0; state--) {
		if (ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE)
			continue;

		auto& ps = entry.powerStates[idx];
		ps = {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		/**
		 * We shouldn't have any IOPM clients that require power, so don't
		 * set outputPowerCharacter.
		 */
//		ps.outputPowerCharacter = kIOPMPowerOn;
		ps.inputPowerRequirement = kIOPMPowerOn;

		/* In non-operational state device is not usable and we may sleep */
		ps.capabilityFlags |= kIOPMPreventIdleSleep;
		ps.capabilityFlags |= kIOPMDeviceUsable;
		DBGLOG("pm", "Setting ps %u capabilityFlags 0x%x", idx, ps.capabilityFlags);
		idx++;
	}

	DBGLOG("pm", "Publishing %u states", entry.nstates);

	entry.pm->PMinit();
	auto root = IOService::getPMRootDomain();
	if (root)
		reinterpret_cast<IOService*>(root)->joinPMtree(entry.pm);
	assert(root);

	constexpr unsigned idlePeriod {2};

	auto status = entry.pm->registerPowerDriver(entry.pm, entry.powerStates,
														entry.nstates);
	if (status != kIOReturnSuccess) {
		SYSLOG("pm", "registerPowerDriver failed with 0x%x", status);
		goto fail;
	}

	status = entry.pm->makeUsable();
	if (status != kIOReturnSuccess) {
		SYSLOG("pm", "makeUsable failed with 0x%x", status);
		goto fail;
	}

	entry.pm->changePowerStateTo(1); /* Clamp lowest PS at 1 */
	entry.pm->setIdleTimerPeriod(idlePeriod);

	return true;
fail:
	if (entry.powerStates) {
		delete[] entry.powerStates;
		entry.powerStates = nullptr;
	}
	if (entry.pm) {
		entry.pm->release();
		entry.pm = nullptr;
	}
	return false;
}

OSDefineMetaClassAndStructors(NVMePMProxy, IOService);

IOReturn NVMePMProxy::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
	if (powerStateOrdinal == 0)
		return kIOPMAckImplied;

	auto& plugin = NVMeFixPlugin::globalPlugin();

	unsigned dword11 = ((static_cast<unsigned>(entry->nstates) - 1) -
						static_cast<unsigned>(powerStateOrdinal)) & 0b1111;
	if (IOLockTryLock(entry->lck)) {
		uint32_t res {};
		auto ret = plugin.NVMeFeatures(*entry, NVMe::NVME_FEAT_POWER_MGMT, nullptr, nullptr, &res,
			false);
		res &= 0b1111;
		if (ret != kIOReturnSuccess) {
			SYSLOG("pm", "Failed to get power state");
		} else if (res < entry->nstates - 1) { /* Only transition to op state if we're not in nop state due to APST */
			DBGLOG("pm", "setPowerState %u", powerStateOrdinal);

			ret = plugin.NVMeFeatures(*entry, NVMe::NVME_FEAT_POWER_MGMT, &dword11, nullptr, nullptr,
										   true);
			if (ret != kIOReturnSuccess)
				SYSLOG("pm", "Failed to set power state");
		}

		IOLockUnlock(entry->lck);
	}

	/**
	 * FIXME: We should return entry + exit + switching overhead latency here, but at least in my tests
	 * it is always 0.
	 */
	return kIOPMAckImplied; /* No real way to signal error (not that we expect any) */
}

bool NVMeFixPlugin::PM::solveSymbols(KernelPatcher& kp) {
	auto idx = plugin.kextInfo.loadIndex;
	bool ret =
	plugin.kextFuncs.IONVMeController.activityTickle.routeVirtual(kp, idx,
													"__ZTV16IONVMeController", 249, activityTickle);

	return ret;
}

bool NVMeFixPlugin::PM::activityTickle(void* controller, unsigned long type, unsigned long stateNumber) {
	auto& plugin = NVMeFixPlugin::globalPlugin();

	ControllerEntry* entry {nullptr};
	IOLockLock(plugin.lck);
	entry = plugin.entryForController(static_cast<IOService*>(controller));
	IOLockUnlock(plugin.lck);

	if (entry && IOLockTryLock(entry->lck)) {
		if (entry->powerStates && entry->pm) {
//			DBGLOG("pm", "activityTickle");
			entry->pm->activityTickle(kIOPMSuperclassPolicy1, entry->nstates - 1);
		}
		IOLockUnlock(entry->lck);
	}

	return plugin.kextFuncs.IONVMeController.activityTickle(controller, type, stateNumber);
}
