/*
 * Copyright 2008 Jerome Glisse.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon_reg.h"
#include "radeon.h"

void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt);

int radeon_cs_parser_relocs(struct radeon_cs_parser *p)
{
	struct drm_device *ddev = p->rdev->ddev;
	struct radeon_cs_chunk *chunk;
	unsigned i, j;
	bool duplicate;

	if (p->chunk_relocs_idx == -1) {
		return 0;
	}
	chunk = &p->chunks[p->chunk_relocs_idx];
	/* FIXME: we assume that each relocs use 4 dwords */
	p->nrelocs = chunk->length_dw / 4;
	p->relocs_ptr = kcalloc(p->nrelocs, sizeof(void *), GFP_KERNEL);
	if (p->relocs_ptr == NULL) {
		return -ENOMEM;
	}
	p->relocs = kcalloc(p->nrelocs, sizeof(struct radeon_cs_reloc), GFP_KERNEL);
	if (p->relocs == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < p->nrelocs; i++) {
		struct drm_radeon_cs_reloc *r;

		duplicate = false;
		r = (struct drm_radeon_cs_reloc *)&chunk->kdata[i*4];
		for (j = 0; j < p->nrelocs; j++) {
			if (r->handle == p->relocs[j].handle) {
				p->relocs_ptr[i] = &p->relocs[j];
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			p->relocs[i].gobj = drm_gem_object_lookup(ddev,
								  p->filp,
								  r->handle);
			if (p->relocs[i].gobj == NULL) {
				DRM_ERROR("gem object lookup failed 0x%x\n",
					  r->handle);
				return -EINVAL;
			}
			p->relocs_ptr[i] = &p->relocs[i];
			p->relocs[i].robj = p->relocs[i].gobj->driver_private;
			p->relocs[i].lobj.bo = p->relocs[i].robj;
			p->relocs[i].lobj.rdomain = r->read_domains;
			p->relocs[i].lobj.wdomain = r->write_domain;
			p->relocs[i].handle = r->handle;
			p->relocs[i].flags = r->flags;
			INIT_LIST_HEAD(&p->relocs[i].lobj.list);
			radeon_bo_list_add_object(&p->relocs[i].lobj,
						&p->validated);
		}
	}
	return radeon_bo_list_validate(&p->validated);
}

