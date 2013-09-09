#ifndef VIRTGPU_HW_H
#define VIRTGPU_HW_H

enum virtgpu_ctrl_cmd {
	VIRTGPU_CMD_NOP,
	VIRTGPU_CMD_ATTACH_STATUS_PAGE,
	VIRTGPU_CMD_GET_DISPLAY_INFO,
	VIRTGPU_CMD_GET_CAPS,
	VIRTGPU_CMD_RESOURCE_CREATE_2D,
	VIRTGPU_CMD_RESOURCE_UNREF,
	VIRTGPU_CMD_SET_SCANOUT,
	VIRTGPU_CMD_RESOURCE_FLUSH,
	VIRTGPU_CMD_TRANSFER_SEND_2D,
	VIRTGPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTGPU_CMD_RESOURCE_INVAL_BACKING,
};

struct virtgpu_hw_status_page {
	uint64_t fence_id;
	uint32_t cursor_x, cursor_y;
	uint32_t cursor_hot_x, cursor_hot_y;
	uint32_t cursor_id;
	uint32_t error_state;
};

struct virtgpu_attach_status_page {
	uint64_t page_address;
};

struct virtgpu_resource_unref {
	uint32_t resource_id;
};

/* create a simple 2d resource with a format */
struct virtgpu_resource_create_2d {
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

struct virtgpu_set_scanout {
	uint32_t scanout_id;
	uint32_t resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
};

/* simple transfer send */
struct virtgpu_transfer_send_2d {
	uint32_t resource_id;
	uint32_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
};

struct virtgpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t pad;
};

struct virtgpu_resource_attach_backing {
	uint32_t resource_id;
	uint32_t nr_entries;
};

struct virtgpu_resource_inval_backing {
	uint32_t resource_id;
};

#define VIRTGPU_MAX_SCANOUTS 16
struct virtgpu_display_info {
	uint32_t num_scanouts;
	struct {
		uint32_t enabled;
		uint32_t width;
		uint32_t height;
		uint32_t x;
		uint32_t y;
		uint32_t flags;
	} pmodes[VIRTGPU_MAX_SCANOUTS];
};

#define VIRTGPU_COMMAND_EMIT_FENCE (1 << 0)

struct virtgpu_command {
	uint32_t type;
	uint32_t flags;
	uint64_t fence;
	union virtgpu_cmds {
		struct virtgpu_attach_status_page attach_status_page;
		struct virtgpu_resource_create_2d resource_create_2d;
		struct virtgpu_resource_unref resource_unref;
		struct virtgpu_set_scanout set_scanout;
		struct virtgpu_transfer_send_2d transfer_send_2d;
		struct virtgpu_resource_attach_backing resource_attach_backing;
		struct virtgpu_resource_inval_backing resource_inval_backing;
	} u;
};

/* simple formats for fbcon/X use */
enum virtgpu_formats {
   VIRGL_FORMAT_B8G8R8A8_UNORM          = 1,
   VIRGL_FORMAT_B8G8R8X8_UNORM          = 2,
   VIRGL_FORMAT_A8R8G8B8_UNORM          = 3,
   VIRGL_FORMAT_X8R8G8B8_UNORM          = 4,

   VIRGL_FORMAT_B5G5R5A1_UNORM          = 5,

   VIRGL_FORMAT_R8_UNORM                = 64,
};

#endif
