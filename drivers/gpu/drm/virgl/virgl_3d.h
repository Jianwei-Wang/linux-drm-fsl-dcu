#ifndef VIRGL_3D_H
#define VIRGL_3D_H

#include <linux/virtio.h>
#include <linux/virtio_ring.h>

typedef uint64_t VIRGLPHYSICAL;
/* specification for the 3D command processor */

enum virgl_3d_cmd_type {
	VIRGL_CMD_NOP,
	VIRGL_CMD_CREATE_CONTEXT,
	VIRGL_CMD_CREATE_RESOURCE,
	VIRGL_CMD_SUBMIT,
	VIRGL_DESTROY_CONTEXT,
	VIRGL_TRANSFER_GET,
	VIRGL_TRANSFER_PUT,
	VIRGL_SET_SCANOUT,
	VIRGL_FLUSH_BUFFER,
	VIRGL_RESOURCE_UNREF,
};

/* put a box of data from a BO into a tex/buffer resource */
struct virgl_3d_transfer_put {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct drm_virgl_3d_box dst_box; /* dst box */
	uint32_t dst_level;
	uint32_t src_stride;
};

struct virgl_3d_transfer_get {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct drm_virgl_3d_box box;
	int level;
	uint32_t dx, dy;
};

struct virgl_3d_flush_buffer {
	uint32_t res_handle;
	struct drm_virgl_3d_box box;
};

struct virgl_3d_set_scanout {
	uint32_t res_handle;
	struct drm_virgl_3d_box box;
};

struct virgl_3d_resource_create {
	uint32_t handle;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t pad;
};

struct virgl_3d_resource_unref {
	uint32_t res_handle;
};

struct virgl_3d_ring_header {
	uint32_t num_items;
	uint32_t prod;
	uint32_t notify_on_prod;
	uint32_t cons;
	uint32_t notify_on_cons;
};

struct virgl_3d_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
};

#define VIRGL_COMMAND_EMIT_FENCE (1 << 0)

struct virgl_3d_command {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	union virgl_3d_cmds {
		struct virgl_3d_resource_create res_create;
		struct virgl_3d_transfer_put transfer_put;
		struct virgl_3d_transfer_get transfer_get;
		struct virgl_3d_cmd_submit cmd_submit;
		struct virgl_3d_set_scanout set_scanout;
		struct virgl_3d_flush_buffer flush_buffer;
		struct virgl_3d_resource_unref res_unref;
	} u;
};

struct virgl_3d_fence_driver {
	atomic64_t last_seq;
	uint64_t last_activity;
	bool initialized;
	uint64_t			sync_seq;
};

struct virgl_3d_info {
	struct virgl_3d_fence_driver fence_drv;
	wait_queue_head_t		fence_queue;

	struct idr	resource_idr;
	spinlock_t resource_idr_lock;

	/* virt io info */
	void __iomem *ioaddr; /* bar 3 */
	struct virtqueue *cmdq;
	int cmd_num;
	void *cmdqueue;
	spinlock_t cmdq_lock;
	wait_queue_head_t cmd_ack_queue;
	struct work_struct dequeue_work;
};


struct virgl_3d_fence {
	struct virgl_device *qdev;
	struct kref kref;
	uint64_t seq;
};

struct virgl_3d_vbuffer {
	char *buf;
	size_t size;

	size_t bo_max_len;
	size_t bo_start_offset;
	size_t bo_user_offset;
	struct virgl_bo *bo;

	bool inout;
	int sgpages;
	int firstsg;
	struct scatterlist sg[0];
};

struct virgl_3d_fence *virgl_3d_fence_ref(struct virgl_3d_fence *fence);
void virgl_3d_fence_unref(struct virgl_3d_fence **fence);
 
bool virgl_3d_fence_signaled(struct virgl_3d_fence *fence);
int virgl_3d_fence_wait(struct virgl_3d_fence *fence, bool interruptible);
void virgl_3d_fence_process(struct virgl_device *qdev);

#define VIRGL_FENCE_SIGNALED_SEQ 0LL
#define VIRGL_FENCE_JIFFIES_TIMEOUT		(HZ / 2)


#endif
