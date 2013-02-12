/*
 * Copyright (C) 2009 Red Hat <bskeggs@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *  Alon Levy <alevy@redhat.com>
 */

#include <linux/debugfs.h>

#include "drmP.h"
#include "qxl_drv.h"
#include "qxl_object.h"

int qxl_log_level;
int qxl_debug_disable_fb;

static void ppm_save(int width, int height, int bytes_per_pixel, int line_size,
		     int bits_per_pixel, uint8_t *d1, struct seq_file *m)
{
	uint8_t *d;
	uint32_t v;
	int y, x;
	uint8_t r, g, b;
	int ret;
	char *linebuf, *pbuf;
	int rshift = 16;
	int gshift = 8;
	int bshift = 0;
	int rmax = 255;
	int gmax = 255;
	int bmax = 255;

	DRM_INFO("%s: hwd %d,%d,%d bpp %d line %d\n", __func__,
		height, width, bytes_per_pixel, bits_per_pixel, line_size);
	seq_printf(m, "P6\n%d %d\n%d\n", width, height, 255);
	linebuf = kmalloc(width * 3, GFP_KERNEL);
	for (y = 0; y < height; y++) {
		d = d1;
		pbuf = linebuf;
		for (x = 0; x < width; x++) {
			if (bits_per_pixel == 32)
				v = *(uint32_t *)d;
			else
				v = (uint32_t) (*(uint16_t *)d);
			r = ((v >> rshift) & rmax) * 256 / (rmax + 1);
			g = ((v >> gshift) & gmax) * 256 / (gmax + 1);
			b = ((v >> bshift) & bmax) * 256 / (bmax + 1);
			*pbuf++ = r;
			*pbuf++ = g;
			*pbuf++ = b;
			d += bytes_per_pixel;
		}
		d1 += line_size;
		ret = seq_write(m, linebuf, pbuf - linebuf);
	}
	kfree(linebuf);
}

static void ppm_save_qxl_fb(struct qxl_framebuffer *qxl_fb, struct seq_file *m)
{
	struct qxl_bo *qobj = gem_to_qxl_bo(qxl_fb->obj);
	int width = qxl_fb->base.width;
	int height = qxl_fb->base.height;
	int bytes_per_pixel = qxl_fb->base.bits_per_pixel / 8;
	int line_size = qxl_fb->base.pitches[0];
	int bits_per_pixel = qxl_fb->base.bits_per_pixel;
	uint8_t *d1 = qobj->kptr;

	ppm_save(width, height, bytes_per_pixel, line_size, bits_per_pixel,
		 d1, m);
}

static int
qxl_debugfs_dumbppm(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_rect area;

	if (qdev->active_user_framebuffer) {
		ppm_save_qxl_fb(qdev->active_user_framebuffer, m);
	} else if (qdev->fbdev_qfb) {
		area.top = area.left = 0;
		area.right = qdev->fbdev_qfb->base.width;
		area.bottom = qdev->fbdev_qfb->base.height;
		qxl_io_update_area(qdev, gem_to_qxl_bo(qdev->fbdev_qfb->obj), &area);
		ppm_save_qxl_fb(qdev->fbdev_qfb, m);
	}
	return 0;
}

struct release_idr_data {
	struct seq_file *m;
	int total_bo;
};

static int idr_iter_fn(int id, void *p, void *data)
{
	struct release_idr_data *release_data = data;
	struct seq_file *m = release_data->m;
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_release *release = qxl_release_from_id_locked(qdev, id);

	seq_printf(m, "%d, type %d bo %d\n", id, release->type,
		   release->bo_count);
	release_data->total_bo += release->bo_count;
	return 0;
}

static int
qxl_debugfs_release(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_ring_header *r = &qdev->ram_header->release_ring_hdr;
	struct release_idr_data idr_data;

	idr_data.m = m;
	idr_data.total_bo = 0;
	spin_lock(&qdev->release_idr_lock);
	idr_for_each(&qdev->release_idr, idr_iter_fn, &idr_data);
	spin_unlock(&qdev->release_idr_lock);
	seq_printf(m, "ring %d [%d,%d] (%d,%d)\n", r->num_items, r->prod,
		   r->cons, r->notify_on_prod, r->notify_on_cons);
	seq_printf(m, "collected %d, release bo's %d / %d ttm\n",
		   qxl_garbage_collect(qdev),
		   idr_data.total_bo,
		   atomic_read(&qdev->mman.bdev.glob->bo_count));
	return 0;
}

#define DRAW_WIDTH 256
#define DRAW_HEIGHT 96
static uint32_t draw_data[DRAW_WIDTH * DRAW_HEIGHT];
static int draw_line;

