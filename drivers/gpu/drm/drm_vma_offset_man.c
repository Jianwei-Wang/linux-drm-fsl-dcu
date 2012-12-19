
#include <drm/drmP.h>

static struct drm_vma_offset_node *drm_vma_offset_lookup_rb_locked(struct drm_vma_offset_manager *man,
							    unsigned long page_start,
							    unsigned long num_pages)
{
	struct rb_node *cur = man->addr_space_rb.rb_node;
	unsigned long cur_offset;
	struct drm_vma_offset_node *node;
	struct drm_vma_offset_node *best_node = NULL;

	while (likely(cur != NULL)) {
		node = rb_entry(cur, struct drm_vma_offset_node, vm_rb);
		cur_offset = node->vm_node->start;
		if (page_start >= cur_offset) {
			cur = cur->rb_right;
			best_node = node;
			if (page_start == cur_offset)
				break;
		} else
			cur = cur->rb_left;
	}
	if (unlikely(best_node == NULL))
		return NULL;

	if (unlikely((best_node->vm_node->start + best_node->num_pages) <
		     (page_start + num_pages)))
		return NULL;
	return best_node;
}

struct drm_vma_offset_node *drm_vma_offset_lookup(struct drm_vma_offset_manager *man,
						  unsigned long page_start,
						  unsigned long num_pages)
{
	struct drm_vma_offset_node *node;
	read_lock(&man->vm_lock);
	node = drm_vma_offset_lookup_rb_locked(man, page_start, num_pages);
	read_unlock(&man->vm_lock);
	return node;
}
EXPORT_SYMBOL(drm_vma_offset_lookup);

static void drm_vma_offset_insert_rb(struct drm_vma_offset_manager *man, struct drm_vma_offset_node *node)
{
	struct rb_node **cur = &man->addr_space_rb.rb_node;
	struct rb_node *parent = NULL;
	struct drm_vma_offset_node *cur_node;
	unsigned long offset = node->vm_node->start;
	unsigned long cur_offset;

	while (*cur) {
		parent = *cur;
		cur_node = rb_entry(parent, struct drm_vma_offset_node, vm_rb);
		cur_offset = cur_node->vm_node->start;
		if (offset < cur_offset)
			cur = &parent->rb_left;
		else if (offset > cur_offset)
			cur = &parent->rb_right;
		else
			BUG();
	}

	rb_link_node(&node->vm_rb, parent, cur);
	rb_insert_color(&node->vm_rb, &man->addr_space_rb);
}

void drm_vma_offset_destroy_locked(struct drm_vma_offset_manager *man,
				   struct drm_vma_offset_node *node)
{
	if (likely(node->vm_node != NULL)) {
		rb_erase(&node->vm_rb, &man->addr_space_rb);
		drm_mm_put_block(node->vm_node);
		node->vm_node = NULL;
	}
}

void drm_vma_offset_destroy(struct drm_vma_offset_manager *man,
			    struct drm_vma_offset_node *node)
{
	write_lock(&man->vm_lock);
	drm_vma_offset_destroy_locked(man, node);
	write_unlock(&man->vm_lock);
}
EXPORT_SYMBOL(drm_vma_offset_destroy);

int drm_vma_offset_setup(struct drm_vma_offset_manager *man,
			 struct drm_vma_offset_node *node,
			 unsigned long num_pages)
{
	int ret;

	list_init(&node->flist);
retry_pre_get:
	ret = drm_mm_pre_get(&man->addr_space_mm);
	if (unlikely(ret != 0))
		return ret;

	write_lock(&man->vm_lock);
	node->vm_node = drm_mm_search_free(&man->addr_space_mm,
					   num_pages, 0, 0);

	if (unlikely(node->vm_node == NULL)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	node->vm_node = drm_mm_get_block_atomic(node->vm_node,
						num_pages, 0);

	if (unlikely(node->vm_node == NULL)) {
		write_unlock(&man->vm_lock);
		goto retry_pre_get;
	}

	node->num_pages = num_pages;
	drm_vma_offset_insert_rb(man, node);
	write_unlock(&man->vm_lock);

	return 0;
out_unlock:
	write_unlock(&man->vm_lock);
	return ret;	
	
}
EXPORT_SYMBOL(drm_vma_offset_setup);

void drm_vma_unmap_mapping(struct address_space *dev_mapping,
			   struct drm_vma_offset_node *node)
{
	if (dev_mapping && drm_vma_node_is_allocated(node)) {
		unmap_mapping_range(dev_mapping,
				    drm_vma_node_offset_addr(node),
				    node->num_pages << PAGE_SHIFT, 1);
	}
}
EXPORT_SYMBOL(drm_vma_unmap_mapping);

int drm_vma_offset_man_init(struct drm_vma_offset_manager *man, uint64_t file_page_offset, uint64_t size)
{
	int ret;
	rwlock_init(&man->vm_lock);
	man->addr_space_rb = RB_ROOT;
	ret = drm_mm_init(&man->addr_space_mm, file_page_offset, size);
	if (unlikely(ret != 0))
		goto out_no_addr_mm;
	return 0;
out_no_addr_mm:
	return ret;
}
EXPORT_SYMBOL(drm_vma_offset_man_init);

void drm_vma_offset_man_fini(struct drm_vma_offset_manager *man)
{
	BUG_ON(!drm_mm_clean(&man->addr_space_mm));
	write_lock(&man->vm_lock);
	drm_mm_takedown(&man->addr_space_mm);
	write_unlock(&man->vm_lock);
}
EXPORT_SYMBOL(drm_vma_offset_man_fini);

int drm_vma_offset_node_add_file(struct drm_vma_offset_node *node,
				 struct file *filp)
{
	struct drm_vma_offset_node_per_file *fnode;

	fnode = kmalloc(sizeof(*fnode), GFP_KERNEL);
	if (!fnode)
		return -ENOMEM;

	fnode->filp = filp;
	list_add(&fnode->lhead, &node->flist);
	return 0;
}
EXPORT_SYMBOL(drm_vma_offset_node_add_file);

void drm_vma_offset_node_remove_file(struct drm_vma_offset_node *node,
			       struct file *filp)
{
	struct drm_vma_offset_node_per_file *fnode, *temp;

	list_for_each_entry_safe(fnode, temp, &node->flist, lhead) {
		if (fnode->filp == filp) {
			list_del(&fnode->lhead);
			kfree(fnode);
			break;
		}
	}
}
EXPORT_SYMBOL(drm_vma_offset_node_remove_file);

bool drm_vma_offset_node_valid_file(struct drm_vma_offset_node *node,
				    struct file *filp)
{
	struct drm_vma_offset_node_per_file *fnode;
	list_for_each_entry(fnode, &node->flist, lhead) {	
		if (fnode->filp == filp)
			return true;
	}
	return false;
}
EXPORT_SYMBOL(drm_vma_offset_node_valid_file);
