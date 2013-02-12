#include <linux/gfp.h>
#include <linux/slab.h>
#if 0
#include <linux/jhash.h>
#endif

#include "qxl_drv.h"

struct image_info {
	struct qxl_image *image;
	int ref_count;
	struct image_info *next;
};

#define HASH_SIZE 4096
static struct image_info *image_table[HASH_SIZE];

#if 0
static unsigned int
hash_and_copy(const uint8_t *src, int src_stride,
	       uint8_t *dest, int dest_stride,
	       int width, int height)
{
	int i, j;
	unsigned int hash = 0;

	for (i = 0; i < height; ++i)    {
		const uint8_t *src_line = src + i * src_stride;
		uint8_t *dest_line = dest + i * dest_stride;

		for (j = 0; j < width; ++j) {
			uint32_t *s = (uint32_t *)src_line;
			uint32_t *d = (uint32_t *)dest_line;

			if (dest)
				d[j] = s[j];
		}

		hash = jhash(src_line, width * sizeof(uint32_t), hash);
	}

	return hash;
}

static struct image_info *
lookup_image_info(unsigned int hash,
		   int width,
		   int height)
{
	struct image_info *info = image_table[hash % HASH_SIZE];

	while (info) {
		struct qxl_image *image = info->image;

		if (image->descriptor.id == hash		&&
		    image->descriptor.width == width		&&
		    image->descriptor.height == height)	{
			return info;
		}

		info = info->next;
	}

	return NULL;
}
#endif

static struct image_info *
insert_image_info(unsigned int hash)
{
	struct image_info *info =
		kmalloc(sizeof(struct image_info), GFP_KERNEL);

	if (!info)
		return NULL;

	info->next = image_table[hash % HASH_SIZE];
	image_table[hash % HASH_SIZE] = info;

	return info;
}

#if 0
static void
remove_image_info(struct image_info *info)
{
	struct image_info **location =
		&image_table[info->image->descriptor.id % HASH_SIZE];

	while (*location && (*location) != info)
		location = &((*location)->next);

	if (*location)
		*location = info->next;

	kfree(info);
}
#endif

#if 0
void debug_fill(uint8_t *data, int width, int height)
{
	int xx, yy;
	uint32_t *p;
	static int count;

	for (yy = 0, p = data ; yy < height; ++yy) {
		for (xx = 0 ; xx < width ; ++xx) {
			(*p++) = (((count + yy) & 0xff) << 24) |
				 (((count + yy) & 0xff) << 16) |
				 (((count + yy) & 0xff) << 8) |
				 ((count + yy) & 0xff);
		}
	}
	++count;
}
#endif


