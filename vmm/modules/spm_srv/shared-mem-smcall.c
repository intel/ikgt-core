/*
 * Copyright (c) 2020, ARM Limited and Contributors. All rights reserved.
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "modules/spm_srv.h"
#include "shared-mem-smcall.h"
#include "lib/util.h"
#include "errno.h"

/*
 * Use a 512KB buffer by default for shared memory descriptors. Set
 * TRUSTY_SHARED_MEMORY_OBJ_SIZE in platform_def.h to use a different value.
 */
#ifndef TRUSTY_SHARED_MEMORY_OBJ_SIZE
#define TRUSTY_SHARED_MEMORY_OBJ_SIZE (512 * 1024)
#endif

/* Definition copied from ARM trusted firmware -- begin -- */
#define SMC_UNK             -1ULL

#define MT_DEVICE           (0u)
#define MT_NON_CACHEABLE    (1u)
#define MT_MEMORY           (2u)

/* Access permissions (RO/RW) */
#define MT_PERM_SHIFT       (3u)
/* Access permissions for instruction execution (EXECUTE/EXECUTE_NEVER) */
#define MT_EXECUTE_SHIFT    (5u)

#define MT_RO               ((0u) << MT_PERM_SHIFT)
#define MT_RW               ((1u) << MT_PERM_SHIFT)

#define MT_EXECUTE          ((0u) << MT_EXECUTE_SHIFT)
#define MT_EXECUTE_NEVER    ((1u) << MT_EXECUTE_SHIFT)

/* Compound attributes for most common usages */
#define MT_CODE             (MT_MEMORY | MT_RO | MT_EXECUTE)
#define MT_RO_DATA          (MT_MEMORY | MT_RO | MT_EXECUTE_NEVER)
#define MT_RW_DATA          (MT_MEMORY | MT_RW | MT_EXECUTE_NEVER)
/* Definition copied from ARM trusted firmware -- end -- */

/* Convenience macros to return from SMC handler */
#define SMC_RET1(gcpu, ret, r0)	{   \
	smc_ret1(gcpu, r0);             \
	return (int)(long)(ret);        \
}

#define SMC_RET8(gcpu, ret, r0, r1, r2, r3, r4, r5, r6, r7) { \
	smc_ret8(gcpu, r0, r1, r2, r3, r4, r5, r6, r7);           \
	return (int)(long)(ret);                                  \
}

/**
 * struct trusty_shmem_obj - Shared memory object.
 * @desc_size:      Size of @desc.
 * @desc_filled:    Size of @desc already received.
 * @in_use:         Number of clients that have called ffa_mem_retrieve_req
 *                  without a matching ffa_mem_relinquish call.
 * @desc:           FF-A memory region descriptor passed in ffa_mem_share.
 */
struct trusty_shmem_obj {
	uint64_t desc_size;
	uint64_t desc_filled;
	uint64_t in_use;
	struct ffa_mtd desc;
};

/**
 * struct trusty_shmem_obj_state - Global state.
 * @data:           Backing store for trusty_shmem_obj objects.
 * @allocated:      Number of bytes allocated in @data.
 * @next_handle:    Handle used for next allocated object.
 * @lock:           Lock protecting all state in this file.
 */
struct trusty_shmem_obj_state {
	uint8_t data[TRUSTY_SHARED_MEMORY_OBJ_SIZE];
	uint64_t allocated;
	uint64_t next_handle;
	//struct spinlock lock;
};

/**
 * struct trusty_shmem_client_state - Per client state.
 * @tx_buf:             Client's transmit buffer.
 * @rx_buf:             Client's receive buffer.
 * @buf_size:           Size of @tx_buf and @rx_buf.
 * @secure:             If %TRUE, the client is the secure os.
 * @identity_mapped:    If %TRUE, all client memory is identity mapped.
 * @receiver:           If %TRUE, the client is allowed to receive memory.
 *                      If %false, the client is allowed to send memory.
 */
struct trusty_shmem_client_state {
	const void *tx_buf;
	void *rx_buf;
	uint64_t buf_size;
	const boolean_t secure;
	const boolean_t identity_mapped;
	const boolean_t receiver;
	char pad[4];
};

static struct trusty_shmem_obj_state trusty_shmem_obj_state = {
	/* Set start value for handle so top 32 bits are needed quickly */
	.next_handle = 0xffffffc0,
};

static struct trusty_shmem_client_state trusty_shmem_client_state[2] = {
	{
		.secure   = FALSE,
		.receiver = FALSE,
	},
	{
		.secure          = TRUE,
		.identity_mapped = TRUE,
		.receiver        = TRUE,
	}
};

static struct _world_handle {
    uint64_t handle;
} world_handle[2];

/**
 * trusty_shmem_obj_size - Convert from descriptor size to object size.
 * @desc_size:  Size of struct ffa_memory_region_descriptor object.
 *
 * Return: Size of struct trusty_shmem_obj object.
 */
