/* decode display ID block */

/* just enough coding to get tiling blocks from new monitors */

#include "drmP.h"
#include "drm_edid.h"
#include "drm_displayid.h"

int drm_parse_display_id(u8 *displayid, int length, bool is_edid_extension)
{
	/* if this is an EDID extension the first byte will be 0x70 */
	int idx = 0;
	struct displayid_hdr *base;
	struct displayid_block *block;
	u8 csum = 0;
	int i;
	if (is_edid_extension)
		idx = 1;

	base = (struct displayid_hdr *)&displayid[idx];

	printk("base revision 0x%x, length %d, %d %d\n",
	       base->rev, base->bytes, base->prod_id, base->ext_count);

	if (base->bytes + 5 > length - idx)
		return -EINVAL;

	for (i = idx; i <= base->bytes + 5; i++) {
		csum += displayid[i];
	}
	if (csum) {
		DRM_ERROR("DisplayID checksum invalid, remainder is %d\n", csum);
		return -EINVAL;
	}

	block = (struct displayid_block *)&displayid[idx + 4];
	printk("block id %d, rev %d, len %d\n", 
	       block->tag, block->rev, block->num_bytes);

	switch (block->tag) {
	case DATA_BLOCK_TILED_DISPLAY: {
		struct displayid_tiled_block *tile = (struct displayid_tiled_block *)block;
		u16 w, h;
		u8 tile_v_loc, tile_h_loc;
		u8 num_v_tile, num_h_tile;

		w = tile->tile_size[0] | tile->tile_size[1] << 8;
		h = tile->tile_size[2] | tile->tile_size[3] << 8;

		num_v_tile = (tile->topo[0] & 0xf) | (tile->topo[2] & 0x30);
		num_h_tile = (tile->topo[0] >> 4) | ((tile->topo[2] >> 2) & 0x30);
		tile_v_loc = (tile->topo[1] & 0xf) | ((tile->topo[2] & 0x3) << 4);
		tile_h_loc = (tile->topo[1] >> 4) | (((tile->topo[2] >> 2) & 0x3) << 4);

		printk("tile cap %d\n", tile->tile_cap);
		printk("tile_size %d x %d\n", w, h);
		printk("topo num tiles %dx%d, location %dx%d\n",
		       num_h_tile, num_v_tile, tile_h_loc, tile_v_loc);
		printk("vend %c%c%c\n", tile->topology_id[0], tile->topology_id[1], tile->topology_id[2]);
	}
		break;
	default:
		printk("unknown displayid tag %d\n", block->tag);
		break;
	}
	return 0;
}
