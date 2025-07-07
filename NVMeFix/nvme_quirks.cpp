//
// @file nvme_quirks.cpp
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

#include "Log.hpp"
#include "nvme_quirks.hpp"

#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <kern/assert.h>
#include <Headers/kern_efi.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_nvram.hpp>
#include <Headers/kern_util.hpp>

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device tables which are exported to userspace via
 * scripts/mod/file2alias.c.  You must keep that file in sync with this
 * header.
 */

/* linux/include/mod_devicetable.h */

/**
 * struct pci_device_id - PCI device ID structure
 * @vendor:		Vendor ID to match (or PCI_ANY_ID)
 * @device:		Device ID to match (or PCI_ANY_ID)
 * @subvendor:		Subsystem vendor ID to match (or PCI_ANY_ID)
 * @subdevice:		Subsystem device ID to match (or PCI_ANY_ID)
 * @class:		Device class, subclass, and "interface" to match.
 *			See Appendix D of the PCI Local Bus Spec or
 *			include/linux/pci_ids.h for a full list of classes.
 *			Most drivers do not need to specify class/class_mask
 *			as vendor/device is normally sufficient.
 * @class_mask:		Limit which sub-fields of the class field are compared.
 *			See drivers/scsi/sym53c8xx_2/ for example of usage.
 * @driver_data:	Data private to the driver.
 *			Most drivers don't need to use driver_data field.
 *			Best practice is to use driver_data as an index
 *			into a static list of equivalent device types,
 *			instead of using it as a pointer.
 */
struct pci_device_id {
	linux_types::__u32 vendor, device;		/* Vendor and device ID or PCI_ANY_ID*/
	// linux_types::__u32 subvendor, subdevice;	/* Subsystem ID's or PCI_ANY_ID */
	// linux_types::__u32 /* class */ cls, class_mask;	/* (class,subclass,prog-if) triplet */
	/* kernel_ulong_t */ unsigned long driver_data;	/* Data private to the driver */
};

// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 */

/* linux/drivers/nvme/host/pci.c */