static uint64_t trusty_shmem_obj_size(uint64_t desc_size) {
	return desc_size + offsetof(struct trusty_shmem_obj, desc);
}

/**
 * trusty_shmem_obj_alloc - Allocate struct trusty_shmem_obj.
 * @state:      Global state.
 * @desc_size:  Size of struct ffa_memory_region_descriptor object that
 *              allocated object will hold.
 *
 * Return: Pointer to newly allocated object, or %NULL if there not enough space
 *         left. The returned pointer is only valid while @state is locked, to
 *         used it again after unlocking @state, trusty_shmem_obj_lookup must be
 *         called.
 */
static struct trusty_shmem_obj *trusty_shmem_obj_alloc(
		struct trusty_shmem_obj_state *state,
		uint64_t desc_size) {
	struct trusty_shmem_obj *obj;
	uint64_t free = sizeof(state->data) - state->allocated;
	if (trusty_shmem_obj_size(desc_size) > free) {
		print_panic("%s(0x%zx) failed, free 0x%zx\n", __func__, desc_size, free);
		return NULL;
	}
	obj = (struct trusty_shmem_obj *)((void *)(state->data + state->allocated));
	obj->desc_size = desc_size;
	obj->desc_filled = 0;
	obj->in_use = 0;
	state->allocated += trusty_shmem_obj_size(desc_size);
	return obj;
}

/**
 * trusty_shmem_obj_free - Free struct trusty_shmem_obj.
 * @state:      Global state.
 * @obj:        Object to free.
 *
 * Release memory used by @obj. Other objects may move, so on return all
 * pointers to struct trusty_shmem_obj object should be considered invalid, not
 * just @obj.
 *
 * The current implementation always compacts the remaining objects to simplify
 * the allocator and to avoid fragmentation.
 */

static void trusty_shmem_obj_free(struct trusty_shmem_obj_state *state,
		struct trusty_shmem_obj *obj) {
	uint64_t free_size = trusty_shmem_obj_size(obj->desc_size);
	uint8_t *shift_dest = (uint8_t *)obj;
	uint8_t *shift_src = shift_dest + free_size;
	uint64_t shift_size = state->allocated - (shift_src - state->data);
	if (shift_size) {
		/* Use memmove instead */
		memcpy(shift_dest, shift_src, shift_size);
	}
	state->allocated -= free_size;
}

/**
 * trusty_shmem_obj_lookup - Lookup struct trusty_shmem_obj by handle.
 * @state:      Global state.
 * @handle:     Unique handle of object to return.
 *
 * Return: struct trusty_shmem_obj_state object with handle matching @handle.
 *         %NULL, if not object in @state->data has a matching handle.
 */
static struct trusty_shmem_obj *
trusty_shmem_obj_lookup(struct trusty_shmem_obj_state *state, uint64_t handle) {
	uint8_t *curr = state->data;
	struct trusty_shmem_obj *obj = NULL;

	while (curr - state->data < (long)state->allocated) {
		obj = (struct trusty_shmem_obj *)((void *)curr);
		if (obj->desc.handle == handle) {
			return obj;
		}
		curr += trusty_shmem_obj_size(obj->desc_size);
	}
	return NULL;
}

static struct ffa_comp_mrd *
trusty_shmem_obj_get_comp_mrd(struct trusty_shmem_obj *obj) {
	return (struct ffa_comp_mrd *)
		((void *)((uint8_t *)(&obj->desc) + obj->desc.emad[0].comp_mrd_offset));
}

/**
 * trusty_shmem_obj_ffa_constituent_size - Calculate variable size part of obj.
 * @obj:    Object containing ffa_memory_region_descriptor.
 *
 * Return: Size of ffa_constituent_memory_region_descriptors in @obj.
 */
static uint64_t
trusty_shmem_obj_ffa_constituent_size(struct trusty_shmem_obj *obj) {
	return trusty_shmem_obj_get_comp_mrd(obj)->address_range_count *
		sizeof(struct ffa_cons_mrd);
}

/**
 * trusty_shmem_check_obj - Check that counts in descriptor match overall size.
 * @obj:    Object containing ffa_memory_region_descriptor.
 *
 * Return: 0 if object is valid, -EINVAL if memory region attributes count is
 * not 1, -EINVAL if constituent_memory_region_descriptor offset or count is
 * invalid.
 */
