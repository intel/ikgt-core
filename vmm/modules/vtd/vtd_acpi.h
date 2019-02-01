/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VTD_ACPI_H_
#define _VTD_ACPI_H_

typedef union {
	struct {
		uint64_t nd         : 3;
		uint64_t afl        : 1;
		uint64_t rwbf       : 1;
		uint64_t pmlr       : 1;
		uint64_t pmhr       : 1;
		uint64_t cm         : 1;
		uint64_t sagaw      : 5;
		uint64_t rsvd0      : 3;
		uint64_t mgaw       : 6;
		uint64_t zlr        : 1;
		uint64_t isoch      : 1;
		uint64_t fro_low    : 8;
		uint64_t fro_high   : 2;
		uint64_t sllps      : 4;
		uint64_t rsvd1      : 1;
		uint64_t psi        : 1;
		uint64_t nfr        : 8;
		uint64_t mamv       : 6;
		uint64_t dwd        : 1;
		uint64_t drd        : 1;
		uint64_t rsvd2      : 8;
	} bits;
	uint64_t uint64;
} vtd_cap_reg_t;

typedef union {
	struct {
		uint64_t c      : 1;
		uint64_t qi     : 1;
		uint64_t dt     : 1;
		uint64_t ir     : 1;
		uint64_t eim    : 1;
		uint64_t rsvd   : 1;
		uint64_t pt     : 1;
		uint64_t sc     : 1;
		uint64_t iro    : 10;
		uint64_t rsvd1  : 2;
		uint64_t mhmv   : 4;
		uint64_t ecs    : 1;
		uint64_t mts    : 1;
		uint64_t nest   : 1;
		uint64_t dis    : 1;
		uint64_t rsvd2  : 1;
		uint64_t prs    : 1;
		uint64_t ers    : 1;
		uint64_t srs    : 1;
		uint64_t rsvd3  : 1;
		uint64_t nwfs   : 1;
		uint64_t eafs   : 1;
		uint64_t pss    : 5;
		uint64_t pasid  : 1;
		uint64_t dit    : 1;
		uint64_t pds    : 1;
		uint64_t rsvd4  : 21;
	} bits;
	uint64_t uint64;
} vtd_ext_cap_reg_t;

typedef struct {
	uint64_t reg_base_hva;
	uint64_t reg_base_hpa;
	vtd_cap_reg_t cap;
	vtd_ext_cap_reg_t ecap;
} vtd_engine_t;

void vtd_dmar_parse(vtd_engine_t *engine_list);

#endif

