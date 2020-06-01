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

#include "Log.hpp"
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
bool NVMeFixPlugin::PM::init(ControllerEntry& entry, const NVMe::nvme_id_ctrl* ctrl, bool apst) {
	unsigned op {};

	entry.pm = new NVMePMProxy();
	if (entry.pm && !entry.pm->init()) {
		DBGLOG(Log::PM, "Failed to init IOService");
		return false;
	}
	static_cast<NVMePMProxy*>(entry.pm)->entry = &entry;

	if (entry.apstAllowed()) {
		DBGLOG(Log::PM, "Registering power change interest");
		entry.controller->registerInterestedDriver(entry.pm);
	}
		
	/* For APST just post the dummy PS */
	if (!apst) {
		for (int state = ctrl->npss; state >= 0; state--)
			if (!(ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE))
				op++;
	} else
		op = 0;

	if (!apst && op <= 1)
		SYSLOG(Log::PM, "Controller declares too few operational power states");

	DBGLOG(Log::PM, "npss 0x%x", ctrl->npss);

	entry.nstates = 1 /* off */ + op;

	entry.powerStates = new IOPMPowerState[entry.nstates];
	if (!entry.powerStates) {
		SYSLOG(Log::PM, "Failed to allocate power state buffer");
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
		DBGLOG(Log::PM, "Setting ps %u capabilityFlags 0x%x", idx, ps.capabilityFlags);
		idx++;
	}

	DBGLOG(Log::PM, "Publishing %u states", entry.nstates);

	entry.pm->PMinit();
	auto root = IOService::getPMRootDomain();
	if (root)
		reinterpret_cast<IOService*>(root)->joinPMtree(entry.pm);
	assert(root);

	auto status = entry.pm->registerPowerDriver(entry.pm, entry.powerStates, entry.nstates);
	if (status != kIOReturnSuccess) {
		SYSLOG(Log::PM, "registerPowerDriver failed with 0x%x", status);
		goto fail;
	}

	status = entry.pm->makeUsable();
	if (status != kIOReturnSuccess) {
		SYSLOG(Log::PM, "makeUsable failed with 0x%x", status);
		goto fail;
	}
	
	if (!apst) {
		entry.pm->changePowerStateTo(1); /* Clamp lowest PS at 1 */
		entry.pm->setIdleTimerPeriod(idlePeriod);
	}

	return true;
fail:
	if (entry.powerStates) {
		delete[] entry.powerStates;
		entry.powerStates = nullptr;
	}
	/* Do not release PM IOService -- we need it for tracking controller power state change */
	return false;
}

OSDefineMetaClassAndStructors(NVMePMProxy, IOService);

IOReturn NVMePMProxy::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
	DBGLOG(Log::PM, "setPowerState %lu", powerStateOrdinal);

	if (powerStateOrdinal == 0)
		return kIOPMAckImplied;

	auto& plugin = NVMeFixPlugin::globalPlugin();

	unsigned dword11 = ((static_cast<unsigned>(entry->nstates) - 1) -
						static_cast<unsigned>(powerStateOrdinal)) & 0b1111;
	/* It's ok to skip active PM */
	if (IOLockTryLock(entry->lck)) {
		uint32_t res {};
		auto ret = plugin.NVMeFeatures(*entry, NVMe::NVME_FEAT_POWER_MGMT, nullptr, nullptr, &res,
			false);
		res &= 0b1111;

		DBGLOG(Log::PM, "Current ps 0x%x, proposed 0x%x", res, dword11);

		if (ret != kIOReturnSuccess) {
			SYSLOG(Log::PM, "Failed to get power state");
		} else if (res < entry->nstates - 1) { /* Only transition to op state if we're not in nop state due to APST */
			DBGLOG(Log::PM, "Setting power state 0x%x", dword11);

			ret = plugin.NVMeFeatures(*entry, NVMe::NVME_FEAT_POWER_MGMT, &dword11, nullptr, nullptr,
										   true);
			if (ret != kIOReturnSuccess)
				SYSLOG(Log::PM, "Failed to set power state");
		}

		IOLockUnlock(entry->lck);
	} else
		DBGLOG(Log::PM, "Failed to obtain entry lock");

	/**
	 * FIXME: We should return entry + exit + switching overhead latency here, but at least in my tests
	 * it is always 0.
	 */
	return kIOPMAckImplied; /* No real way to signal error (not that we expect any) */
}

IOReturn NVMePMProxy::powerStateDidChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber,
											IOService *whatDevice) {
	DBGLOG(Log::PM, "powerStateDidChangeTo 0x%x", stateNumber);

	/* FIXME: Should we ignore PAUSE->ACTIVE transition? */
	if (!(capabilities & kIOPMDeviceUsable)) {
		DBGLOG(Log::PM, "Ignoring transition to non-usable state 0x%x", stateNumber);
		return kIOPMAckImplied;
	}

	auto& plugin = NVMeFixPlugin::globalPlugin();

	/* We only get once chance after wake, so we insist on getting to critical section */
	IOLockLock(entry->lck);
	NVMe::nvme_id_ctrl* identify {};

	if (entry->controller != whatDevice) {
		DBGLOG(Log::PM, "Power state change for irrelevant device %s",
			   whatDevice->getMetaClass()->getClassName());
		goto done;
	}

	if (!entry->apstAllowed()) {
		DBGLOG(Log::PM, "APST not allowed");
		goto done;
	}

	if (!entry->apste) {
		DBGLOG(Log::PM, "APST not enabled yet; not re-enabling");
		goto done;
	}

	assert(entry->identify);
	identify = static_cast<decltype(identify)>(entry->identify->getBytesNoCopy());
	if (!identify) {
		DBGLOG(Log::PM, "Failed to get identify bytes");
		goto done;
	} else if (!plugin.enableAPST(*entry, identify))
		DBGLOG(Log::PM, "Failed to re-enable APST");

done:
	IOLockUnlock(entry->lck);

	return IOPMAckImplied;
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
	
	/* If APST is enabled, we do not manage NVMe PM ourselves. */
	/* We cannot avoid hooking activityTickle, however, as don't know if we have APST in advance */
	if (entry && IOLockTryLock(entry->lck)) {
		if (!entry->apste && entry->powerStates && entry->pm) {
//			DBGLOG("pm", "activityTickle");
			entry->pm->activityTickle(kIOPMSuperclassPolicy1, entry->nstates - 1);
		}
		IOLockUnlock(entry->lck);
	}

	return plugin.kextFuncs.IONVMeController.activityTickle(controller, type, stateNumber);
}