static int trusty_shmem_check_obj(struct trusty_shmem_obj *obj) {
	uint32_t offset = 0;
	uint64_t size = 0;
	uint64_t count = 0;
	uint64_t header_emad_size = 0;
	uint64_t expected_size = 0;
	uint64_t total_page_count = 0;
	struct ffa_comp_mrd *comp = NULL;

	if (obj->desc.emad_count != 1) {
		print_panic("%s: unsupported attribute desc count %u != 1\n",
				__func__, obj->desc.emad_count);
		return -EINVAL;
	}

	offset = obj->desc.emad[0].comp_mrd_offset;
	header_emad_size = sizeof(obj->desc) +
		obj->desc.emad_count * sizeof(obj->desc.emad[0]);

	if (offset < header_emad_size) {
		print_panic("%s: invalid object, offset %u < header + emad %zu\n",
				__func__, offset, header_emad_size);
		return -EINVAL;
	}

	size = obj->desc_size;
	if (offset > size) {
		print_panic("%s: invalid object, offset %u > total size %zu\n",
				__func__, offset, obj->desc_size);
		return -EINVAL;
	}
	size -= offset;

	if (size < sizeof(struct ffa_comp_mrd)) {
		print_panic("%s: invalid object, offset %u, total size %zu," \
				" no space for header\n",
				__func__, offset, obj->desc_size);
		return -EINVAL;
	}
	size -= sizeof(struct ffa_comp_mrd);

	count = size / sizeof(struct ffa_cons_mrd);

	comp = trusty_shmem_obj_get_comp_mrd(obj);

	if (comp->address_range_count != count) {
		print_panic("%s: invalid object, desc count %u != %zu\n",
				__func__, comp->address_range_count, count);
		return -EINVAL;
	}

	expected_size = offset + sizeof(*comp) +
		trusty_shmem_obj_ffa_constituent_size(obj);
	if (expected_size != obj->desc_size) {
		print_panic("%s: invalid object, computed size %zu != size %zu\n",
				__func__, expected_size, obj->desc_size);
		return -EINVAL;
	}

	if (obj->desc_filled < obj->desc_size) {
		/*
		 * The whole descriptor has not yet been received. Skip final
		 * checks.
		 */
		return 0;
	}

	total_page_count = 0;
	for (uint64_t i = 0; i < count; i++) {
		total_page_count +=
			comp->address_range_array[i].page_count;
	}
	if (comp->total_page_count != total_page_count) {
		print_panic("%s: invalid object, desc total_page_count %u != %zu\n",
				__func__, comp->total_page_count,
				total_page_count);
		return -EINVAL;
	}

	return 0;
}

long trusty_ffa_fill_desc(guest_cpu_handle_t gcpu,
		struct trusty_shmem_client_state *client,
		struct trusty_shmem_obj *obj,
		uint32_t fragment_length,
		void *smc_handle) {
	int ret;
	uint32_t handle_low = 0;
	uint32_t handle_high = 0;

	if (fragment_length > client->buf_size) {
		print_panic("%s: bad fragment size %u > %zu buffer size\n", __func__,
				fragment_length, client->buf_size);
		ret = -EINVAL;
		goto err_arg;
	}

	if (fragment_length > obj->desc_size - obj->desc_filled) {
		print_panic("%s: bad fragment size %u > %zu remaining\n", __func__,
				fragment_length, obj->desc_size - obj->desc_filled);
		return -EINVAL;
	}

	memcpy((uint8_t *)&obj->desc + obj->desc_filled, client->tx_buf,
			fragment_length);

	if (!obj->desc_filled) {
		/* First fragment, descriptor header has been copied */
		obj->desc.handle = trusty_shmem_obj_state.next_handle++;
		obj->desc.flags = FFA_MTD_FLAG_TYPE_SHARE_MEMORY;
	}

	obj->desc_filled += fragment_length;

	ret = trusty_shmem_check_obj(obj);
	if (ret) {
		goto err_bad_desc;
	}

	handle_low = (uint32_t)obj->desc.handle;
	handle_high = obj->desc.handle >> 32;
	if (obj->desc_filled != obj->desc_size) {
		SMC_RET8(gcpu, smc_handle, SMC_FC_FFA_MEM_FRAG_RX, handle_low,
				handle_high, obj->desc_filled,
				obj->desc.sender_id << 16, 0, 0, 0);
	}

	SMC_RET8(gcpu, smc_handle, SMC_FC_FFA_SUCCESS, 0, handle_low, handle_high, 0,
			0, 0, 0);

err_bad_desc:
err_arg:
	trusty_shmem_obj_free(&trusty_shmem_obj_state, obj);
	return ret;
}

/**
 * trusty_ffa_mem_share - FFA_MEM_SHARE implementation.
 * @client:             Client state.
 * @total_length:       Total length of shared memory descriptor.
 * @fragment_length:    Length of fragment of shared memory descriptor passed in
 *                      this call.
 * @address:            Not supported, must be 0.
 * @page_count:         Not supported, must be 0.
 *
 * Implements a subset of the FF-A FFA_MEM_SHARE call needed to share memory
 * from non-secure os to secure os (with no stream endpoints).
 *
 * Return: 0 on success, error code on failure.
 */
