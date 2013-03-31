#ifndef QXL_3D_H
#define QXL_3D_H

enum qxl_3d_cmd_type {
	QXL_3D_CMD_NOP,
	QXL_3D_CMD_CREATE_CONTEXT,
	QXL_3D_CMD_CREATE_RESOURCE,
	QXL_3D_CMD_SUBMIT,
	QXL_3D_DESTROY_CONTEXT,
	QXL_3D_TRANSFER_GET,
	QXL_3D_TRANSFER_PUT,
	QXL_3D_FENCE,
};

struct qxl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};
struct qxl_3d_transfer_put {
	uint32_t res_handle;
	struct qxl_box box;
	struct qxl_box transfer_box;
	uint32_t level;
};

struct qxl_3d_transfer_get {
	uint32_t res_handle;
	struct qxl_box box;
};

struct qxl_3d_flush_buffer {
	uint32_t res_handle;
};

struct qxl_3d_resource_create {
	uint32_t handle;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
};

struct qxl_3d_ring_header {
	uint32_t num_items;
	uint32_t prod;
	uint32_t notify_on_prod;
	uint32_t cons;
	uint32_t notify_on_cons;
};

struct qxl_3d_command {
	QXLPHYSICAL data;
	uint32_t type;
	uint32_t padding;
};

struct qxl_3d_ram {
	uint32_t version;
	uint32_t pad;
	uint64_t last_fence;
	struct qxl_ring_header  cmd_ring_hdr;
	struct qxl_command	cmd_ring[QXL_COMMAND_RING_SIZE];
};

struct qxl_3d_info {
	struct qxl_ring *iv3d_ring;
	void *iv3d;
	struct qxl_3d_ram *ram_3d_header;  
  
	struct qxl_bo *ringbo;
};

struct qxl_3d_fence_driver {
	atomic64_t last_seq;
	uint64_t last_activity;
	bool initialized;
};

struct qxl_3d_fence {
	uint32_t type;
	struct qxl_device *qdev;
	struct kref kref;
	uint64_t seq;
};

struct qxl_3d_fence *qxl_3d_fence_ref(struct qxl_3d_fence *fence);
void qxl_3d_fence_unref(struct qxl_3d_fence **fence);
 
bool qxl_3d_fence_signaled(struct qxl_3d_fence *fence);
int qxl_3d_fence_wait(struct qxl_3d_fence *fence, bool interruptible);
#endif
