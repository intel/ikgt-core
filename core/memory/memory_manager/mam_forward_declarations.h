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

/*
 *
 * Forward declaration of internal functions of Memroy Address Mapper
 *
 */
#ifndef MAM_FORWAD_DECLARATIONS_H
#define MAM_FORWAD_DECLARATIONS_H

static
uint64_t mam_get_size_covered_by_level1_entry(void);

static
uint64_t mam_get_size_covered_by_level2_entry(void);

static
uint64_t mam_get_size_covered_by_level3_entry(void);

static
uint64_t mam_get_size_covered_by_level4_entry(void);

static
uint32_t mam_get_level1_entry_index(IN uint64_t address);

static
uint32_t mam_get_level2_entry_index(IN uint64_t address);

static
uint32_t mam_get_level3_entry_index(IN uint64_t address);

static
uint32_t mam_get_level4_entry_index(IN uint64_t address);

static
const mam_level_ops_t *mam_get_non_existing_ops(void);

static
const mam_level_ops_t *mam_get_level1_ops(void);

static
const mam_level_ops_t *mam_get_level2_ops(void);

static
const mam_level_ops_t *mam_get_level3_ops(void);

static
const mam_level_ops_t *mam_get_level4_ops(void);

static
uint64_t mam_get_address_from_leaf_internal_entry(IN mam_entry_t *entry,
						  IN const mam_level_ops_t *
						  level_ops);

static
uint64_t mam_get_address_from_leaf_page_table_entry(IN mam_entry_t *entry,
						    IN const mam_level_ops_t *
						    level_ops);

static
uint64_t mam_get_address_from_leaf_ept_entry(IN mam_entry_t *entry,
					     IN const mam_level_ops_t *level_ops);

static
uint64_t mam_get_address_from_leaf_vtdpt_entry(IN mam_entry_t *entry,
					       IN const mam_level_ops_t *
					       level_ops);

static
mam_attributes_t mam_get_attributes_from_internal_entry(IN mam_entry_t *entry,
							IN const mam_level_ops_t *
							level_ops);

static
mam_attributes_t mam_get_attributes_from_page_table_entry(IN mam_entry_t *entry,
							  IN const mam_level_ops_t *
							  level_ops);

static
mam_attributes_t mam_get_attributes_from_ept_entry(IN mam_entry_t *entry,
						   IN const mam_level_ops_t *
						   level_ops);

static
mam_attributes_t mam_get_attributes_from_vtdpt_entry(IN mam_entry_t *entry,
						     IN const mam_level_ops_t *
						     level_ops);

static
mam_hav_t mam_get_table_pointed_by_internal_enty(IN mam_entry_t *entry);

static
mam_hav_t mam_get_table_pointed_by_page_table_entry(IN mam_entry_t *entry);

static
mam_hav_t mam_get_table_pointed_by_ept_entry(IN mam_entry_t *entry);

static
mam_hav_t mam_get_table_pointed_by_vtdpt_entry(IN mam_entry_t *entry);

static
boolean_t mam_is_internal_entry_present(IN mam_entry_t *entry);

static
boolean_t mam_is_page_table_entry_present(IN mam_entry_t *entry);

static
boolean_t mam_is_ept_entry_present(IN mam_entry_t *entry);

static
boolean_t mam_is_vtdpt_entry_present(IN mam_entry_t *entry);

static
boolean_t mam_can_be_leaf_internal_entry(IN mam_t *mam,
					 IN const mam_level_ops_t *level_ops,
					 IN uint64_t requested_size,
					 IN uint64_t tgt_addr);

static
boolean_t mam_can_be_leaf_page_table_entry(IN mam_t *mam,
					   IN const mam_level_ops_t *level_ops,
					   IN uint64_t requested_size,
					   IN uint64_t tgt_addr);

static
boolean_t mam_can_be_leaf_ept_entry(IN mam_t *mam,
				    IN const mam_level_ops_t *level_ops,
				    IN uint64_t requested_size,
				    IN uint64_t tgt_addr);

static
boolean_t mam_can_be_leaf_vtdpt_entry(IN mam_t *mam,
				      IN const mam_level_ops_t *level_ops,
				      IN uint64_t requested_size,
				      IN uint64_t tgt_addr);

static
void mam_update_leaf_internal_entry(IN mam_entry_t *entry,
				    IN uint64_t addr,
				    IN mam_attributes_t attr,
				    IN const mam_level_ops_t *level_ops);

static
void mam_update_leaf_page_table_entry(IN mam_entry_t *entry,
				      IN uint64_t addr,
				      IN mam_attributes_t attr,
				      IN const mam_level_ops_t *level_ops);

static
void mam_update_leaf_ept_entry(IN mam_entry_t *entry,
			       IN uint64_t addr,
			       IN mam_attributes_t attr,
			       IN const mam_level_ops_t *level_ops);

static
void mam_update_leaf_vtdpt_entry(IN mam_entry_t *entry,
				 IN uint64_t addr,
				 IN mam_attributes_t attr,
				 IN const mam_level_ops_t *level_ops);

static
void mam_update_inner_internal_entry(mam_t *mam,
				     mam_entry_t *entry,
				     mam_hav_t next_table,
				     const mam_level_ops_t *level_ops);

static
void mam_update_inner_page_table_entry(mam_t *mam,
				       mam_entry_t *entry,
				       mam_hav_t next_table,
				       const mam_level_ops_t *level_ops);

static
void mam_update_inner_ept_entry(mam_t *mam,
				mam_entry_t *entry,
				mam_hav_t next_table,
				const mam_level_ops_t *level_ops);

static
void mam_update_inner_vtdpt_entry(mam_t *mam,
				  mam_entry_t *entry,
				  mam_hav_t next_table,
				  const mam_level_ops_t *level_ops);

static
void mam_update_attributes_in_leaf_internal_entry(mam_entry_t *entry,
						  mam_attributes_t attrs,
						  const mam_level_ops_t *
						  level_ops);

static
void mam_update_attributes_in_leaf_page_table_entry(mam_entry_t *entry,
						    mam_attributes_t attrs,
						    const mam_level_ops_t *
						    level_ops);

static
void mam_update_attributes_in_leaf_ept_entry(mam_entry_t *entry,
					     mam_attributes_t attrs,
					     const mam_level_ops_t *level_ops);

static
void mam_update_attributes_in_leaf_vtdpt_entry(mam_entry_t *entry,
					       mam_attributes_t attrs,
					       const mam_level_ops_t *level_ops);

static
mam_entry_type_t mam_get_leaf_internal_entry_type(void);

static
mam_entry_type_t mam_get_leaf_page_table_entry_type(void);

static
mam_entry_type_t mam_get_leaf_ept_entry_type(void);

static
mam_entry_type_t mam_get_leaf_vtdpt_entry_type(void);

static
void mam_destroy_table(IN mam_hav_t table);

#endif
