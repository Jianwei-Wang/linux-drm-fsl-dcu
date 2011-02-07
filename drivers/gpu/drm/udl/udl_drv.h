#include <linux/usb.h>

#define DRIVER_NAME		"udl"
#define DRIVER_DESC		"DisplayLink"
#define DRIVER_DATE		"20101215"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

struct udl_device;

struct urb_node {
	struct list_head entry;
	struct udl_device *dev;
	struct delayed_work release_urb_work;
	struct urb *urb;
};


struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct udl_fbdev;

struct udl_device {
	struct device *dev;
	struct drm_device *ddev;
	struct usb_device *udev;

	int sku_pixel_limit;

	struct urb_list urbs;
	atomic_t lost_pixels; /* 1 = a render op failed. Need screen refresh */
	bool virtualized;

	struct udl_fbdev *fbdev;
	char mode_buf[1024];
	uint32_t mode_buf_len;
	void *backing_buffer;
	atomic_t bytes_rendered; /* raw pixel-bytes driver asked to render */
	atomic_t bytes_identical; /* saved effort with backbuffer comparison */
	atomic_t bytes_sent; /* to usb, after compression including overhead */
	atomic_t cpu_kcycles_used; /* transpired during pixel processing */
};


/* modeset */
int udl_modeset_init(struct drm_device *dev);
void udl_modeset_cleanup(struct drm_device *dev);
	
int udl_connector_init(struct drm_device *dev, struct drm_encoder *encoder);

struct drm_encoder *udl_encoder_init(struct drm_device *dev);
  
struct urb *udl_get_urb(struct drm_device *dev);

int udl_submit_urb(struct drm_device *dev, struct urb *urb, size_t len);
void udl_urb_completion(struct urb *urb);

int udl_driver_load(struct drm_device *dev, unsigned long flags);
int udl_driver_unload(struct drm_device *dev);

int udl_fbdev_init(struct drm_device *dev);
void udl_fbdev_cleanup(struct drm_device *dev);

int udl_render_hline(struct drm_device *dev, struct urb **urb_ptr,
		     const char *front, char **urb_buf_ptr,
		     u32 byte_offset, u32 byte_width,
		     int *ident_ptr, int *sent_ptr);
