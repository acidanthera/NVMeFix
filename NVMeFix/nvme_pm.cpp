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
#include <IOKit/pwr_mgt/IOPM.h>
#include <kern/assert.h>

#include "NVMeFixPlugin.hpp"

/**
* For Apple Controllers, AppleNVMeController toggles self-refresh for low-power states, and
* completely ignores PCI PM. For generic controllers, IONVMeController uses PCI PM and ignores
* NVMe Power Management features.
* If we detect that the controller declares at least one operational and at least one non-operational
* state, we completely disable PM for it by calling PMstop, and re-register it with a new power state
* table based on the declared states.
* As the PM callout thread is entered from setPowerState, we will issue an NVMe command to switch to
* the corresponding state.
*/
bool NVMeFixPlugin::PM::init(ControllerEntry& entry, const NVMe::nvme_id_ctrl* ctrl, bool apste) {
	IOService* parent {nullptr};
	if (entry.controller->getProvider())
		parent = static_cast<IOService*>(entry.controller->getProvider()->metaCast("IOPCIDevice"));
	if (!parent) {
		SYSLOG("pm", "Failed to get PCI parent");
		return false;
	}
	unsigned op {}, nop {};

	entry.pm = new NVMePMProxy();
	if (entry.pm && !entry.pm->init()) {
		DBGLOG("pm", "Failed to init IOService");
		return false;
	}

	if (entry.quirks & NVMe::NVME_QUIRK_SIMPLE_SUSPEND) {
		SYSLOG("pm", "Using PCI PM due to quirk");
		return false;
	}

	for (int state = ctrl->npss; state >= 0; state--)
		if (ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE)
			nop++;
		else
			op++;

	if (op < 1 || nop < 1) {
		SYSLOG("pm", "Controller declares too few power states, using PCI PM");
		return false;
	}

	DBGLOG("pm", "npss 0x%x", ctrl->npss);

	if (ctrl->apsta || apste)
		nop = 1;
	entry.nstates = 1 /* off */ + 1 /* nop */ + op;

	entry.powerStates = new IOPMPowerState[entry.nstates];
	if (!entry.powerStates) {
		SYSLOG("pm", "Failed to allocate power state buffer");
		return false;
	}

	entry.powerStates[0] = {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	/**
	 * If APST is used, we will report just one non-operational power state.
	 * The motivation for this is that the controller knows better when to switch as
	 * it considers the switching time, while IOPM ignores it.
	 * Linux has a different model: they save state upon suspend and switch to
	 * npss state; upon resume, they restore last state. They also reset ps
	 * when they fail to set or get it, but it's unclear how that would ever
	 * occur given that NVMe 1.4 spec 5.21.1.2 only mentions error when trying
	 * to set unsupported state.
	 */
	size_t idx {1};
	for (int state = ctrl->npss; state >= 0; state--) {
		/* Seek to last non-operational state if NPSS is enabled */
		if (ctrl->apsta && state > 0 && ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE &&
			ctrl->psd[state + 1].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE)
			continue;
		assert(idx != 1 || ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE);

		auto& ps = entry.powerStates[idx];
		ps = {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		/**
		 * We shouldn't have any IOPM clients that require power, so don't
		 * set outputPowerCharacter.
		 */
//		ps.outputPowerCharacter = kIOPMPowerOn;
		ps.inputPowerRequirement = kIOPMPowerOn;

		/* In non-operational state device is not usable and we may sleep */
		if (ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE) {
			ps.capabilityFlags |= kIOPMLowPower;
		} else {
			ps.capabilityFlags |= kIOPMPreventIdleSleep;
			ps.capabilityFlags |= kIOPMDeviceUsable;
		}

		DBGLOG("pm", "Setting ps %u capabilityFlags 0x%x", idx, ps.capabilityFlags);
		idx++;
	}

	DBGLOG("pm", "Publishing %u states", entry.nstates);

	/* Not sure if this is true with APST on */
//	entry.powerStates[ctrl->npss + 1].capabilityFlags |= kIOPMInitialDeviceState;

	/* FIXME: Issue thread_call_cancel(controller->PMThread)? */

	/**
	 * This seems to leave the parent in inconsistent state. PMstop should detach us from PCIDevice but
	 * when calling joinPMTree PM says "PXSX: IONVMeController is already a child", although it does
	 * not seem to be visible anywhere in PM plane. As a workaround, we remove it by hand.
	 */

	parent->retain();
	entry.controller->retain();
	entry.controller->PMstop();
	auto iter = entry.controller->getParentIterator(gIOPowerPlane);
	if (iter) {
		OSObject* next {nullptr};
		while ((next = iter->getNextObject())) {
			IOPowerConnection* conn = reinterpret_cast<IOPowerConnection*>
				(next->metaCast("IOPowerConnection"));
			if (conn) {
				parent->removePowerChild(conn);
				DBGLOG("pm", "Removing power child");
				break;
			}
		}
	}
	entry.pm->PMinit();
	parent->joinPMtree(entry.pm);

	/* Judging by IOServicePM source it should work without stop/init */
	auto status = entry.pm->registerPowerDriver(entry.pm, entry.powerStates,
														entry.nstates);
	if (status != kIOReturnSuccess) {
		SYSLOG("pm", "registerPowerDriver failed with 0x%x", status);
		goto fail;
	}
//	entry.controller->PMinit();
	entry.pm->joinPMtree(entry.controller);

	status = entry.pm->makeUsable();
	if (status != kIOReturnSuccess) {
		SYSLOG("pm", "makeUsable failed with 0x%x", status);
		goto fail;
	}

	entry.pm->setPowerState(entry.nstates - 1, entry.controller);

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
	DBGLOG("pm", "setPowerState %ul", powerStateOrdinal);
	return IOPMAckImplied;
}

bool NVMePMProxy::activityTickle(unsigned long type, unsigned long stateNumber) {
	DBGLOG("pm", "activityTickle %u", stateNumber);
	return false;
}

bool NVMeFixPlugin::PM::solveSymbols(KernelPatcher& kp) {
	return true;

	auto idx = plugin.kextInfo.loadIndex;
	bool ret =
		plugin.kextFuncs.IONVMeController.GetActivePowerState.solve(kp, idx) &&
		plugin.kextFuncs.IONVMeController.initialPowerStateForDomainState.solve(kp, idx) &&
		kp.solveSymbol(idx, "__ZTV16IONVMeController") &&
		plugin.kextFuncs.IONVMeController.ThreadEntry.solve(kp, idx) &&
		plugin.kextFuncs.IONVMeController.setPowerState.solve(kp, idx);

	/* mov ecx, [rbx+0x188] */
	ret &= plugin.kextMembers.IONVMeController.fProposedPowerState.fromFunc(
				plugin.kextFuncs.IONVMeController.ThreadEntry.fptr, 0x8b, 1, 3);

	ret &= plugin.kextFuncs.IONVMeController.GetActivePowerState.route(kp, idx,
																		   GetActivePowerState) &&
	plugin.kextFuncs.IONVMeController.initialPowerStateForDomainState.route(kp,
													idx,
													initialPowerStateForDomainState) &&
	plugin.kextFuncs.IONVMeController.activityTickle.routeVirtual(kp, idx,
													"__ZTV16IONVMeController", 249, activityTickle) &&
	plugin.kextFuncs.IONVMeController.ThreadEntry.route(kp, idx, ThreadEntry) &&
	plugin.kextFuncs.IONVMeController.setPowerState.route(kp, idx, setPowerState);

	return ret;
}
/**
 * NOTE: IOPM callbacks are invoked asynchronously, so we need locking here when
 * accessing the plugin.
 * We must install the PM hooks early because KernelPatcher may not be available
 * anymore, even if we discover we want to use PCI PM later on.
 *
 * GetActivePowerState has the following usage in the kext:
 * initialPowerStateForDomainState to return active state
 * setPowerState, ThreadEntry set fProposedPowerState and test if it corresponds to active
 * InitializePowerManagement, PreTransitionFixup to set activePowerState
 * GetRequest to do activityTickle
 * AddReportingChannels, ReportPowerState to setStateID for reporter
 * ClampLowPowerState after init to get to active PS
 *
 * There's only one active power state in apple logic, but we may have numerous, so we patch the
 * following functions in order to keep the resulting logic consistent:
 * GetActivePowerState to return fProposedPowerState if fProposedPowerState corresponds to an
 * active state, else return the highest-power state
 * initialPowerStateForDomainState to read PS via NVMe command
 * virtual IOService::activityTickle in IONVMeController vtable to tickle always to the highest power
 * state
 * FIXME: We need to actually issue set power state commands (in ThreadEntry?) See
 * AppleNVMeController::BeginThread for example: they call AppleNVMeController::EnterSelfRefresh when
 * needed.
 * FIXME: IOPM may see an inconsistent non-active state if APST is enabled. We could hook
 */

uint64_t NVMeFixPlugin::PM::GetActivePowerState(void* controller) {
	uint64_t aps {}; /* Active power state is never zero */
	auto& plugin = NVMeFixPlugin::globalPlugin();

	/**
	 * We need to lock now as controller list may be being modified.
	 * Our initialization occurs in critical section, and it depends on some of these functions, so
	 * we will deadlock if we just use that lock.
	 */
	ControllerEntry* entry {nullptr};
	IOLockLock(plugin.lck);
	entry = plugin.entryForController(static_cast<IOService*>(controller));
	IOLockUnlock(plugin.lck);

	/* If we fail to obtain the lock now, it means we are initialisating the controller */
	if (entry && IOLockTryLock(entry->lck)) {
		/* Our IOPM tables are ready, so we have our PM working */
		if (entry->powerStates) {
			auto& pps = plugin.kextMembers.IONVMeController.fProposedPowerState.get(controller);
			assertf(pps < entry->nstates, "fProposedPowerState not in PM table");
//				DBGLOG("pm", "fProposedPowerState %llu", pps);

			if (entry->powerStates[pps].capabilityFlags & kIOPMDeviceUsable)
				aps = pps;
			else
				aps = entry->nstates - 1;
		}

		IOLockUnlock(entry->lck);
	}
	if (!aps)
		aps = plugin.kextFuncs.IONVMeController.GetActivePowerState(controller);

	return aps;
}

uint64_t NVMeFixPlugin::PM::initialPowerStateForDomainState(void* controller, uint64_t domainState) {
	DBGLOG("pm", "initialPowerStateForDomainState");

	auto& plugin = NVMeFixPlugin::globalPlugin();
	if (atomic_load_explicit(&plugin.solvedSymbols, memory_order_acquire))
		return GetActivePowerState(controller);
	else
		return plugin.kextFuncs.IONVMeController.initialPowerStateForDomainState(controller, domainState);
}

bool NVMeFixPlugin::PM::activityTickle(void* controller, unsigned long type, unsigned long stateNumber) {
//	DBGLOG("pm", "activityTickle %u", stateNumber);
	uint64_t aps {};
	auto& plugin = NVMeFixPlugin::globalPlugin();

	ControllerEntry* entry {nullptr};
	IOLockLock(plugin.lck);
	entry = plugin.entryForController(static_cast<IOService*>(controller));
	IOLockUnlock(plugin.lck);

//	DBGLOG("pm", "activityTickle 0x%x", stateNumber);

	if (entry && IOLockTryLock(entry->lck)) {
		if (entry->powerStates)
			aps = entry->nstates - 1;
		IOLockUnlock(entry->lck);
	}

	if (!aps)
		aps = stateNumber;
	return plugin.kextFuncs.IONVMeController.activityTickle(controller, type, aps);
}

IOReturn NVMeFixPlugin::PM::setPowerState(void* controller, unsigned long state, IOService* what) {
	DBGLOG("pm", "setPowerState 0x%x", state);
	return NVMeFixPlugin::globalPlugin().kextFuncs.IONVMeController.setPowerState(controller,
																				  state,
																				  what);
}

void NVMeFixPlugin::PM::ThreadEntry(void* controller) {
	auto& plugin = NVMeFixPlugin::globalPlugin();
	SYSLOG("pm", "ThreadEntry: fProposedPowerState 0x%x",
		   plugin.kextMembers.IONVMeController.fProposedPowerState.get(controller));
	return plugin.kextFuncs.IONVMeController.ThreadEntry(controller);
}
