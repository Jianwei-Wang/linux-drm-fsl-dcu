/*
 * Copyright 2013 Red Hat
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef VIRGL_DRM_H
#define VIRGL_DRM_H

#include <stddef.h>
#include "drm/drm.h"

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use uint64_t instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define DRM_VIRGL_ALLOC       0x00
#define DRM_VIRGL_MAP         0x01
#define DRM_VIRGL_EXECBUFFER  0x02
#define DRM_VIRGL_GETPARAM    0x03
#define DRM_VIRGL_RESOURCE_CREATE 0x04
#define DRM_VIRGL_RESOURCE_INFO     0x05
#define DRM_VIRGL_TRANSFER_GET 0x06
#define DRM_VIRGL_TRANSFER_PUT 0x07
#define DRM_VIRGL_WAIT     0x08

struct drm_virgl_alloc {
	uint32_t size;
	uint32_t handle; /* 0 is an invalid handle */
};

struct drm_virgl_map {
	uint64_t offset; /* use for mmap system call */
	uint32_t handle;
	uint32_t pad;
};

struct drm_virgl_execbuffer {
	uint32_t		flags;		/* for future use */
	uint32_t size;
	uint64_t	 __user command; /* void* */
};

/* no params yet */
struct drm_virgl_getparam {
	uint64_t param;
	uint64_t value;
};

/* NO_BO flags? NO resource flag? */
struct drm_virgl_resource_create {
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t res_handle;  /* returned by kernel */
	uint32_t size;
	uint32_t flags;
	uint32_t bo_handle; /* if this is set - recreate a new resource attached to this bo ? */
};

struct drm_virgl_resource_info {
	uint32_t bo_handle;
	uint32_t res_handle;
	uint32_t size;
};

struct drm_virgl_3d_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

struct drm_virgl_3d_transfer_put {
	uint32_t bo_handle;/* set to 0 to use user_ptr */
	struct drm_virgl_3d_box dst_box;
	uint32_t dst_level;
	uint32_t src_stride;
	uint32_t src_offset;
};

struct drm_virgl_3d_transfer_get {
	uint32_t bo_handle;/* set to 0 to use user_ptr */
	struct drm_virgl_3d_box box;
	uint32_t level;
	uint32_t dst_offset;
};

#define VIRGL_WAIT_NOWAIT 1 /* like it */
struct drm_virgl_3d_wait {
	uint32_t handle; /* 0 is an invalid handle */
        uint32_t flags;
};

#define DRM_IOCTL_VIRGL_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_ALLOC, struct drm_virgl_alloc)

#define DRM_IOCTL_VIRGL_MAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_MAP, struct drm_virgl_map)

#define DRM_IOCTL_VIRGL_EXECBUFFER \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIRGL_EXECBUFFER,\
		struct drm_virgl_execbuffer)

#define DRM_IOCTL_VIRGL_GETPARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_GETPARAM,\
		struct drm_virgl_getparam)

#define DRM_IOCTL_VIRGL_RESOURCE_CREATE			\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_RESOURCE_CREATE,	\
		struct drm_virgl_resource_create)

#define DRM_IOCTL_VIRGL_RESOURCE_INFO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_RESOURCE_INFO, \
		 struct drm_virgl_resource_info)

#define DRM_IOCTL_VIRGL_TRANSFER_GET \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_TRANSFER_GET,	\
		struct drm_virgl_3d_transfer_get)

#define DRM_IOCTL_VIRGL_TRANSFER_PUT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_TRANSFER_PUT,	\
		struct drm_virgl_3d_transfer_put)

#define DRM_IOCTL_VIRGL_WAIT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRGL_WAIT,	\
		struct drm_virgl_3d_wait)

#endif