int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data)
{
	struct drm_radeon_cs *cs = data;
	uint64_t *chunk_array_ptr;
	unsigned size, i;

	if (!cs->num_chunks) {
		return 0;
	}
	/* get chunks */
	INIT_LIST_HEAD(&p->validated);
	p->chunk_final_ib_idx = -1;
	p->chunk_relocs_idx = -1;
	p->chunks_array = kcalloc(cs->num_chunks, sizeof(uint64_t), GFP_KERNEL);
	if (p->chunks_array == NULL) {
		DRM_ERROR("fail alloc chunks array %d\n", cs->num_chunks);
		return -ENOMEM;
	}
	chunk_array_ptr = (uint64_t *)(unsigned long)(cs->chunks);
	if (DRM_COPY_FROM_USER(p->chunks_array, chunk_array_ptr,
			       sizeof(uint64_t)*cs->num_chunks)) {
		DRM_ERROR("failed copy chun array %d\n", cs->num_chunks);
		return -EFAULT;
	}
	p->nchunks = cs->num_chunks;
	p->chunks = kcalloc(p->nchunks, sizeof(struct radeon_cs_chunk), GFP_KERNEL);
	if (p->chunks == NULL) {
		DRM_ERROR("failed calloc cunks %d\n", cs->num_chunks);
		return -ENOMEM;
	}
	for (i = 0; i < p->nchunks; i++) {
		struct drm_radeon_cs_chunk __user **chunk_ptr = NULL;
		struct drm_radeon_cs_chunk user_chunk;
		uint32_t __user *cdata;

		chunk_ptr = (void __user*)(unsigned long)p->chunks_array[i];
		if (DRM_COPY_FROM_USER(&user_chunk, chunk_ptr,
				       sizeof(struct drm_radeon_cs_chunk))) {
			return -EFAULT;
		}
		p->chunks[i].length_dw = user_chunk.length_dw;
		p->chunks[i].kdata = NULL;
		p->chunks[i].chunk_id = user_chunk.chunk_id;
		p->chunks[i].idx = 0;
		p->chunks[i].parser = p;

		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_RELOCS) {
			p->chunk_relocs_idx = i;
		}
		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_IB) {
			/* zero length IB isn't useful */
			if ((p->chunks[i].length_dw == 0) || (p->chunks[i].length_dw > (16 * 1024))) {
				DRM_ERROR("ib chunk %d/%d too large %d\n", i, p->num_ib_chunks, p->chunks[i].length_dw);
				return -EINVAL;
			}
			p->chunk_ib_idx[p->num_ib_chunks++] = i;
			p->chunk_final_ib_idx = i;
		}

		p->chunks[i].user_ptr = (void __user *)(unsigned long)user_chunk.chunk_data;

		cdata = (uint32_t *)(unsigned long)user_chunk.chunk_data;
		if (p->chunks[i].chunk_id != RADEON_CHUNK_ID_IB) {
			size = p->chunks[i].length_dw * sizeof(uint32_t);
			p->chunks[i].kdata = kmalloc(size, GFP_KERNEL);
			if (p->chunks[i].kdata == NULL) {
				DRM_ERROR("failed kmalloc non-ib chunk failed %d\n", size);
				return -ENOMEM;
			}
			if (DRM_COPY_FROM_USER(p->chunks[i].kdata,
					       p->chunks[i].user_ptr, size)) {
				return -EFAULT;
			}
		} else {
			p->chunks[i].kpage[0] = kmalloc(PAGE_SIZE, GFP_KERNEL);
			p->chunks[i].kpage[1] = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (p->chunks[i].kpage[0] == NULL || p->chunks[i].kpage[1] == NULL) {
				kfree(p->chunks[i].kpage[0]);
				kfree(p->chunks[i].kpage[1]);
				DRM_ERROR("failed alloc pages\n");
				return -ENOMEM;
			}
			p->chunks[i].kpage_idx[0] = -1;
			p->chunks[i].kpage_idx[1] = -1;
			p->chunks[i].last_copied_page = -1;
			p->chunks[i].last_page_index = ((p->chunks[i].length_dw * 4) - 1) / PAGE_SIZE;
		}
	}

	return 0;
}

/**
 * cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 *
 * If error is set than unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void radeon_cs_parser_fini(struct radeon_cs_parser *parser, int error)
{
	unsigned i;

	if (!error && parser->chunks[parser->chunk_final_ib_idx].ib)
		radeon_bo_list_fence(&parser->validated, parser->chunks[parser->chunk_final_ib_idx].ib->fence);

	radeon_bo_list_unreserve(&parser->validated);
	if (parser->relocs != NULL) {
		for (i = 0; i < parser->nrelocs; i++) {
			if (parser->relocs[i].gobj)
				drm_gem_object_unreference_unlocked(parser->relocs[i].gobj);
		}
	}
	kfree(parser->relocs);
	kfree(parser->relocs_ptr);
	for (i = 0; i < parser->nchunks; i++) {
		radeon_ib_free(parser->rdev, &parser->chunks[i].ib);
		kfree(parser->chunks[i].kdata);
		kfree(parser->chunks[i].kpage[0]);
		kfree(parser->chunks[i].kpage[1]);
	}
	kfree(parser->chunks);
	kfree(parser->chunks_array);
}

static int radeon_cs_parser_chunk(struct radeon_cs_parser *parser, int chunk_id, bool flush)
{
	struct radeon_device *rdev = parser->rdev;
	struct radeon_cs_chunk *chunk;
	int r;

	chunk = &parser->chunks[chunk_id];
	r = radeon_ib_get(parser->rdev, &chunk->ib);
	if (r) {
		DRM_ERROR("Failed to get ib !\n");
		return r;
	}
	chunk->ib->fence->flush = flush;

	if (chunk->parser->num_ib_chunks > 1)
		DRM_INFO("chunks %d  length %d\n", chunk_id, chunk->length_dw);
	chunk->ib->length_dw = chunk->length_dw;
	/* Copy the packet into the IB, the parser will read from the
	 * input memory (cached) and write to the IB (which can be
	 * uncached). */
	r = radeon_cs_parse(parser, chunk);
	if (r || parser->parser_error) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}
	r = radeon_cs_finish_pages(chunk);
	if (r) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}
	r = radeon_ib_schedule(rdev, chunk->ib);
	if (r) {
		DRM_ERROR("Faild to schedule IB !\n");
	}
	return 0;
}