static long trusty_ffa_mem_share(guest_cpu_handle_t gcpu,
		struct trusty_shmem_client_state *client,
		uint32_t total_length,
		uint32_t fragment_length,
		uint64_t address,
		uint32_t page_count,
		void *smc_handle) {
	struct trusty_shmem_obj *obj;

	if (address || page_count) {
		print_panic("%s: custom memory region for message not supported\n",
				__func__);
		return -EINVAL;
	}

	if (client->receiver) {
		print_panic("%s: unsupported share direction\n", __func__);
		return -EINVAL;
	}

	if (fragment_length < sizeof(obj->desc)) {
		print_panic("%s: bad first fragment size %u < %zu\n",
				__func__, fragment_length, sizeof(obj->desc));
		return -EINVAL;
	}
	obj = trusty_shmem_obj_alloc(&trusty_shmem_obj_state, total_length);
	if (!obj) {
		return -ENOMEM;
	}

	return trusty_ffa_fill_desc(gcpu, client, obj, fragment_length, smc_handle);
}

/**
 * trusty_ffa_mem_frag_tx - FFA_MEM_FRAG_TX implementation.
 * @client:             Client state.
 * @handle_low:         Handle_low value returned from SMC_FC_FFA_MEM_FRAG_RX.
 * @handle_high:        Handle_high value returned from SMC_FC_FFA_MEM_FRAG_RX.
 * @fragment_length:    Length of fragments transmitted.
 * @sender_id:          Vmid of sender in bits [31:16]
 *
 * Return: SMC_FC_FFA_SUCCESS on success, error code on failure.
 */
static long trusty_ffa_mem_frag_tx(guest_cpu_handle_t gcpu,
		struct trusty_shmem_client_state *client,
		uint32_t handle_low,
		uint32_t handle_high,
		uint32_t fragment_length,
		uint32_t sender_id,
		void *smc_handle) {
	struct trusty_shmem_obj *obj;
	uint64_t handle = handle_low | (((uint64_t)handle_high) << 32);

	if (client->receiver) {
		print_panic("%s: unsupported share direction\n", __func__);
		return -EINVAL;
	}

	obj = trusty_shmem_obj_lookup(&trusty_shmem_obj_state, handle);
	if (!obj) {
		print_panic("%s: invalid handle, 0x%llx, not a valid handle\n",
				__func__, handle);
		return -ENOENT;
	}

	if (sender_id != (uint32_t)obj->desc.sender_id << 16) {
		print_panic("%s: invalid sender_id 0x%x != 0x%x\n", __func__,
				sender_id, (uint32_t)obj->desc.sender_id << 16);
		return -ENOENT;
	}

	return trusty_ffa_fill_desc(gcpu, client, obj, fragment_length, smc_handle);
}

/**
 * trusty_ffa_mem_retrieve_req - FFA_MEM_RETRIEVE_REQ implementation.
 * @client:             Client state.
 * @total_length:       Total length of retrieve request descriptor if this is
 *                      the first call. Otherwise (unsupported) must be 0.
 * @fragment_length:    Length of fragment of retrieve request descriptor passed
 *                      in this call. Only @fragment_length == @length is
 *                      supported by this implementation.
 * @address:            Not supported, must be 0.
 * @page_count:         Not supported, must be 0.
 *
 * Implements a subset of the FF-A FFA_MEM_RETRIEVE_REQ call.
 * Used by secure os to retrieve memory already shared by non-secure os.
 * If the data does not fit in a single SMC_FC_FFA_MEM_RETRIEVE_RESP message,
 * the client must call FFA_MEM_FRAG_RX until the full response has been
 * received.
 *
 * Return: SMC_FC_FFA_SUCCESS on success, error code on failure.
 */
