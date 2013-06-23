/*
 * Copyright (C) 2009 Red Hat
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
#include "virgl_drv.h"
#include "virgl_object.h"

static int
virgl_debugfs_irq_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct virgl_device *qdev = node->minor->dev->dev_private;

	seq_printf(m, "irqs: vring: %d fence: %d\n", atomic_read(&qdev->irq_count_vbuf), atomic_read(&qdev->irq_count_fence));
	seq_printf(m, "fence %d %ld %lld\n", virgl_fence_read(qdev),
		   atomic64_read(&qdev->fence_drv.last_seq),
		   qdev->fence_drv.sync_seq);
	if (!spin_trylock(&qdev->cmdq_lock))
		seq_printf(m, "can't lock vring\n");
	else {
		seq_printf(m, "vring: %d alloc: %d freed: %d\n", qdev->cmdq->num_free, qdev->num_alloc, qdev->num_freed);
		spin_unlock(&qdev->cmdq_lock);
	}
	return 0;
}

static int
virgl_debugfs_debug_kick(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct virgl_device *qdev = node->minor->dev->dev_private;

	schedule_work(&qdev->dequeue_work);
	return 0;
}

static struct drm_info_list virgl_debugfs_list[] = {
	{ "irq_fence", virgl_debugfs_irq_info, 0, NULL },
	{ "debug_kick", virgl_debugfs_debug_kick, 0, NULL },
};

#define VIRGL_DEBUGFS_ENTRIES ARRAY_SIZE(virgl_debugfs_list)

int
virgl_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(virgl_debugfs_list, VIRGL_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
	return 0;
}

void
virgl_debugfs_takedown(struct drm_minor *minor)
{
	drm_debugfs_remove_files(virgl_debugfs_list, VIRGL_DEBUGFS_ENTRIES,
				 minor);
}