namespace NVMe {

static constexpr struct pci_device_id nvme_id_table[] = {
	{ 0x8086, 0x0953,
		NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ 0x8086, 0x0a53,
		NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ 0x8086, 0x0a54,
		NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ 0x8086, 0x0a55,
		NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ 0x8086, 0xf1a5,   /* Intel 600P/P3100 */
		NVME_QUIRK_NO_DEEPEST_PS |
				NVME_QUIRK_MEDIUM_PRIO_SQ },
	{ 0x8086, 0xf1a6,   /* Intel 760p/Pro 7600p */
		NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ 0x8086, 0x5845,   /* Qemu emulated controller */
		NVME_QUIRK_IDENTIFY_CNS |
				NVME_QUIRK_DISABLE_WRITE_ZEROES, },
	{ 0x1bb1, 0x0100,   /* Seagate Nytro Flash Storage */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x1c58, 0x0003,   /* HGST adapter */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x1c58, 0x0023,   /* WDC SN200 adapter */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x1c5f, 0x0540,   /* Memblaze Pblaze4 adapter */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x144d, 0xa821,   /* Samsung PM1725 */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x144d, 0xa822,   /* Samsung PM1725a */
		NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ 0x1d1d, 0x1f1f,   /* LighNVM qemu device */
		NVME_QUIRK_LIGHTNVM, },
	{ 0x1d1d, 0x2807,   /* CNEX WL */
		NVME_QUIRK_LIGHTNVM, },
	{ 0x1d1d, 0x2601,   /* CNEX Granby */
		NVME_QUIRK_LIGHTNVM, },
	{ 0x10ec, 0x5762,   /* ADATA SX6000LNP */
		NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ 0x1cc1, 0x8201,   /* ADATA SX8200PNP 512GB */
		NVME_QUIRK_NO_DEEPEST_PS |
				NVME_QUIRK_IGNORE_DEV_SUBNQN, }, 
        /* patch here */
        {0x1d97, 0x2269, /* Lexar NM760 */    NVME_QUIRK_IGNORE_DEV_SUBNQN,},
        {0x1e49, 0x0021, /* ZHITAI TiPro5000 NVMe SSD */  NVME_QUIRK_NO_DEEPEST_PS,},
        {0x1e49, 0x0041, /* ZHITAI TiPro7000 NVMe SSD */  NVME_QUIRK_NO_DEEPEST_PS,},
        {0x2646, 0x2262, /* KINGSTON SKC2000 NVMe SSD */  NVME_QUIRK_NO_DEEPEST_PS,},
        {0x2646, 0x2263, /* KINGSTON A2000 NVMe SSD  */  NVME_QUIRK_NO_DEEPEST_PS,},
        {0x2646, 0x5016, /* KINGSTON OM3PGP4xxxxP OS21011 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x2646, 0x5018, /* KINGSTON OM8SFP4xxxxP OS21012 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x2646, 0x501A, /* KINGSTON OM8PGP4xxxxP OS21005 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x2646, 0x501B, /* KINGSTON OM8PGP4xxxxQ OS21005 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x2646, 0x501E, /* KINGSTON OM3PGP4xxxxQ OS21011 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x126f, 0x2262, /* Silicon Motion generic */  NVME_QUIRK_NO_DEEPEST_PS ,},
        {0x1344, 0x5407, /* Micron Technology Inc NVMe SSD */  NVME_QUIRK_IGNORE_DEV_SUBNQN},
        {0x144d, 0xa809, /* Samsung MZALQ256HBJD 256G */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x144d, 0xa80b, /* Samsung PM9B1 256G and 512G */  NVME_QUIRK_DISABLE_WRITE_ZEROES ,},
        {0x15b7, 0x2001, /*  Sandisk Skyhawk */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x1987, 0x5016, /* Phison E16 */  NVME_QUIRK_IGNORE_DEV_SUBNQN ,},
        {0x1987, 0x5019, /* phison E19 */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x1987, 0x5021, /* Phison E21 */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x1c5c, 0x1504, /* SK Hynix PC400 */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x1c5c, 0x1D59, /* SK Hynix BC901 */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
	{0x1c5c, 0x2849, /* PE81x0 U.2/3 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x243B, /* PE6110 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x2839, /* PE8000 Series NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x2204, /* 960GB TLC PCIe Gen3 x4 NVMe M.2 22110 */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x2429, /* PE6011 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x174A, /* Gold P31/BC711/PC711 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1639, /* PC611 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1739, /* BC701 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1959, /* Platinum P41/PC801 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1D59, /* BC901 NVMe Solid State Drive (DRAM-less) */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1285, /* PC300 NVMe Solid State Drive 1TB */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1327, /* BC501 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1504, /* PC400 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1527, /* PC401 NVMe Solid State Drive 256GB */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1339, /* BC511 NVMe SSD */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1627, /* PC601 NVMe Solid State Drive */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1283, /* PC300 NVMe Solid State Drive 256GB */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1284, /* PC300 NVMe Solid State Drive 512GB */  NVME_QUIRK_DISABLE_WRITE_ZEROES,}, 
	{0x1c5c, 0x1282, /* PC300 NVMe Solid State Drive 128GB */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},                 
        {0x1cc4, 0x6302, /* UMIS RPJTJ256MGE1QDY 256G */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},
        {0x1cc4, 0x6303, /* UMIS RPJTJ512MGE1QDY 512G */  NVME_QUIRK_DISABLE_WRITE_ZEROES,},        
	/* Should be taken care of by IONVMeFamily */
#if 0
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2001) },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2003) },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2005),
		.driver_data = NVME_QUIRK_SINGLE_VECTOR |
				NVME_QUIRK_128_BYTES_SQES |
				NVME_QUIRK_SHARED_TAGS },
#endif
	{ 0, }
};

/**
 * FIXME: This will only work with Clover
 */
