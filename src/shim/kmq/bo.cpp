// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2023-2024, Advanced Micro Devices, Inc. All rights reserved.

#include "bo.h"

namespace {

amdxdna_bo_type
flag_to_type(uint64_t bo_flags)
{
  auto flags = xcl_bo_flags{bo_flags};
  auto boflags = (static_cast<uint32_t>(flags.boflags) << 24);
  switch (boflags) {
  case XCL_BO_FLAGS_NONE:
  case XCL_BO_FLAGS_HOST_ONLY:
    return AMDXDNA_BO_SHMEM;
  case XCL_BO_FLAGS_CACHEABLE:
    return AMDXDNA_BO_DEV;
  case XCL_BO_FLAGS_EXECBUF:
    return AMDXDNA_BO_CMD;
  default:
    break;
  }
  return AMDXDNA_BO_INVALID;
}

}

namespace shim_xdna {

bo_kmq::
bo_kmq(const device& device, xrt_core::hwctx_handle::slot_id ctx_id,
  size_t size, uint64_t flags)
  : bo_kmq(device, ctx_id, size, flags, flag_to_type(flags))
{
  if (m_type == AMDXDNA_BO_INVALID)
    shim_err(EINVAL, "Invalid BO flags: 0x%lx", flags);
}

bo_kmq::
bo_kmq(const device& device, size_t size, amdxdna_bo_type type)
  : bo_kmq(device, INVALID_CTX_HANDLE, size, 0, type)
{
}

bo_kmq::
bo_kmq(const device& device, xrt_core::hwctx_handle::slot_id ctx_id,
  size_t size, uint64_t flags, amdxdna_bo_type type)
  : bo(device, ctx_id, size, flags, type)
{
  switch (m_type) {
  case AMDXDNA_BO_SHMEM:
    alloc_bo();
    m_buf = map(bo::map_type::write);
    // Do NOT remove and change the order of below two lines
    memset(m_buf, 0, size); // Make sure the mapping is settled
    sync(direction::host2device, size, 0); // avoid cache flush issue on output bo
    break;
  case AMDXDNA_BO_DEV:
    alloc_bo();
    m_buf = reinterpret_cast<void *>(m_bo->m_vaddr);
    break;
  case AMDXDNA_BO_DEV_HEAP:
    // Device mem heap must align at 64MB boundary.
    alloc_buf(64 * 1024 * 1024);
    alloc_bo();
    break;
  case AMDXDNA_BO_CMD:
    alloc_buf();
    alloc_bo();
    break;
  default:
    shim_err(EINVAL, "Invalid BO type: %d", type);
    break;
  }

  attach_to_ctx();

  shim_debug("Allocated KMQ BO for: userptr=0x%lx, size=%ld, flags=0x%llx",
    m_buf, m_size, m_flags);
}

bo_kmq::
~bo_kmq()
{
  shim_debug("Freeing KMQ BO, %s", describe().c_str());

  try {
    detach_from_ctx();
    // If BO is in use, we should block and wait in driver
    free_bo();
  } catch (const xrt_core::system_error& e) {
    shim_debug("Failed to free BO: %s", e.what());
  }

  switch (m_type) {
  case AMDXDNA_BO_SHMEM:
    unmap(m_buf);
    break;
  default:
    break;
  }
}

void
bo_kmq::
sync(bo_kmq::direction dir, size_t size, size_t offset)
{
  amdxdna_drm_sync_bo sbo = {
    .handle = m_bo->m_handle,
    .direction = (dir == shim_xdna::bo::direction::host2device ? SYNC_DIRECT_TO_DEVICE : SYNC_DIRECT_FROM_DEVICE),
    .offset = offset,
    .size = size,
  };
  m_pdev.ioctl(DRM_IOCTL_AMDXDNA_SYNC_BO, &sbo);
}

void
bo_kmq::
bind_at(size_t pos, const buffer_handle* bh, size_t offset, size_t size)
{
  std::lock_guard<std::mutex> lg(m_args_map_lock);

  if (m_type != AMDXDNA_BO_CMD)
    shim_err(EINVAL, "Can't call bind_at() on non-cmd BO");
  m_args_map[pos] = static_cast<const bo*>(bh)->get_drm_bo_handle();
}

uint32_t
bo_kmq::
get_arg_bo_handles(uint32_t *handles, size_t num)
{
  std::lock_guard<std::mutex> lg(m_args_map_lock);

  auto sz = m_args_map.size();
  if (sz > num)
    shim_err(E2BIG, "There are %ld BO args, provided buffer can hold only %ld", sz, num);

  for (auto const& m : m_args_map)
    *(handles++) = m.second;

  return sz;
}

} // namespace shim_xdna
