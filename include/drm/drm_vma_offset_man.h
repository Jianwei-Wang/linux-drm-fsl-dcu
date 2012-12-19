
#ifndef DRM_VMA_OFFSET_MAN
#define DRM_VMA_OFFSET_MAN

#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <drm/drm_mm.h>

struct drm_mm_node;

struct drm_vma_offset_node {
	struct drm_mm_node *vm_node;
	struct rb_node vm_rb;
	uint64_t num_pages;
};

struct drm_vma_offset_manager {
	rwlock_t vm_lock;
	struct rb_root addr_space_rb;
	struct drm_mm addr_space_mm;
};

struct drm_vma_offset_node *drm_vma_offset_lookup(struct drm_vma_offset_manager *man,
						  unsigned long page_start,
						  unsigned long num_pages);

int drm_vma_offset_setup(struct drm_vma_offset_manager *man,
			 struct drm_vma_offset_node *node,
			 unsigned long num_pages);

void drm_vma_offset_destroy_locked(struct drm_vma_offset_manager *man,
				     struct drm_vma_offset_node *node);
void drm_vma_offset_destroy(struct drm_vma_offset_manager *man,
			      struct drm_vma_offset_node *node);
int drm_vma_offset_man_init(struct drm_vma_offset_manager *man, uint64_t file_page_offset, uint64_t size);
void drm_vma_offset_man_fini(struct drm_vma_offset_manager *man);

void drm_vma_unmap_mapping(struct address_space *dev_mapping,
			   struct drm_vma_offset_node *node);

static inline void drm_vma_node_reset(struct drm_vma_offset_node *node)
{
	node->vm_node = NULL;
}

static inline uint64_t drm_vma_node_start(struct drm_vma_offset_node *node)
{
	return node->vm_node->start;
}

static inline bool drm_vma_node_is_allocated(struct drm_vma_offset_node *node)
{
	return node->vm_node ? true : false;
}

static inline uint64_t drm_vma_node_offset_addr(struct drm_vma_offset_node *node)
{
	return ((uint64_t) node->vm_node->start) << PAGE_SHIFT;
}

#endif
