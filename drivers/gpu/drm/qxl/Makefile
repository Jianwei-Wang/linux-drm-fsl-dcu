#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

qxl-y := qxl_drv.o qxl_kms.o qxl_display.o qxl_ttm.o qxl_fb.o qxl_object.o qxl_gem.o qxl_cmd.o lookup3.o qxl_image.o

obj-$(CONFIG_DRM_QXL)+= qxl.o
