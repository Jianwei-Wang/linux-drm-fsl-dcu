/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <christian.koenig@amd.com>
 */

#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

struct amdgpu_sync_entry {
	struct hlist_node	node;
	struct fence		*fence;
};

/**
 * amdgpu_sync_create - zero init sync object
 *
 * @sync: sync object to initialize
 *
 * Just clear the sync object for now.
 */
void amdgpu_sync_create(struct amdgpu_sync *sync)
{
	unsigned i;

	for (i = 0; i < AMDGPU_NUM_SYNCS; ++i)
		sync->semaphores[i] = NULL;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		sync->sync_to[i] = NULL;

	hash_init(sync->fences);
	sync->last_vm_update = NULL;
}

static bool amdgpu_sync_same_dev(struct amdgpu_device *adev, struct fence *f)
{
	struct amdgpu_fence *a_fence = to_amdgpu_fence(f);
	struct amd_sched_fence *s_fence = to_amd_sched_fence(f);

	if (a_fence)
		return a_fence->ring->adev == adev;
	if (s_fence)
		return (struct amdgpu_device *)s_fence->scheduler->priv == adev;
	return false;
}

static bool amdgpu_sync_test_owner(struct fence *f, void *owner)
{
	struct amdgpu_fence *a_fence = to_amdgpu_fence(f);
	struct amd_sched_fence *s_fence = to_amd_sched_fence(f);
	if (s_fence)
		return s_fence->owner == owner;
	if (a_fence)
		return a_fence->owner == owner;
	return false;
}

/**
 * amdgpu_sync_fence - remember to sync to this fence
 *
 * @sync: sync object to add fence to
 * @fence: fence to sync to
 *
 */
int amdgpu_sync_fence(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		      struct fence *f)
{
	struct amdgpu_sync_entry *e;
	struct amdgpu_fence *fence;
	struct amdgpu_fence *other;
	struct fence *tmp, *later;

	if (!f)
		return 0;

	if (amdgpu_sync_same_dev(adev, f) &&
	    amdgpu_sync_test_owner(f, AMDGPU_FENCE_OWNER_VM)) {
		if (sync->last_vm_update) {
			tmp = sync->last_vm_update;
			BUG_ON(f->context != tmp->context);
			later = (f->seqno - tmp->seqno <= INT_MAX) ? f : tmp;
			sync->last_vm_update = fence_get(later);
			fence_put(tmp);
		} else
			sync->last_vm_update = fence_get(f);
	}

	fence = to_amdgpu_fence(f);
	if (!fence || fence->ring->adev != adev) {
		hash_for_each_possible(sync->fences, e, node, f->context) {
			struct fence *new;
			if (unlikely(e->fence->context != f->context))
				continue;
			new = fence_get(fence_later(e->fence, f));
			if (new) {
				fence_put(e->fence);
				e->fence = new;
			}
			return 0;
		}

		e = kmalloc(sizeof(struct amdgpu_sync_entry), GFP_KERNEL);
		if (!e)
			return -ENOMEM;

		hash_add(sync->fences, &e->node, f->context);
		e->fence = fence_get(f);
		return 0;
	}

	other = sync->sync_to[fence->ring->idx];
	sync->sync_to[fence->ring->idx] = amdgpu_fence_ref(
		amdgpu_fence_later(fence, other));
	amdgpu_fence_unref(&other);

	return 0;
}

/**
 * amdgpu_sync_resv - use the semaphores to sync to a reservation object
 *
 * @sync: sync object to add fences from reservation object to
 * @resv: reservation object with embedded fence
 * @shared: true if we should only sync to the exclusive fence
 *
 * Sync to the fence using the semaphore objects
 */
