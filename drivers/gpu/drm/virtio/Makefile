#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

virtio-gpu-y := virtio_drv.o virtio_kms.o virtio_drm_bus.o virtio_gem.o virtio_ttm.o virtio_object.o virtio_fb.o virtio_display.o virtio_vq.o

obj-$(CONFIG_DRM_VIRTIO_GPU)+= virtio-gpu.o 
