/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_GEM_H_
#define _AMDXDNA_GEM_H_

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

struct amdxdna_mem {
	u64			userptr;
	void			*kva;
	u64			dev_addr;
	size_t			size;
	struct page		**pages;
	u32			nr_pages;
	int			pin_cnt;
};

struct amdxdna_gem_obj {
	struct drm_gem_shmem_object	base;
	struct amdxdna_client		*client;
	u8				type;
	bool				pinned;
	struct mutex			lock; /* Protects: pinned, assigned_hwctx */
	u64				mmap_offset;
	struct amdxdna_mem		mem;
	struct amdxdna_gem_obj		*dev_heap;
	struct drm_mm			mm;
	struct drm_mm_node		mm_node;
	u32				assigned_hwctx;
};

#define to_gobj(obj)    (&(obj)->base.base)

static inline struct amdxdna_gem_obj *to_xdna_obj(struct drm_gem_object *gobj)
{
	return container_of(gobj, struct amdxdna_gem_obj, base.base);
}

struct amdxdna_gem_obj *amdxdna_gem_get_obj(struct amdxdna_client *client,
					    u32 bo_hdl, u8 bo_type);
static inline void amdxdna_gem_put_obj(struct amdxdna_gem_obj *abo)
{
	drm_gem_object_put(to_gobj(abo));
}

struct drm_gem_object *
amdxdna_gem_create_object(struct drm_device *dev, size_t size);
struct drm_gem_object *
amdxdna_gem_import_sg_table(struct drm_device *dev,
			    struct dma_buf_attachment *attach,
			    struct sg_table *sgt);

int amdxdna_gem_pin_nolock(struct amdxdna_gem_obj *abo);
int amdxdna_gem_pin(struct amdxdna_gem_obj *abo);
void amdxdna_gem_unpin(struct amdxdna_gem_obj *abo);

u32 amdxdna_gem_get_assigned_hwctx(struct amdxdna_client *client, u32 bo_hdl);
int amdxdna_gem_set_assigned_hwctx(struct amdxdna_client *client, u32 bo_hdl, u32 ctx_hdl);
void amdxdna_gem_clear_assigned_hwctx(struct amdxdna_client *client, u32 bo_hdl);
int amdxdna_drm_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_get_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_sync_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

#endif /* _AMDXDNA_GEM_H_ */
