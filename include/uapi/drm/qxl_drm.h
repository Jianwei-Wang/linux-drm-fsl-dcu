#ifndef QXL_DRM_H
#define QXL_DRM_H

#include <stddef.h>
#include "drm/drm.h"

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use uint64_t instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define QXL_GEM_DOMAIN_CPU 0
#define QXL_GEM_DOMAIN_VRAM 1
#define QXL_GEM_DOMAIN_SURFACE 2

#define DRM_QXL_ALLOC       0x00
#define DRM_QXL_INCREF      0x01
#define DRM_QXL_DECREF      0x02
#define DRM_QXL_MAP         0x03
#define DRM_QXL_EXECBUFFER  0x04
#define DRM_QXL_UPDATE_AREA 0x05

enum {
	QXL_ALLOC_TYPE_DATA,
	QXL_ALLOC_TYPE_SURFACE,
	QXL_ALLOC_TYPE_SURFACE_PRIMARY,
};

struct drm_qxl_alloc {
	uint32_t type;
	uint32_t size;
	uint32_t handle; /* 0 is an invalid handle */
};

struct drm_qxl_incref {
	uint32_t handle;
};

struct drm_qxl_decref {
	uint32_t handle;
};

struct drm_qxl_map {
	uint64_t offset; /* use for mmap system call */
	uint32_t handle;
};

/*
 * *(src_handle.base_addr + src_offset) = physical_address(dst_handle.addr +
 * dst_offset)
 */
struct drm_qxl_reloc {
	uint64_t src_offset; /* offset into src_handle or src buffer */
	uint64_t dst_offset; /* offset in dest handle */
	uint32_t src_handle; /* 0 if to command buffer */
	uint32_t dst_handle; /* dest handle to compute address from */
};

struct drm_qxl_command {
	uint32_t		type;
	uint32_t		command_size;
	uint64_t	 __user command; /* void* */
	uint32_t		relocs_num;
	uint64_t	 __user relocs; /* struct drm_qxl_reloc* */
};

/* XXX: call it drm_qxl_commands? */
struct drm_qxl_execbuffer {
	uint32_t		flags;		/* for future use */
	uint32_t		commands_num;
	uint64_t	 __user commands;	/* struct drm_qxl_command* */
	uint8_t			spare[16];	/* spare for future */
};

struct drm_qxl_update_area {
	uint32_t surface_id;
	uint32_t top;
	uint32_t left;
	uint32_t bottom;
	uint32_t right;
};

#define DRM_IOCTL_QXL_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_ALLOC, struct drm_qxl_alloc)

#define DRM_IOCTL_QXL_INCREF \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_INCREF, struct drm_qxl_incref)

#define DRM_IOCTL_QXL_DECREF \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_DECREF, struct drm_qxl_decref)

#define DRM_IOCTL_QXL_MAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_MAP, struct drm_qxl_map)

#define DRM_IOCTL_QXL_EXECBUFFER \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_EXECBUFFER,\
		struct drm_qxl_execbuffer)

#define DRM_IOCTL_QXL_UPDATE_AREA \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_UPDATE_AREA,\
		struct drm_qxl_update_area)

#endif
