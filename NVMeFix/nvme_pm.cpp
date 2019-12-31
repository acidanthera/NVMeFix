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
		if (ctrl->apsta && state > 0 && ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE && ctrl->psd[state + 1].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE)
			continue;
		assert(idx != 1 || ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE);

		auto& ps = entry.powerStates[idx++];
		ps = {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		/**
		 * We shouldn't have any IOPM clients that require power, so don't
		 * set outputPowerCharacter.
		 */
//		ps.outputPowerCharacter = kIOPMPowerOn;
		ps.inputPowerRequirement = kIOPMPowerOn;

		/* In non-operational state device is not usable and we may sleep */
		if (ctrl->psd[state].flags & NVMe::NVME_PS_FLAGS_NON_OP_STATE) {
			entry.powerStates[idx].capabilityFlags |= kIOPMLowPower;
		} else {
			ps.capabilityFlags |= kIOPMPreventIdleSleep;
			ps.capabilityFlags |= kIOPMDeviceUsable;
		}
	}

	/* Not sure if this is true with APST on */
//	entry.powerStates[ctrl->npss + 1].capabilityFlags |= kIOPMInitialDeviceState;

	entry.controller->PMstop();
	entry.controller->PMinit();
	parent->joinPMtree(entry.controller);
	entry.controller->registerPowerDriver(entry.controller, entry.powerStates, entry.nstates);
	/* Not sure if we need this and how that would work in the context */
//	IOService::makeUsable();
	entry.controller->changePowerStateTo(entry.nstates - 1);

	return true;
}

bool NVMeFixPlugin::PM::solveSymbols(KernelPatcher& kp) {
	auto idx = plugin.kextInfo.loadIndex;
	bool ret = plugin.kextFuncs.IONVMeController.GetActivePowerState.route(kp, idx,
																		   GetActivePowerState) &&
	plugin.kextFuncs.IONVMeController.initialPowerStateForDomainState.route(kp,
													idx,
													initialPowerStateForDomainState) &&
	plugin.kextFuncs.IONVMeController.activityTickle.routeVirtual(kp, idx,
													"__ZTV16IONVMeController", 249, activityTickle) &&
	plugin.kextFuncs.IONVMeController.ThreadEntry.route(kp, idx, ThreadEntry);

	/* mov ecx, [rbx+0x188] */
	ret &= plugin.kextMembers.IONVMeController.fProposedPowerState.fromFunc(
				plugin.kextFuncs.IONVMeController.ThreadEntry.fptr, 0x8b, 1, 3);
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
	 */
	IOLockLock(plugin.lck);
	auto entry = plugin.entryForController(static_cast<IOService*>(controller));

	/* Our IOPM tables are ready, so we have our PM working */
	if (entry && entry->powerStates) {
		auto& pps = plugin.kextMembers.IONVMeController.fProposedPowerState.get(controller);
		assertf(pps < entry->nstates, "fProposedPowerState not in PM table");

		if (entry->powerStates[pps].capabilityFlags & kIOPMDeviceUsable)
			aps = pps;
		else
			aps = entry->nstates - 1;
	}
	IOLockUnlock(plugin.lck);

	if (aps)
		return aps;
	else
		return plugin.kextFuncs.IONVMeController.GetActivePowerState(controller);
}

uint64_t NVMeFixPlugin::PM::initialPowerStateForDomainState(void* controller, uint64_t domainState) {
	return GetActivePowerState(controller);
}

bool NVMeFixPlugin::PM::activityTickle(void* controller, unsigned long type, unsigned long stateNumber) {
	uint64_t aps {};
	auto& plugin = NVMeFixPlugin::globalPlugin();

	DBGLOG("pm", "activityTickle 0x%x", stateNumber);

	IOLockLock(plugin.lck);
	auto entry = plugin.entryForController(static_cast<IOService*>(controller));

	if (entry && entry->powerStates)
		aps = entry->nstates - 1;
	IOLockUnlock(plugin.lck);

	if (!aps)
		aps = stateNumber;
	return plugin.kextFuncs.IONVMeController.activityTickle(controller, type, aps);
}

void NVMeFixPlugin::PM::ThreadEntry(void* controller) {
	auto& plugin = NVMeFixPlugin::globalPlugin();
	SYSLOG("pm", "ThreadEntry: fProposedPowerState 0x%x",
		   plugin.kextMembers.IONVMeController.fProposedPowerState.get(controller));
	return plugin.kextFuncs.IONVMeController.ThreadEntry(controller);
}
