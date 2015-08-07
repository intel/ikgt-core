/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef _8259A_PIC_H
#define _8259A_PIC_H

#include "mon_defs.h"

/*************************************************************************
*
* 8259A Prigrammable Interrupt Controller (legacy)
*
*************************************************************************/

/*
 * Init PIC support. Accesses HW PIC and changes its Read Register Command
 * state
 * to IRR read.
 * Registers to ICW3 port writes for the given guest to trace
 * Read Register Command state changes. */
void pic_init(guest_id_t pic_owner);

/* Test for ready-to-be-accepted fixed interrupts. */
boolean_t pic_is_ready_interrupt_exist(void);

#endif