static long
trusty_ffa_mem_retrieve_req(guest_cpu_handle_t gcpu,
		struct trusty_shmem_client_state *client,
		uint32_t total_length,
		uint32_t fragment_length,
		uint64_t address,
		uint32_t page_count,
		void *smc_handle) {
	struct trusty_shmem_obj *obj = NULL;
	const struct ffa_mtd *req = client->tx_buf;
	struct ffa_mtd *resp = client->rx_buf;
	uint64_t copy_size = 0;

	if (address || page_count) {
		print_panic("%s: custom memory region not supported\n", __func__);
		return -EINVAL;
	}

	if (fragment_length != total_length) {
		print_panic("%s: fragmented retrieve request not supported\n",
				__func__);
		return -EINVAL;
	}

	/* req->emad_count is not set for retrieve by hypervisor */
	if (client->receiver && req->emad_count != 1) {
		print_panic("%s: unsupported retrieve descriptor count: %u\n",
				__func__, req->emad_count);
		return -EINVAL;
	}

	if (total_length < sizeof(*req)) {
		print_panic("%s: invalid length %u < %zu\n", __func__, total_length,
				sizeof(*req));
		return -EINVAL;
	}

	obj = trusty_shmem_obj_lookup(&trusty_shmem_obj_state, req->handle);
	if (!obj) {
        print_panic("%s: invalid handle %lx\n", __func__, req->handle);
		return -ENOENT;
	}

	if (obj->desc_filled != obj->desc_size) {
		print_panic("%s: incomplete object desc filled %zu < size %zu\n",
				__func__, obj->desc_filled, obj->desc_size);
		return -EINVAL;
	}

	if (req->emad_count && req->sender_id != obj->desc.sender_id) {
		print_panic("%s: wrong sender id 0x%x != 0x%x\n",
				__func__, req->sender_id, obj->desc.sender_id);
		return -EINVAL;
	}

	if (req->emad_count && req->tag != obj->desc.tag) {
		print_panic("%s: wrong tag 0x%llx != 0x%llx\n",
				__func__, req->tag, obj->desc.tag);
		return -EINVAL;
	}

	if (req->flags != 0 && req->flags != FFA_MTD_FLAG_TYPE_SHARE_MEMORY) {
		/*
		 * Current implementation does not support lend or donate, and
		 * it supports no other flags.
		 */
		print_panic("%s: invalid flags 0x%x\n", __func__, req->flags);
		return -EINVAL;
	}

	/* TODO: support more than one endpoint ids */
	if (req->emad_count &&
			req->emad[0].mapd.endpoint_id !=
			obj->desc.emad[0].mapd.endpoint_id) {
		print_panic("%s: wrong receiver id 0x%x != 0x%x\n",
				__func__, req->emad[0].mapd.endpoint_id,
				obj->desc.emad[0].mapd.endpoint_id);
		return -EINVAL;
	}

	if (req->emad_count) {
		obj->in_use++;
	}

	copy_size = MIN(obj->desc_size, client->buf_size);

	memcpy(resp, &obj->desc, copy_size);

	SMC_RET8(gcpu, smc_handle, SMC_FC_FFA_MEM_RETRIEVE_RESP, obj->desc_size,
			copy_size, 0, 0, 0, 0, 0);
}

/**
 * trusty_ffa_mem_frag_rx - FFA_MEM_FRAG_RX implementation.
 * @client:             Client state.
 * @handle_low:         Handle passed to &FFA_MEM_RETRIEVE_REQ. Bit[31:0].
 * @handle_high:        Handle passed to &FFA_MEM_RETRIEVE_REQ. Bit[63:32].
 * @fragment_offset:    Byte offset in descriptor to resume at.
 * @sender_id:          Bit[31:16]: Endpoint id of sender if client is a
 *                      hypervisor. 0 otherwise.
 *
 * Return: SMC_FC_FFA_SUCCESS on success, error code on failure.
 */
static long trusty_ffa_mem_frag_rx(guest_cpu_handle_t gcpu,
		struct trusty_shmem_client_state *client,
		uint32_t handle_low,
		uint32_t handle_high,
		uint32_t fragment_offset,
		uint32_t sender_id,
		void *smc_handle) {
	struct trusty_shmem_obj *obj;
	uint64_t handle = handle_low | (((uint64_t)handle_high) << 32);
	uint64_t full_copy_size = 0;
	uint64_t copy_size = 0;
	void *src = NULL;

	if (client->secure && sender_id) {
		print_panic("%s: invalid sender_id 0x%x != 0\n",
				__func__, sender_id);
		return -EINVAL;
	}

	obj = trusty_shmem_obj_lookup(&trusty_shmem_obj_state, handle);
	if (!obj) {
		print_panic("%s: invalid handle, 0x%llx, not a valid handle\n",
				__func__, handle);
		return -ENOENT;
	}

	if (!client->secure && sender_id &&
			sender_id != (uint32_t)obj->desc.sender_id << 16) {
		print_panic("%s: invalid sender_id 0x%x != 0x%x\n", __func__,
				sender_id, (uint32_t)obj->desc.sender_id << 16);
		return -ENOENT;
	}

	if (fragment_offset >= obj->desc_size) {
		print_panic("%s: invalid fragment_offset 0x%x >= 0x%zx\n",
				__func__, fragment_offset, obj->desc_size);
		return -EINVAL;
	}

	full_copy_size = obj->desc_size - fragment_offset;
	copy_size = MIN(full_copy_size, client->buf_size);

	src = (void *)((uint64_t)&obj->desc + fragment_offset);

	memcpy(client->rx_buf, (void *)((uint64_t)src + fragment_offset), copy_size);

	SMC_RET8(gcpu, smc_handle, SMC_FC_FFA_MEM_FRAG_TX, handle_low, handle_high,
			copy_size, sender_id, 0, 0, 0);
}

/**
 * trusty_ffa_mem_relinquish - FFA_MEM_RELINQUISH implementation.
 * @client:             Client state.
 *
 * Implements a subset of the FF-A FFA_MEM_RELINQUISH call.
 * Used by secure os release previously shared memory to non-secure os.
 *
 * The handle to release must be in the client's (secure os's) transmit buffer.
 *
 * Return: 0 on success, error code on failure.
 */
