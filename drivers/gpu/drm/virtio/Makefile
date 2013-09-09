#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

virtio-gpu-y := virtio_drv.o virtio_kms.o virtio_drm_bus.o

obj-$(CONFIG_DRM_VIRTIO_GPU)+= virtio-gpu.o 