int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_cs_parser parser;
	int r;
	int i;

	mutex_lock(&rdev->cs_mutex);

	if (!rdev->current_track) {
		rdev->current_track = radeon_cs_create_tracker(rdev);
	}
	  
	/* initialize parser */
	memset(&parser, 0, sizeof(struct radeon_cs_parser));
	parser.filp = filp;
	parser.rdev = rdev;
	parser.dev = rdev->dev;
	parser.track = rdev->current_track;
	r = radeon_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser !\n");
		goto out;
	}

	r = radeon_cs_parser_relocs(&parser);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to parse relocation %d!\n", r);
		goto out;
	}

	radeon_cs_clear_tracker(rdev, parser.track);

	if (parser.num_ib_chunks > 1)
		DRM_INFO("submitting chunks %d\n", parser.num_ib_chunks);

	for (i = 0; i < parser.num_ib_chunks; i++) {
		bool flush = (parser.chunk_ib_idx[i] == parser.chunk_final_ib_idx);
		if (parser.num_ib_chunks > 1)
			DRM_INFO("submitting chunks %d %d %d\n", i, parser.chunk_ib_idx[i], flush);
		r = radeon_cs_parser_chunk(&parser, parser.chunk_ib_idx[i], flush);
		if (r)
			goto out;
	}
out:
	radeon_cs_parser_fini(&parser, r);
	mutex_unlock(&rdev->cs_mutex);
	return r;
}

int radeon_cs_finish_pages(struct radeon_cs_chunk *chunk)
{
	int i;
	int size = PAGE_SIZE;

	for (i = chunk->last_copied_page + 1; i <= chunk->last_page_index; i++) {
		if (i == chunk->last_page_index) {
			size = (chunk->length_dw * 4) % PAGE_SIZE;
			if (size == 0)
				size = PAGE_SIZE;
		}
		
		if (DRM_COPY_FROM_USER(chunk->ib->ptr + (i * (PAGE_SIZE/4)),
				       chunk->user_ptr + (i * PAGE_SIZE),
				       size))
			return -EFAULT;
	}
	return 0;
}

int radeon_cs_update_pages(struct radeon_cs_chunk *chunk,
			   int pg_idx)
{
	int new_page;
	int i;
	int size = PAGE_SIZE;

	for (i = chunk->last_copied_page + 1; i < pg_idx; i++) {
		if (DRM_COPY_FROM_USER(chunk->ib->ptr + (i * (PAGE_SIZE/4)),
				       chunk->user_ptr + (i * PAGE_SIZE),
				       PAGE_SIZE)) {
			chunk->parser->parser_error = -EFAULT;
			return 0;
		}
	}

	new_page = chunk->kpage_idx[0] < chunk->kpage_idx[1] ? 0 : 1;

	if (pg_idx == chunk->last_page_index) {
		size = (chunk->length_dw * 4) % PAGE_SIZE;
			if (size == 0)
				size = PAGE_SIZE;
	}

	if (DRM_COPY_FROM_USER(chunk->kpage[new_page],
			       chunk->user_ptr + (pg_idx * PAGE_SIZE),
			       size)) {
		chunk->parser->parser_error = -EFAULT;
		return 0;
	}

	/* copy to IB here */
	memcpy((void *)(chunk->ib->ptr+(pg_idx*(PAGE_SIZE/4))), chunk->kpage[new_page], size);

	chunk->last_copied_page = pg_idx;
	chunk->kpage_idx[new_page] = pg_idx;

	return new_page;
}
