#pragma once
#include <ATen/OpMathType.h>
#include <ATen/native/ForeachUtils.h>
#include "Loops.h"
#include "MemoryAccess.h"
#include "MultiTensorApply.h"

using namespace xpu::dpcpp;
namespace at {
namespace AtenIpexTypeXPU {
namespace {

template <int depth, typename T, typename TL>
bool init_args(
    T** args,
    TL tl,
    int64_t chunk_idx,
    int64_t chunk_size,
    int tensor_loc) {
  bool aligned = true;
  for (int i = 0; i < depth; ++i) {
    args[i] = (T*)tl[tensor_loc].addresses[i];
    args[i] += chunk_idx * chunk_size;
    if (!is_aligned(args[i])) {
      aligned = false;
    }
  }
  return aligned;
}

template <int depth, typename T>
void load_args(
    T r_args[][kILP],
    T** args,
    int64_t i,
    int64_t chunk_size,
    int64_t n,
    int64_t item_idx,
    int64_t item_range) {
#pragma unroll
  for (int ii = 0; ii < kILP; ++ii) {
    int64_t i_start = i + item_idx + ii * item_range;
#pragma unroll
    for (int index = 0; index < depth; ++index) {
      r_args[index][ii] = 0;
      if (i_start < n && i_start < chunk_size) {
        r_args[index][ii] = args[index][i_start];
      }
    }
  }
}

template <typename T>
void store_args(
    T* dst,
    T* src,
    int64_t i_start,
    int64_t chunk_size,
    int64_t n,
    int64_t item_idx,
    int64_t group_range) {
#pragma unroll
  for (int ii = 0; ii < kILP; ++ii) {
    int64_t i = i_start + item_idx + group_range * ii;
    if (i < n && i < chunk_size) {
      dst[i] = src[ii];
    }
  }
}
} // namespace

template <typename T, int depth, int r_args_depth, int res_arg_index>
struct UnaryOpFunctor {
  using opmath_t = at::opmath_type<T>;
  template <typename TLA, typename TLW, typename Op>
  void operator()(
      const int64_t chunk_size,
      TLA tlAddress,
      TLW tlWGMeta,
      DPCPP::nd_item<1> item_id,
      Op op) const {
    auto item_idx = item_id.get_local_id(0);
    auto item_range = item_id.get_local_range(0);
    auto group_idx = item_id.get_group(0);
    int tensor_loc = tlWGMeta[group_idx].wg_to_tensor;
    int chunk_idx = tlWGMeta[group_idx].wg_to_chunk;
    int64_t n = tlAddress[tensor_loc].numel_to_tensor;
    T* args[depth];
    bool all_aligned =
        init_args<depth>(args, tlAddress, chunk_idx, chunk_size, tensor_loc);
    n -= chunk_idx * chunk_size;
    T r_args[r_args_depth][kILP];
    // vec path
    if (n % kILP == 0 && chunk_size % kILP == 0 && all_aligned) {
      for (int64_t i = item_idx; i * kILP < n && i * kILP < chunk_size;
           i += item_range) {
        load_store(r_args[0], args[0], 0, i);
#pragma unroll
        for (int ii = 0; ii < kILP; ++ii) {
          r_args[0][ii] =
              static_cast<T>(op(static_cast<opmath_t>(r_args[0][ii])));
        }
        load_store(args[res_arg_index], r_args[0], i, 0);
      }
      // non-vec path
    } else {
      for (int64_t i = 0; i < n && i < chunk_size; i += item_range * kILP) {
        load_args<r_args_depth>(
            r_args, args, i, chunk_size, n, item_idx, item_range);
#pragma unroll
        for (int ii = 0; ii < kILP; ++ii) {
          r_args[0][ii] =
              static_cast<T>(op(static_cast<opmath_t>(r_args[0][ii])));
        }
        store_args(
            args[res_arg_index],
            r_args[0],
            i,
            chunk_size,
            n,
            item_idx,
            item_range);
      }
    }
  }
};

template <typename T, int depth, int r_args_depth, int res_arg_index>
struct ZeroFunctor {
  using opmath_t = at::opmath_type<T>;
  template <typename TLA, typename TLW>
  void operator()(
      const int64_t chunk_size,
      TLA tlAddress,
      TLW tlWGMeta,
      DPCPP::nd_item<1> item_id) const {
    auto item_idx = item_id.get_local_id(0);
    auto item_range = item_id.get_local_range(0);
    auto group_idx = item_id.get_group(0);
    int tensor_loc = tlWGMeta[group_idx].wg_to_tensor;
    int chunk_idx = tlWGMeta[group_idx].wg_to_chunk;
    int64_t n = tlAddress[tensor_loc].numel_to_tensor;
    T* args[depth];
    bool all_aligned =
        init_args<depth>(args, tlAddress, chunk_idx, chunk_size, tensor_loc);
    n -= chunk_idx * chunk_size;
    T r_args[r_args_depth][kILP];
    // vec path
    if (n % kILP == 0 && chunk_size % kILP == 0 && all_aligned) {
      for (int64_t i = item_idx; i * kILP < n && i * kILP < chunk_size;
           i += item_range) {
#pragma unroll
        for (int ii = 0; ii < kILP; ++ii) {
          r_args[0][ii] = 0;
        }
        load_store(args[res_arg_index], r_args[0], i, 0);
      }
      // non-vec path
    } else {
      for (int64_t i = 0; i < n && i < chunk_size; i += item_range * kILP) {
#pragma unroll
        for (int ii = 0; ii < kILP; ++ii) {
          r_args[0][ii] = 0;
        }
        store_args(
            args[res_arg_index],
            r_args[0],
            i,
            chunk_size,
            n,
            item_idx,
            item_range);
      }
    }
  }
};

} // namespace AtenIpexTypeXPU
} // namespace at