int amdgpu_sync_resv(struct amdgpu_device *adev,
		     struct amdgpu_sync *sync,
		     struct reservation_object *resv,
		     void *owner)
{
	struct reservation_object_list *flist;
	struct fence *f;
	struct amdgpu_fence *fence;
	unsigned i;
	int r = 0;

	if (resv == NULL)
		return -EINVAL;

	/* always sync to the exclusive fence */
	f = reservation_object_get_excl(resv);
	r = amdgpu_sync_fence(adev, sync, f);

	flist = reservation_object_get_list(resv);
	if (!flist || r)
		return r;

	for (i = 0; i < flist->shared_count; ++i) {
		f = rcu_dereference_protected(flist->shared[i],
					      reservation_object_held(resv));
		fence = f ? to_amdgpu_fence(f) : NULL;
		if (fence && fence->ring->adev == adev) {
			/* VM updates are only interesting
			 * for other VM updates and moves.
			 */
			if ((owner != AMDGPU_FENCE_OWNER_MOVE) &&
			    (fence->owner != AMDGPU_FENCE_OWNER_MOVE) &&
			    ((owner == AMDGPU_FENCE_OWNER_VM) !=
			     (fence->owner == AMDGPU_FENCE_OWNER_VM)))
				continue;

			/* Ignore fence from the same owner as
			 * long as it isn't undefined.
			 */
			if (owner != AMDGPU_FENCE_OWNER_UNDEFINED &&
			    fence->owner == owner)
				continue;
		}

		r = amdgpu_sync_fence(adev, sync, f);
		if (r)
			break;
	}
	return r;
}

int amdgpu_sync_wait(struct amdgpu_sync *sync)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	int i, r;

	hash_for_each_safe(sync->fences, i, tmp, e, node) {
		r = fence_wait(e->fence, false);
		if (r)
			return r;

		hash_del(&e->node);
		fence_put(e->fence);
		kfree(e);
	}
	return 0;
}

/**
 * amdgpu_sync_rings - sync ring to all registered fences
 *
 * @sync: sync object to use
 * @ring: ring that needs sync
 *
 * Ensure that all registered fences are signaled before letting
 * the ring continue. The caller must hold the ring lock.
 */
int amdgpu_sync_rings(struct amdgpu_sync *sync,
		      struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned count = 0;
	int i, r;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_fence *fence = sync->sync_to[i];
		struct amdgpu_semaphore *semaphore;
		struct amdgpu_ring *other = adev->rings[i];

		/* check if we really need to sync */
		if (!amdgpu_fence_need_sync(fence, ring))
			continue;

		/* prevent GPU deadlocks */
		if (!other->ready) {
			dev_err(adev->dev, "Syncing to a disabled ring!");
			return -EINVAL;
		}

		if (amdgpu_enable_scheduler || (count >= AMDGPU_NUM_SYNCS)) {
			/* not enough room, wait manually */
			r = fence_wait(&fence->base, false);
			if (r)
				return r;
			continue;
		}
		r = amdgpu_semaphore_create(adev, &semaphore);
		if (r)
			return r;

		sync->semaphores[count++] = semaphore;

		/* allocate enough space for sync command */
		r = amdgpu_ring_alloc(other, 16);
		if (r)
			return r;

		/* emit the signal semaphore */
		if (!amdgpu_semaphore_emit_signal(other, semaphore)) {
			/* signaling wasn't successful wait manually */
			amdgpu_ring_undo(other);
			r = fence_wait(&fence->base, false);
			if (r)
				return r;
			continue;
		}

		/* we assume caller has already allocated space on waiters ring */
		if (!amdgpu_semaphore_emit_wait(ring, semaphore)) {
			/* waiting wasn't successful wait manually */
			amdgpu_ring_undo(other);
			r = fence_wait(&fence->base, false);
			if (r)
				return r;
			continue;
		}

		amdgpu_ring_commit(other);
		amdgpu_fence_note_sync(fence, ring);
	}

	return 0;
}

/**
 * amdgpu_sync_free - free the sync object
 *
 * @adev: amdgpu_device pointer
 * @sync: sync object to use
 * @fence: fence to use for the free
 *
 * Free the sync object by freeing all semaphores in it.
 */
void amdgpu_sync_free(struct amdgpu_device *adev,
		      struct amdgpu_sync *sync,
		      struct fence *fence)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	unsigned i;

	hash_for_each_safe(sync->fences, i, tmp, e, node) {
		hash_del(&e->node);
		fence_put(e->fence);
		kfree(e);
	}

	for (i = 0; i < AMDGPU_NUM_SYNCS; ++i)
		amdgpu_semaphore_free(adev, &sync->semaphores[i], fence);

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		amdgpu_fence_unref(&sync->sync_to[i]);

	fence_put(sync->last_vm_update);
}
