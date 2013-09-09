#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

virtio-gpu-y := virtgpu_drv.o virtgpu_kms.o virtgpu_drm_bus.o virtgpu_gem.o virtgpu_ttm.o virtgpu_object.o virtgpu_fb.o virtgpu_display.o virtgpu_vq.o

obj-$(CONFIG_DRM_VIRTIO_GPU)+= virtio-gpu.o 
