#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

virgl-y := virgl_drv.o virgl_kms.o virgl_ttm.o virgl_fb.o virgl_object.o virgl_gem.o virgl_dumb.o virgl_3d.o virgl_ioctl.o virgl_display.o virgl_debugfs.o

obj-$(CONFIG_DRM_VIRGL)+= virgl.o