static int
trusty_ffa_mem_relinquish(struct trusty_shmem_client_state *client) {
	struct trusty_shmem_obj *obj;
	const struct ffa_mem_relinquish_descriptor *req = client->tx_buf;

	if (!client->receiver) {
		print_panic("%s: unsupported share direction\n", __func__);
		return -EINVAL;
	}

	if (req->flags) {
		print_panic("%s: unsupported flags 0x%x\n", __func__, req->flags);
		return -EINVAL;
	}

	obj = trusty_shmem_obj_lookup(&trusty_shmem_obj_state, req->handle);
	if (!obj) {
		return -ENOENT;
	}

	if (obj->desc.emad_count != req->endpoint_count) {
		return -EINVAL;
	}
	for (uint64_t i = 0; i < req->endpoint_count; i++) {
		if (req->endpoint_array[i] !=
				obj->desc.emad[i].mapd.endpoint_id) {
			return -EINVAL;
		}
	}
	if (!obj->in_use) {
		return -EACCES;
	}
	obj->in_use--;
	return 0;
}

/**
 * trusty_ffa_mem_reclaim - FFA_MEM_RECLAIM implementation.
 * @client:         Client state.
 * @handle_low:     Unique handle of shared memory object to relaim. Bit[31:0].
 * @handle_high:    Unique handle of shared memory object to relaim. Bit[63:32].
 * @flags:          Unsupported, ignored.
 *
 * Implements a subset of the FF-A FFA_MEM_RECLAIM call.
 * Used by non-secure os reclaim memory previously shared with secure os.
 *
 * Return: 0 on success, error code on failure.
 */
static int trusty_ffa_mem_reclaim(struct trusty_shmem_client_state *client,
		uint32_t handle_low,
		uint32_t handle_high,
		uint32_t flags) {
	struct trusty_shmem_obj *obj;
	uint64_t handle = handle_low | (((uint64_t)handle_high) << 32);

	if (client->receiver) {
		print_panic("%s: unsupported share direction\n", __func__);
		return -EINVAL;
	}

	if (flags) {
		print_panic("%s: unsupported flags 0x%x\n", __func__, flags);
		return -EINVAL;
	}

	obj = trusty_shmem_obj_lookup(&trusty_shmem_obj_state, handle);
	if (!obj) {
		return -ENOENT;
	}
	if (obj->in_use) {
		return -EACCES;
	}
	trusty_shmem_obj_free(&trusty_shmem_obj_state, obj);
	return 0;
}

/**
 * trusty_ffa_rxtx_map - FFA_RXTX_MAP implementation.
 * @client:     Client state.
 * @tx_address: Address of client's transmit buffer.
 * @rx_address: Address of client's receive buffer.
 * @page_count: Number of (contiguous) 4K pages per buffer.
 *
 * Implements the FF-A FFA_RXTX_MAP call.
 * Used by non-secure os and secure os to register their RX/TX buffer pairs.
 *
 * Return: 0 on success, error code on failure.
 */
static long trusty_ffa_rxtx_map(struct trusty_shmem_client_state *client,
		uint64_t tx_address,
		uint64_t rx_address,
		uint64_t page_count) {
	uint64_t buf_size = page_count * FFA_PAGE_SIZE;

	if (!buf_size) {
		print_panic("%s: invalid page_count %ld\n", __func__, page_count);
		return -EINVAL;
	}

	if (client->buf_size) {
		print_panic("%s: buffer pair already uint64_ted\n", __func__);
		return -EACCES;
	}

	/*
	 * Following mechanism is different from orginal implementation.
	 * In FFA implementation, if client is not identity mapped, firmware
	 * needs to create mapping for PA: tx_address and rx_address.
	 * In switcher solution, we treat VA identical to PA, so with this
	 * assumption, both Trusty and Test-runner need to prepare this mapping.
	 *
	 */
	client->buf_size = buf_size;
	client->tx_buf = (const void *)tx_address;
	client->rx_buf = (void *)rx_address;

	return 0;
}

/**
 * trusty_ffa_rxtx_unmap - FFA_RXTX_UNMAP implementation.
 * @client:     Client state.
 * @id:         Unsupported, ignored.
 *
 * Implements the FF-A FFA_RXTX_UNMAP call.
 * Used by non-secure os and secure os to release their RX/TX buffer pairs.
 *
 * Return: 0 on success, error code on failure.
 */
static long trusty_ffa_rxtx_unmap(struct trusty_shmem_client_state *client,
		UNUSED uint64_t id) {
	if (!client->buf_size) {
		print_panic("%s: buffer pair not uint64_ted\n", __func__);
		return -EINVAL;
	}

	if (trusty_shmem_obj_state.allocated) {
		print_panic("%s: shared memory regions are still active\n", __func__);
	}

	client->buf_size = 0;
	client->tx_buf = NULL;
	client->rx_buf = NULL;
	return 0;
}