static nvme_quirks check_vendor_combination_bug(uint32_t vendor, uint32_t device) {
	auto platform = IORegistryEntry::fromPath("/efi/platform", gIODTPlane);
	unsigned ret = NVME_QUIRK_NONE;

	char vendorName[64];
	char productName[64];
	char boardName[64];

	bool foundVendor {false}, foundProduct {false}, foundBoard {false};

	auto getStrProp = [](auto& platform, auto name, auto& res) {
		auto ret = OSDynamicCast(OSData, platform->getProperty(name));
	
		if (ret && ret->getLength() > 0 && ret->getBytesNoCopy()) {
			auto sz = min(ret->getLength(), sizeof(res));
			lilu_os_memcpy(res, ret->getBytesNoCopy(), sz);
			res[sz - 1] = '\0';
			DBGLOG(Log::Quirks, "Found %s = %s", name, res);

			return true;
		} else {
			DBGLOG(Log::Quirks, "Failed to find IODeviceTree:/efi/platform %s", name);
			return false;
		}
	};
	
	/* Strings in NVRAM are not NUL-terminated */
	auto getEFIProp = [](auto& ser, auto name, auto& res) {
		uint32_t attr;
		uint64_t szRead {sizeof(res)};
		auto status = ser->getVariable(name, &EfiRuntimeServices::LiluVendorGuid, &attr, &szRead, res);
		if (status != EFI_SUCCESS)
			DBGLOG(Log::Quirks, "Failed to find LiluVendorGuid:%s", name);
		else {
			res[min(sizeof(res) - 1, szRead)] = '\0';
			DBGLOG(Log::Quirks, "Found LiluVendorGuid:%s = %s", name, res);
		}
		
		return status == EFI_SUCCESS;
	};
	
	auto getNVRAMProp = [](auto& storage, auto name, auto& res) {
		uint32_t szRead {sizeof(res)};
		auto data = storage.read(name, szRead);
		if (!data) {
			DBGLOG(Log::Quirks, "Failed to find LiluVendorGuid:%s", name);
			return false;
		} else {
			auto sz = min(szRead, sizeof(res));
			lilu_os_memcpy(res, data, sz);
			res[min(szRead, sizeof(res) - 1)] = '\0';
			Buffer::deleter(data);
			DBGLOG(Log::Quirks, "Found LiluVendorGuid:%s = %s", name, res);
			return true;
		}
	};

	if (platform) {
		DBGLOG(Log::Quirks, "Reading OEM info from IODT");

		foundProduct = getStrProp(platform, "OEMProduct", productName);
		foundVendor = getStrProp(platform, "OEMVendor", vendorName);
		foundBoard = getStrProp(platform, "OEMBoard", boardName);
	} if (!foundProduct || !foundVendor || !foundBoard) {
		DBGLOG(Log::Quirks, "Reading OEM info from NVRAM");

		NVStorage storage;
		if (storage.init()) {
			foundProduct = getNVRAMProp(storage, "oem-product", productName);
			foundVendor = getNVRAMProp(storage, "oem-vendor", vendorName);
			foundBoard = getNVRAMProp(storage, "oem-board", boardName);

			storage.deinit();
		} else {
			auto ser = EfiRuntimeServices::get();
			if (!ser)
				DBGLOG(Log::Quirks, "Failed to get EFI services");
			else {
				foundProduct = getEFIProp(ser, u"oem-product", productName);
				foundVendor = getEFIProp(ser, u"oem-vendor", vendorName);
				foundBoard = getEFIProp(ser, u"oem-board", boardName);
			}
		}
	}

	if (vendor == 0x144d && device == 0xa802 && foundProduct && foundVendor) {
		/*
		 * Several Samsung devices seem to drop off the PCIe bus
		 * randomly when APST is on and uses the deepest sleep state.
		 * This has been observed on a Samsung "SM951 NVMe SAMSUNG
		 * 256GB", a "PM951 NVMe SAMSUNG 512GB", and a "Samsung SSD
		 * 950 PRO 256GB", but it seems to be restricted to two Dell
		 * laptops.
		 */
		if (!strcmp("Dell Inc.", vendorName) && (!strcmp(productName, "XPS 15 9550") ||
												 !strcmp(productName, "Precision 5510")))
			ret |= NVME_QUIRK_NO_DEEPEST_PS;
	} else if (vendor == 0x144d && device == 0xa804 && foundVendor && foundBoard) {
		/*
		 * Samsung SSD 960 EVO drops off the PCIe bus after system
		 * suspend on a Ryzen board, ASUS PRIME B350M-A, as well as
		 * within few minutes after bootup on a Coffee Lake board -
		 * ASUS PRIME Z370-A
		 */
		if (!strcmp(vendorName, "ASUSTeK COMPUTER INC.") && (!strcmp(boardName, "PRIME B350M-A") ||
															!strcmp(boardName, "PRIME Z370-A")))
			ret |=  NVME_QUIRK_NO_APST;
	}

	if (platform)
		platform->release();

	return static_cast<nvme_quirks>(ret);
}