static struct qxl_image *
qxl_image_create_helper(struct qxl_device *qdev,
			struct qxl_release *release,
			struct qxl_bo **image_bo,
			const uint8_t *data,
			int width, int height,
			int depth, unsigned int hash,
			int stride)
{
	struct qxl_image *image;
	struct qxl_data_chunk *chunk;
	struct image_info *info;
	int i;
	int chunk_stride;
	int linesize = width * depth / 8;
	struct qxl_bo *chunk_bo;

	/* Chunk */
	/* FIXME: Check integer overflow */
	/* TODO: variable number of chunks */
	chunk_stride = stride; /* TODO: should use linesize, but it renders
				  wrong (check the bitmaps are sent correctly
				  first) */
	chunk = qxl_allocnf(qdev, sizeof(*chunk) + height * chunk_stride,
			    release);
	chunk_bo = release->bos[release->bo_count - 1];

	chunk->data_size = height * chunk_stride;
	chunk->prev_chunk = 0;
	chunk->next_chunk = 0;

#if 0
	qxl_io_log(qdev, "%s: stride %d, chunk_s %d, linesize %d; "
			 "chunk %p, chunk->data %p, data %p\n",
		   __func__, stride, chunk_stride, linesize, chunk,
		   chunk ? chunk->data : NULL, data);
#endif
	/* TODO: put back hashing */
	if (stride == linesize && chunk_stride == stride)
		memcpy(chunk->data, data, linesize * height);
	else
		for (i = 0 ; i < height ; ++i)
			memcpy(chunk->data + i*chunk_stride, data + i*stride,
			       linesize);
	/* Image */
	image = qxl_allocnf(qdev, sizeof(*image), release);
	*image_bo = release->bos[release->bo_count - 1];

	image->descriptor.id = 0;
	image->descriptor.type = SPICE_IMAGE_TYPE_BITMAP;

	image->descriptor.flags = 0;
	image->descriptor.width = width;
	image->descriptor.height = height;

	switch (depth) {
	case 1:
		/* TODO: BE? check by arch? */
		image->u.bitmap.format = SPICE_BITMAP_FMT_1BIT_BE;
		break;
	case 24:
		image->u.bitmap.format = SPICE_BITMAP_FMT_24BIT;
		break;
	case 32:
		image->u.bitmap.format = SPICE_BITMAP_FMT_32BIT;
		break;
	default:
		DRM_ERROR("unsupported image bit depth\n");
		return NULL; /* TODO: cleanup */
	}
	image->u.bitmap.flags = QXL_BITMAP_TOP_DOWN;
	image->u.bitmap.x = width;
	image->u.bitmap.y = height;
	image->u.bitmap.stride = chunk_stride;
	image->u.bitmap.palette = 0;
	image->u.bitmap.data = qxl_bo_physical_address(qdev, chunk_bo, 0);

	/* Add to hash table */
	if (!hash) {
		QXL_INFO(qdev, "%s: id %d has size %d %d\n", __func__,
			image->descriptor.id, width, height);

		return image;
	}
	info = insert_image_info(hash);
	if (info) {
		info->image = image;
		info->ref_count = 1;

		image->descriptor.id = hash;
		image->descriptor.flags = QXL_IMAGE_CACHE;

	}
	QXL_INFO(qdev, "%s: id %d has size %d %d\n", __func__,
		image->descriptor.id, width, height);
	return image;
}

struct qxl_image *qxl_image_create(struct qxl_device *qdev,
				   struct qxl_release *release,
				   struct qxl_bo **image_bo,
				   const uint8_t *data,
				   int x, int y, int width, int height,
				   int depth, int stride)
{
#if 0
	unsigned int hash;
	struct image_info *info;
#endif

	data += y * stride + x * (depth / 8);
	/* TODO: return hashing */
	return qxl_image_create_helper(qdev, release, image_bo, data,
				       width, height, depth, 0, stride);

#if 0
	hash = hash_and_copy(data, stride, NULL, -1, width, height);

	info = lookup_image_info(hash, width, height);
	if (info) {
		int i, j;
		qxl_io_log(qdev, "%s: image found in cache\n", __func__);

		info->ref_count++;

		for (i = 0; i < height; ++i) {
			struct qxl_data_chunk *chunk;
			const uint8_t *src_line = data + i * stride;
			uint32_t *dest_line;

			chunk = qxl_fb_virtual_address(qdev,
					info->image->u.bitmap.data);

			dest_line = (uint32_t *)chunk->data + width * i;

			for (j = 0; j < width; ++j) {
				uint32_t *s = (uint32_t *)src_line;
				uint32_t *d = (uint32_t *)dest_line;

				if (d[j] != s[j])
					goto out;
			}
		}
out:
		return info->image;
	} else
		return qxl_image_create_helper(qdev, release, image_bo, data,
					       width, height, depth, hash,
					       stride);
#endif
}

void qxl_image_destroy(struct qxl_device *qdev,
		       struct qxl_image *image)
{
#if 0
	struct qxl_data_chunk *chunk;
	struct image_info *info;

	chunk = qxl_virtual_address(qdev, (void *)image->u.bitmap.data);

	info = lookup_image_info(image->descriptor.id,
				 image->descriptor.width,
				 image->descriptor.height);

	if (info && info->image == image) {
		--info->ref_count;

		if (info->ref_count != 0)
			return;

#if 0
		ErrorF("removed %p from hash table\n", info->image);
#endif

		remove_image_info(info);
	}

	qxl_free(qdev->mem, chunk);
	qxl_free(qdev->mem, image);
#endif
}

void qxl_drop_image_cache(struct qxl_device *qdev)
{
	memset(image_table, 0, HASH_SIZE * sizeof(struct image_info *));
}