static void
qxl_debugfs_draw_depth(struct seq_file *m, void *data, int depth,
		       uint32_t *palette, int workqueue)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_fb_image qxl_fb_image;
	struct fb_image *image = &qxl_fb_image.fb_image;
	uint32_t stride = DRAW_WIDTH * depth / 32;
	uint32_t *p = &draw_data[draw_line * stride];
	int i;
	static int color_ind;
	int fg[] = {0xaaaaaa, 0xff55ff, 0xddff33};

	draw_line = (draw_line + 1) % DRAW_HEIGHT;

	for (i = 0 ; i < stride; ++i, ++p)
		*p = ~*p;
	qxl_fb_image.qdev = qdev;
	qxl_fb_image.visual = FB_VISUAL_DIRECTCOLOR;
	image->dx = 300;
	image->dy = 100;
	image->width = DRAW_WIDTH;
	image->height = DRAW_HEIGHT;
	image->depth = depth;
	image->data = (char *)draw_data;
	if (depth == 1) {
		if (palette) {
			memcpy(qxl_fb_image.pseudo_palette, palette,
			       sizeof(qxl_fb_image.pseudo_palette));
			image->fg_color = 1;
			image->bg_color = 0;
		} else {
			qxl_fb_image.visual = FB_VISUAL_MONO10;
			image->fg_color = fg[color_ind];
			image->bg_color = 0;
		}
		color_ind = (color_ind + 1) % (sizeof(fg) / sizeof(fg[0]));
	}
	if (workqueue)
		qxl_fb_queue_imageblit(qdev, &qxl_fb_image, NULL, NULL);
	else
		qxl_draw_opaque_fb(&qxl_fb_image, 0);
}

static int
qxl_debugfs_draw_32(struct seq_file *m, void *data)
{
	qxl_debugfs_draw_depth(m, data, 32, NULL, 0);
	return 0;
}

static int
qxl_debugfs_draw_1(struct seq_file *m, void *data)
{
	qxl_debugfs_draw_depth(m, data, 1, NULL, 0);
	return 0;
}

static int
qxl_debugfs_oom(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;

	qxl_io_notify_oom(qdev);
	return 0;
}

static int
qxl_debugfs_debug_enable(struct seq_file *m, void *data)
{
	qxl_log_level = 2;
	return 0;
}

static int
qxl_debugfs_debug_disable(struct seq_file *m, void *data)
{
	qxl_log_level = 0;
	return 0;
}

static int gem_idr_iter_fn(int id, void *p, void *data)
{
	++*(int *)data;
	return 0;
}

static int
qxl_debugfs_mem(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	int gem_count = 0;

	seq_printf(m, "ttm bo count %d\n",
		   atomic_read(&qdev->mman.bdev.glob->bo_count));
	gem_count = 0;
	spin_lock(&qdev->ddev->object_name_lock);
	idr_for_each(&qdev->ddev->object_name_idr, gem_idr_iter_fn, &gem_count);
	spin_unlock(&qdev->ddev->object_name_lock);
	seq_printf(m, "gem object_name count %d\n", gem_count);
	return 0;
}

/* test new bo and free of bo */
static int
qxl_debugfs_bo_test(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_bo *bo;
	int ret;

	ret = qxl_bo_create(qdev, 8192, false /* not kernel, device */,
			    QXL_GEM_DOMAIN_VRAM, NULL, &bo);
	if (unlikely(ret != 0)) {
		seq_printf(m, "failed to create bo of 8192 bytes\n");
		return 0;
	}
	/* TODO - pin test as well. qxl_allocnf test generally. */
	qxl_bo_unref(&bo);
	return 0;
}

/* test new idr and release of idr */
static int
qxl_debugfs_alloc_test(struct seq_file *m, void *data)
{
	seq_printf(m, "implement me\n");
	return 0;
}

static int
qxl_debugfs_fb_wq_image(struct seq_file *m, void *data)
{
	uint32_t palette[16] = {0, 0xffffff, 0};
	qxl_debugfs_draw_depth(m, data, 1, palette, 1);
	return 0;
}

static void
set_rect(struct qxl_rect *r, int top, int left, int bottom, int right)
{
	r->top = top;
	r->left = left;
	r->bottom = bottom;
	r->right = right;
}

static int
qxl_debugfs_fb_wq_fill(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_draw_fill qxl_draw_fill_rec;

	qxl_draw_fill_rec.qdev = qdev;
	set_rect(&qxl_draw_fill_rec.rect, 100, 100, 200, 400);
	qxl_draw_fill_rec.color = 0x00ff0000;
	qxl_draw_fill_rec.rop = SPICE_ROPD_OP_PUT;
	qxl_fb_queue_draw_fill(&qxl_draw_fill_rec);
	return 0;
}


static int
qxl_debugfs_fb_enable(struct seq_file *m, void *data)
{
	qxl_debug_disable_fb = 0;
	return 0;
}

static int
qxl_debugfs_fb_disable(struct seq_file *m, void *data)
{
	qxl_debug_disable_fb = 1;
	return 0;
}

static int
qxl_debugfs_irq_received(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;

	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_display));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_cursor));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_io_cmd));
	seq_printf(m, "%d\n", qdev->irq_received_error);
	return 0;
}

static int
qxl_debugfs_read_client_monitors_config(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;

	qxl_display_read_client_monitors_config(qdev);
	return 0;
}

