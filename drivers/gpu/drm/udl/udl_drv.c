#include "drm_usb.h"
#include "udl_drv.h"

static struct drm_driver driver;

static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9, .match_flags = USB_DEVICE_ID_MATCH_VENDOR,},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

MODULE_LICENSE("GPL");

static int udl_usb_probe(struct usb_interface *interface,
			 const struct usb_device_id *id)
{
	return drm_get_usb_dev(interface, id, &driver);
}

static void udl_usb_disconnect(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);
	drm_put_dev(dev);
}

static struct vm_operations_struct udl_gem_vm_ops = {
	.fault = udl_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM,
	.load = udl_driver_load,
	.unload = udl_driver_unload,

	/* gem hooks */
	.gem_init_object = udl_gem_init_object,
	.gem_free_object = udl_gem_free_object,
	.gem_vm_ops = &udl_gem_vm_ops,

	.dumb_create = udl_dumb_create,
	.dumb_map_offset = udl_gem_mmap,
	.dumb_destroy = udl_dumb_destroy,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.unlocked_ioctl = drm_ioctl,
		.poll = drm_poll,
		.fasync = drm_fasync,
		.read = drm_read,
		.llseek = noop_llseek,
		.mmap = drm_gem_mmap,
	},
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
      
static struct usb_driver udl_driver = {
  .name = "udl",
  .probe = udl_usb_probe,
  .disconnect = udl_usb_disconnect,
  .id_table = id_table,
};

static int __init udl_init(void)
{
	return drm_usb_init(&driver, &udl_driver);
}

static void __exit udl_exit(void)
{
	drm_usb_exit(&driver, &udl_driver);
}

module_init(udl_init);
module_exit(udl_exit);