nvme_quirks quirksForController(IOService* controller) {
	assert(controller);

	uint32_t vendor {0}, device {0};
	propertyFromParent(controller, "vendor-id", vendor);
	propertyFromParent(controller, "device-id", device);

	auto parent = controller->getParentEntry(gIOServicePlane);
	if (!parent || !parent->metaCast("IOPCIDevice")) {
		DBGLOG(Log::Quirks, "Controller parent is not an IOPCIDevice");
		return NVME_QUIRK_NONE;
	}

	if (!vendor || !device) {
		DBGLOG(Log::Quirks, "Failed to get vendor or device id");
		return NVME_QUIRK_NONE;
	}

	unsigned ret = NVME_QUIRK_NONE;
	for (const auto& entry : nvme_id_table)
		if (vendor == entry.vendor && device == entry.device)
			ret |= entry.driver_data;

	ret |= check_vendor_combination_bug(vendor, device);

	return static_cast<nvme_quirks>(ret);
}

struct nvme_core_quirk_entry {
	/*
	 * NVMe model and firmware strings are padded with spaces.  For
	 * simplicity, strings in the quirk table are padded with NULLs
	 * instead.
	 */
	linux_types::__u16 vid;
	const char *mn;
	const char *fr;
	unsigned long quirks;
};

static const struct nvme_core_quirk_entry core_quirks[] = {
	{
		/*
		 * This Toshiba device seems to die using any APST states.  See:
		 * https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1678184/comments/11
		 */
		0x1179,
		"THNSF5256GPUK TOSHIBA",
		nullptr,
		NVME_QUIRK_NO_APST,
	},
	{
		/*
		 * This LiteON CL1-3D*-Q11 firmware version has a race
		 * condition associated with actions related to suspend to idle
		 * LiteON has resolved the problem in future firmware
		 */
		0x14a4,
		nullptr,
		"22301111",
		NVME_QUIRK_SIMPLE_SUSPEND,
	},
	{
		/*
		 * This Kingston E8FK11.T firmware version has no interrupt
		 * after resume with actions related to suspend to idle
		 * https://bugzilla.kernel.org/show_bug.cgi?id=204887
		 */
		0x2646,
		nullptr,
		"E8FK11",
		NVME_QUIRK_SIMPLE_SUSPEND,
	},
	{
		/*
		 * Kingston A2000 devices with 5Z42105 firmware can become
		 * unresponsive after entering the deepest power state
		 * https://lore.kernel.org/linux-nvme/20210129052442.310780-1-linux@leemhuis.info/
		 */
		0x2646,
		nullptr,
		"S5Z42105",
		NVME_QUIRK_NO_DEEPEST_PS,
	},
};

template <size_t S>
static bool id_ctrl_match(const char* str, const char (&id_str)[S]) {
	if (str == nullptr)
		return true;

	/* Strings in the core quirk table end with NULL */
	auto i = strlen(str);

	if (i > S || memcmp(str, id_str, i))
		return false;

	/* Controller identity strings in struct nvme_id_ctrl are padded with spaces */
	while (i < S) {
		if (id_str[i++] != 0x20)
			return false;
	}

	return true;
}

nvme_quirks quirksForController(uint16_t vid, mn_ref_t mn, fr_ref_t fr) {
	unsigned ret {NVME_QUIRK_NONE};

	for (const auto& entry : core_quirks) {
		auto match {true};
		match &= !entry.vid || entry.vid == vid;
		match &= id_ctrl_match(entry.mn, mn);
		match &= id_ctrl_match(entry.fr, fr);

		if (match)
			ret |= entry.quirks;
	}

	return static_cast<nvme_quirks>(ret);
}
}
