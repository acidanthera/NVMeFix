//
// @file NVMeFixPlugin.hpp
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


#ifndef NVMeFixPlugin_h
#define NVMeFixPlugin_h

#include <stdint.h>

#include <Library/LegacyIOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pwr_mgt/IOPMpowerState.h>
#include <Headers/kern_patcher.hpp>

#include "nvme.h"
#include "nvme_quirks.hpp"

class NVMeFixPlugin {
public:
	void init();
	void deinit();
	static NVMeFixPlugin& globalPlugin();

private:
	static void processKext(void*, KernelPatcher&, size_t, mach_vm_address_t, size_t);
	static bool matchingNotificationHandler(void*, void*, IOService*, IONotifier*);
	static bool terminatedNotificationHandler(void*, void*, IOService*, IONotifier*);
	bool solveSymbols(KernelPatcher& kp);

	bool solvedSymbols {false};

	IONotifier* matchingNotifier {nullptr}, * terminationNotifier {nullptr};

	/* Used for synchronising concurrent access to this class from notification handlers */
	IOLock* lck {nullptr};

	const char* kextPath {
		"/System/Library/Extensions/IONVMeFamily.kext/Contents/MacOS/IONVMeFamily"
	};

	KernelPatcher::KextInfo kextInfo {
		"com.apple.iokit.IONVMeFamily",
		&kextPath,
		1,
		{true},
		{},
		KernelPatcher::KextInfo::Unloaded
	};

	struct {
		template <typename T, typename... Args>
		struct Func {
			const char* const name;
			mach_vm_address_t fptr {};

			bool solve(KernelPatcher& kp, size_t idx) {
				/* Cache the result */
				if (!fptr)
					fptr = kp.solveSymbol(idx, name);
				return fptr;
			}

			bool route(KernelPatcher& kp, size_t idx, T(*repl)(Args...)) {
				KernelPatcher::RouteRequest request(name, repl, fptr);
				return kp.routeMultiple(idx, &request, 1);
			}

			T operator()(Args&&... args) {
				assertf(fptr, "%s not solved", name);
				return (*reinterpret_cast<T(*)(Args...)>(fptr))(args...);
			}
		};

		struct {
			Func<IOReturn,void*,IOMemoryDescriptor*,void*,uint64_t> IssueIdentifyCommand {
				"__ZN16IONVMeController20IssueIdentifyCommandEP18IOMemoryDescriptorP16AppleNVMeRequestj"
			};

			Func<IOReturn,void*,void*> ProcessSyncNVMeRequest {
				"__ZN16IONVMeController22ProcessSyncNVMeRequestEP16AppleNVMeRequest"
			};

			Func<void*,void*,uint64_t> GetRequest {
				"__ZN16IONVMeController10GetRequestEj"
			};
			Func<void,void*,void*> ReturnRequest {
				"__ZN16IONVMeController13ReturnRequestEP16AppleNVMeRequest"
			};
			Func<uint64_t,void*,uint64_t,void*> setPowerState {
				"__ZN16IONVMeController13setPowerStateEmP9IOService"
			};
			Func<uint64_t,void*> GetActivePowerState {
				"__ZN16IONVMeController19GetActivePowerStateEv"
			};
			Func<IOPMPowerState*,void*> ReturnPowerStatesArray {
				"__ZN16IONVMeController22ReturnPowerStatesArrayEv"
			};
		} IONVMeController;

		struct {
			Func<void,void*,uint8_t> BuildCommandGetFeatures {
				"__ZN16AppleNVMeRequest23BuildCommandGetFeaturesEh"
			};

			Func<void,void*,uint8_t> BuildCommandSetFeaturesCommon {
				"__ZN16AppleNVMeRequest29BuildCommandSetFeaturesCommonEh"
			};

			Func<uint32_t,void*> GetStatus {
				"__ZN16AppleNVMeRequest9GetStatusEv"
			};

			Func<uint32_t,void*> GetOpcode {
				"__ZN16AppleNVMeRequest9GetOpcodeEv"
			};

			Func<IOReturn,void*,uint64_t,uint64_t> GenerateIOVMSegments {
				"__ZN16AppleNVMeRequest20GenerateIOVMSegmentsEyy"
			};
		} AppleNVMeRequest;
	} kextFuncs;


	/**
	 * Controller writes operation result to a member of AppleNVMeRequest which
	 * is then directly accessed by client code. Unlike the other request members,
	 * there is no wrapper, but it always seem to follow uint32_t status member in request
	 * struct, so we rely on that.
	 */
	struct {
		template <typename T>
		struct Member {
			mach_vm_address_t offs {};
			T* get(void* obj) {
				assert(offs);
				assert(obj);

				return reinterpret_cast<T*>(static_cast<uint8_t*>(obj) + offs);
			}
		};

		struct {
			Member<uint32_t> result;
			Member<void*> controller;
			Member<NVMe::nvme_command> command;
			Member<IOBufferMemoryDescriptor*> prpDescriptor;
		} AppleNVMeRequest;
	} kextMembers;

	/*
	 * How far should we traverse ioreg searching for parent NVMe controller
	 * Typical depth is 9 on real setups
	 */
	static constexpr int controllerSearchDepth {20};

	struct ControllerEntry {
		IOService* controller {nullptr};
		bool processed {false};
		NVMe::nvme_quirks quirks {NVMe::NVME_QUIRK_NONE};
		uint64_t ps_max_latency_us {100000};
		IOPMPowerState* powerStates {nullptr};
	};

	evector<ControllerEntry> controllers;
	void handleControllers();
	void handleController(ControllerEntry&);
	IOReturn identify(ControllerEntry&,IOBufferMemoryDescriptor*&);
	IOReturn configureAPST(ControllerEntry&,const NVMe::nvme_id_ctrl*);
	IOReturn APSTenabled(ControllerEntry&, bool&);
	IOReturn dumpAPST(ControllerEntry&, int npss);
	IOReturn NVMeFeatures(ControllerEntry&, unsigned fid, unsigned* dword11, IOBufferMemoryDescriptor* desc,
							 uint32_t* res, bool set);

	struct PM {
		bool init(ControllerEntry&,const NVMe::nvme_id_ctrl*);
		bool solveSymbols();
		IOPMPowerState* statesWithTable(const NVMe::nvme_id_ctrl*) const;

		static uint64_t setPowerState(void*,uint64_t,void*);
		static uint64_t GetActivePowerState(void*);
		static IOPMPowerState* ReturnPowerStatesArray(void*);
	} PM;
};


#endif /* NVMeFixPlugin_h */