static int
qxl_debugfs_set_monitors_config(struct seq_file *m, int width, int height)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;

	qxl_alloc_client_monitors_config(qdev, 1);
	if (!qdev->client_monitors_config) {
		qxl_io_log(qdev, "%s: no memory\n", __func__);
		return 0;
	}

	qdev->monitors_config->count = 1;
	qdev->monitors_config->heads[0].width = width;
	qdev->monitors_config->heads[0].height = height;
	qdev->monitors_config->heads[0].x = 0;
	qdev->monitors_config->heads[0].y = 0;
	qxl_crtc_set_from_monitors_config(qdev);
	qxl_send_monitors_config(qdev);
	drm_sysfs_hotplug_event(qdev->ddev);
	return 0;
}

static int
qxl_debugfs_set_monitor_924_668(struct seq_file *m, void *data)
{
	return qxl_debugfs_set_monitors_config(m, 924, 668);
}

static int
qxl_debugfs_set_monitor_820_620(struct seq_file *m, void *data)
{
	return qxl_debugfs_set_monitors_config(m, 820, 620);
}

static int
qxl_debugfs_buffers_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_bo *bo;

	list_for_each_entry(bo, &qdev->gem.objects, list) {
		seq_printf(m, "size %d, pc %d, sync obj %p, num releases %d\n",
			   bo->gem_base.size, bo->pin_count,
			   bo->tbo.sync_obj, bo->fence.num_active_releases);
	}
	return 0;
}

static struct drm_info_list qxl_debugfs_list[] = {
	{ "dumbppm", qxl_debugfs_dumbppm, 0, NULL },
	{ "release", qxl_debugfs_release, 0, NULL },
	{ "draw32", qxl_debugfs_draw_32, 0, NULL },
	{ "draw1", qxl_debugfs_draw_1, 0, NULL },
	{ "oom", qxl_debugfs_oom, 0, NULL },
	{ "mem", qxl_debugfs_mem, 0, NULL },
	{ "bo_test", qxl_debugfs_bo_test, 0, NULL },
	{ "alloc_test", qxl_debugfs_alloc_test, 0, NULL },
	{ "fb_wq_image", qxl_debugfs_fb_wq_image, 0, NULL },
	{ "fb_wq_fill", qxl_debugfs_fb_wq_fill, 0, NULL },
	{ "read_client_monitors_config",
		qxl_debugfs_read_client_monitors_config, 0, NULL },
	{ "mon_924_668", qxl_debugfs_set_monitor_924_668, 0, NULL },
	{ "mon_820_620", qxl_debugfs_set_monitor_820_620, 0, NULL },
	/* TODO: read int from user; echo debug_level > *
	 * /sys/kernel/debugfs/dri/0/debug		*/
	{ "debug_enable", qxl_debugfs_debug_enable, 0, NULL },
	{ "debug_disable", qxl_debugfs_debug_disable, 0, NULL },
	{ "fb_enable", qxl_debugfs_fb_enable, 0, NULL },
	{ "fb_disable", qxl_debugfs_fb_disable, 0, NULL },
	{ "irq_received", qxl_debugfs_irq_received, 0, NULL },
	{ "qxl_buffers", qxl_debugfs_buffers_info, 0, NULL },
};
#define NOUVEAU_DEBUGFS_ENTRIES ARRAY_SIZE(qxl_debugfs_list)

int
qxl_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(qxl_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
	return 0;
}

void
qxl_debugfs_takedown(struct drm_minor *minor)
{
	drm_debugfs_remove_files(qxl_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor);
}

int qxl_debugfs_add_files(struct qxl_device *qdev,
			  struct drm_info_list *files,
			  unsigned nfiles)
{
	unsigned i;

	for (i = 0; i < qdev->debugfs_count; i++) {
		if (qdev->debugfs[i].files == files) {
			/* Already registered */
			return 0;
		}
	}

	i = qdev->debugfs_count + 1;
	if (i > QXL_DEBUGFS_MAX_COMPONENTS) {
		DRM_ERROR("Reached maximum number of debugfs components.\n");
		DRM_ERROR("Report so we increase "
		          "QXL_DEBUGFS_MAX_COMPONENTS.\n");
		return -EINVAL;
	}
	qdev->debugfs[qdev->debugfs_count].files = files;
	qdev->debugfs[qdev->debugfs_count].num_files = nfiles;
	qdev->debugfs_count = i;
#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_create_files(files, nfiles,
				 qdev->ddev->control->debugfs_root,
				 qdev->ddev->control);
	drm_debugfs_create_files(files, nfiles,
				 qdev->ddev->primary->debugfs_root,
				 qdev->ddev->primary);
#endif
	return 0;
}

static void qxl_debugfs_remove_files(struct qxl_device *qdev)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned i;

	for (i = 0; i < qdev->debugfs_count; i++) {
		drm_debugfs_remove_files(qdev->debugfs[i].files,
					 qdev->debugfs[i].num_files,
					 qdev->ddev->control);
		drm_debugfs_remove_files(qdev->debugfs[i].files,
					 qdev->debugfs[i].num_files,
					 qdev->ddev->primary);
	}
#endif
}