/**
 * trusty_ffa_id_get - FFA_ID_GET implementation.
 * @client:     Client state.
 * @idp:        Pointer to store id return value in.
 *
 * Return the ID of the caller. For the non-secure client, use ID 0 as required
 * by FF-A. For the secure side return 0x8000 as Hafnium expects the secure OS
 * to use that ID.
 *
 * Note that the sender_id check in trusty_ffa_mem_frag_tx and
 * trusty_ffa_mem_frag_rx only works when there is no hypervisor because we use
 * id 0. The spec says the sender_id field must be 0 in that case.
 *
 * Return: 0 on success, error code on failure.
 */
static int trusty_ffa_id_get(struct trusty_shmem_client_state *client,
		uint64_t *idp)
{
	*idp = client->secure ? 0x8000 : 0;
	return 0;
}

/**
 * trusty_ffa_version - FFA_VERSION implementation.
 * @client:     Client state.
 * @version_in: Version supported by client.
 *
 * Return: 0 on success, error code on failure.
 */
static int trusty_ffa_version(guest_cpu_handle_t gcpu,
		UNUSED struct trusty_shmem_client_state *client,
		uint64_t version_in,
		void *smc_handle) {
	if (version_in & (1U << 31)) {
		goto err_not_suppoprted;
	}

	/*
	 * We only implement one version. If the client specified a newer major
	 * version than ours, return the version we suppoort. Otherwise return
	 * not-supported.
	 */
	if (FFA_VERSION_TO_MAJOR(version_in) >= FFA_CURRENT_VERSION_MAJOR) {
		SMC_RET8(gcpu, smc_handle, FFA_CURRENT_VERSION, 0, 0, 0, 0, 0, 0, 0);
	}

err_not_suppoprted:
	SMC_RET1(gcpu, smc_handle, (uint32_t)FFA_ERROR_NOT_SUPPORTED);
}

/**
 * trusty_ffa_features - FFA_FEATURES implementation.
 * @client:     Client state.
 * @func:       Api to check.
 * @ret2:       Pointer to return value2 on success.
 * @ret3:       Pointer to return value3 on success.
 *
 * Return: 0 on success, error code on failure.
 */
static int trusty_ffa_features(UNUSED struct trusty_shmem_client_state *client,
		uint64_t func,
		uint64_t *ret2,
		uint64_t *ret3) {
	if (SMC_ENTITY(func) != SMC_ENTITY_SHARED_MEMORY ||
			!SMC_IS_FASTCALL(func)) {
		return -EINVAL;
	}
	switch (func) {
		case SMC_FC_FFA_ERROR:
		case SMC_FC_FFA_SUCCESS:
		case SMC_FC_FFA_VERSION:
		case SMC_FC_FFA_FEATURES:
		case SMC_FC_FFA_RXTX_UNMAP:
		case SMC_FC_FFA_ID_GET:
		case SMC_FC_FFA_MEM_RETRIEVE_RESP:
		case SMC_FC_FFA_MEM_FRAG_RX:
		case SMC_FC_FFA_MEM_FRAG_TX:
			return 0;

		case SMC_FC_FFA_RXTX_MAP:
		case SMC_FC64_FFA_RXTX_MAP:
			*ret2 = FFA_FEATURES2_RXTX_MAP_BUF_SIZE_4K;
			return 0;

		case SMC_FC_FFA_MEM_RETRIEVE_REQ:
		case SMC_FC64_FFA_MEM_RETRIEVE_REQ:
			/*
			 * Indicate that object can be retrieved up to 2^64 - 1 times
			 * (on a 64 bit build). We track the number of times an object
			 * had been retrieved in a variable of type uint64_t.
			 */
			*ret3 = sizeof(uint64_t) * 8 - 1;
			__attribute__((fallthrough));

		case SMC_FC_FFA_MEM_SHARE:
		case SMC_FC64_FFA_MEM_SHARE:
		case SMC_FC_FFA_MEM_RELINQUISH:
		case SMC_FC_FFA_MEM_RECLAIM:
			*ret2 = 0;
			return 0;

		default:
			return -ENOTSUP;
	}
}

/**
 * to_spi_err - Convert from local error code to FF-A error code.
 * @ret:    Local error code.
 *
 * Return: FF-A defined error code.
 */
static int to_spi_err(int ret) {
	switch(ret) {
		case -ENOMEM:
			return FFA_ERROR_NO_MEMORY;
		case -EINVAL:
		case -ENOENT:
			return FFA_ERROR_INVALID_PARAMETERS;
		case -EACCES:
			return FFA_ERROR_DENIED;
		case -ENOTSUP:
			return FFA_ERROR_NOT_SUPPORTED;
		default:
			return FFA_ERROR_INVALID_PARAMETERS;
	}
}

/*
 * trusty_shared_memory_smc - SMC call handler.
 *
 * Original spm_mm_smc_handler desgined by Google needs 8 parameters,
 * it follows arm-trusted-firmware runtime service design.
 *  uint64_t spm_mm_smc_handler(uint32_t smc_fid, uint64_t x1, uint64_t x2,
 *                              uint64_t x3, uint64_t x4, void *cookie,
 *                              void *handle, uint64_t flags)
 *
 * According to arm-trusted-firmware implementation, parameter 'handle'
 * points to 'smc_ctx_t', cookie is always 0, 'flags' is just used to
 * indicate whether this SMC is invoked from secure world.
 *
 * In current solution, there is no prelude of smc handler,
 * so parameters 'handle', 'cookie', 'flags' are ignore, and we will
 * return smc handling status directly.
 */
int spm_mm_smc_handler(guest_cpu_handle_t gcpu) {
	uint32_t smc_fid = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RDI);
	uint64_t x1 = gcpu_get_gp_reg(gcpu, REG_RSI);
	uint64_t x2 = gcpu_get_gp_reg(gcpu, REG_RDX);
	uint64_t x3 = gcpu_get_gp_reg(gcpu, REG_RCX);
	uint64_t x4 = gcpu_get_gp_reg(gcpu, REG_R8);
	uint64_t ret_reg2 = 0;
	uint64_t ret_reg3 = 0;
	long ret = -1;
	void *handle = &world_handle[is_caller_secure(gcpu)];
	struct trusty_shmem_client_state *client =
		&trusty_shmem_client_state[is_caller_secure(gcpu)];

	if (((smc_fid < SMC_FC32_FFA_MIN) || (smc_fid > SMC_FC32_FFA_MAX)) &&
		((smc_fid < SMC_FC64_FFA_MIN) || (smc_fid > SMC_FC64_FFA_MAX))) {
		print_panic("%s(0x%x) unknown smc\n", __func__, smc_fid);
		SMC_RET1(gcpu, SMC_FC_FFA_ERROR, SMC_UNK);
	}

	//spin_lock(&trusty_shmem_obj_state.lock);

	switch (smc_fid) {
		case SMC_FC_FFA_VERSION:
			ret = trusty_ffa_version(gcpu, client, x1, handle);
			break;

		case SMC_FC_FFA_FEATURES:
			ret = trusty_ffa_features(client, x1, &ret_reg2, &ret_reg3);
			break;

		case SMC_FC_FFA_RXTX_MAP:
		case SMC_FC64_FFA_RXTX_MAP:
			ret = trusty_ffa_rxtx_map(client, x1, x2, x3);
			break;

		case SMC_FC_FFA_RXTX_UNMAP:
			ret = trusty_ffa_rxtx_unmap(client, x1);
			break;

		case SMC_FC_FFA_ID_GET:
			ret = trusty_ffa_id_get(client, &ret_reg2);
			break;

		case SMC_FC_FFA_MEM_SHARE:
		case SMC_FC64_FFA_MEM_SHARE:
			ret = trusty_ffa_mem_share(gcpu, client, x1, x2, x3, x4, handle);
			break;

		case SMC_FC_FFA_MEM_RETRIEVE_REQ:
		case SMC_FC64_FFA_MEM_RETRIEVE_REQ:
			ret = trusty_ffa_mem_retrieve_req(gcpu, client, x1, x2, x3, x4,
					handle);
			break;

		case SMC_FC_FFA_MEM_RELINQUISH:
			ret = trusty_ffa_mem_relinquish(client);
			break;

		case SMC_FC_FFA_MEM_RECLAIM:
			ret = trusty_ffa_mem_reclaim(client, x1, x2, x3);
			break;

		case SMC_FC_FFA_MEM_FRAG_RX:
			ret = trusty_ffa_mem_frag_rx(gcpu, client, x1, x2, x3, x4, handle);
			break;

		case SMC_FC_FFA_MEM_FRAG_TX:
			ret = trusty_ffa_mem_frag_tx(gcpu, client, x1, x2, x3, x4, handle);
			break;

		default:
			print_panic("%s(0x%x, 0x%lx) unsupported ffa smc\n",
					__func__, smc_fid, x1);
			ret = -ENOTSUP;
			break;
	}
	//spin_unlock(&trusty_shmem_obj_state.lock);

	if (ret) {
		if (ret == (long)handle) {
			return ret;
		}
		print_panic("%s(0x%x) failed, handler:%p\n", __func__, smc_fid, handle);
		SMC_RET8(gcpu, handle, SMC_FC_FFA_ERROR, 0, to_spi_err(ret), 0, 0, 0,
				0, 0);
	} else {
		SMC_RET8(gcpu, handle, SMC_FC_FFA_SUCCESS, 0, ret_reg2, ret_reg3, 0,
				0, 0, 0);
	}
}
