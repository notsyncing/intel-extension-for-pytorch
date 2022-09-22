#include <ATen/Dispatch.h>
#include <ATen/native/BinaryOps.h>
#include <ATen/native/Fill.h>
#include <ATen/native/ForeachUtils.h>
#include <ATen/native/TensorIterator.h>

#include <aten/core/detail/IndexUtils.h>
#include <runtime/Utils.h>

#include "ATen/OpMathType.h"
#include "comm/ATDispatch.h"
#include "comm/ApplyUtils.h"
#include "comm/RegistrationDeclarations.h"

#include "ForeachFunctors.h"
#include "Loops.h"
#include "MultiTensorApply.h"
#include "comm/Numerics.h"

namespace at {
namespace AtenIpexTypeXPU {

template <template <class> class Op>
std::vector<Tensor> foreach_pointwise_op(
    TensorList input,
    TensorList tensors1,
    TensorList tensors2,
    const Scalar& scalar) {
  std::vector<std::vector<at::Tensor>> tensor_lists;
  std::vector<at::Tensor> vec_res;
  vec_res.reserve(input.size());
  for (const auto& t : input) {
    vec_res.emplace_back(at::empty_like(t));
  }

  tensor_lists.emplace_back(input.vec());
  tensor_lists.emplace_back(tensors1.vec());
  tensor_lists.emplace_back(tensors2.vec());
  tensor_lists.emplace_back(std::move(vec_res));

  IPEX_DISPATCH_ALL_TYPES_AND_COMPLEX_AND2(
      ScalarType::Half,
      ScalarType::BFloat16,
      input[0].scalar_type(),
      "foreach_pointwise_op_xpu",
      [&]() {
        using opmath_t = at::opmath_type<scalar_t>;
        multi_tensor_apply<4>(
            tensor_lists,
            at::AtenIpexTypeXPU::PointWiseOpScalarFunctor<scalar_t, 4, 3, 3>(),
            Op<opmath_t>(),
            scalar.to<opmath_t>());
      });

  return tensor_lists[3];
}

template <template <class> class Op>
void foreach_pointwise_op_(
    TensorList input,
    TensorList tensors1,
    TensorList tensors2,
    const Scalar& scalar) {
  std::vector<std::vector<at::Tensor>> tensor_lists;
  tensor_lists.emplace_back(input.vec());
  tensor_lists.emplace_back(tensors1.vec());
  tensor_lists.emplace_back(tensors2.vec());

  IPEX_DISPATCH_ALL_TYPES_AND_COMPLEX_AND2(
      ScalarType::Half,
      ScalarType::BFloat16,
      input[0].scalar_type(),
      "foreach_pointwise_op__xpu",
      [&]() {
        using opmath_t = at::opmath_type<scalar_t>;
        multi_tensor_apply<3>(
            tensor_lists,
            at::AtenIpexTypeXPU::PointWiseOpScalarFunctor<scalar_t, 3, 3, 0>(),
            Op<opmath_t>(),
            scalar.to<opmath_t>());
      });
}

#define FOREACH_POINTWISE_OP_SCALAR(NAME, OP)                              \
  std::vector<Tensor> _foreach_##NAME(                                     \
      TensorList input,                                                    \
      TensorList tensors1,                                                 \
      TensorList tensors2,                                                 \
      const Scalar& scalar) {                                              \
    at::native::check_foreach_api_restrictions(input, tensors1, tensors2); \
                                                                           \
    if (!at::native::can_use_fast_route(                                   \
            {input, tensors1, tensors2}, scalar) ||                        \
        at::native::has_integral_tensor(input, /* includeBool */ true)) {  \
      return at::native::foreach_tensor_##NAME##_scalar_slow(              \
          input, tensors1, tensors2, scalar);                              \
    }                                                                      \
                                                                           \
    return foreach_pointwise_op<OP>(input, tensors1, tensors2, scalar);    \
  }                                                                        \
                                                                           \
  void _foreach_##NAME##_(                                                 \
      TensorList input,                                                    \
      TensorList tensors1,                                                 \
      TensorList tensors2,                                                 \
      const Scalar& scalar) {                                              \
    at::native::check_foreach_api_restrictions(input, tensors1, tensors2); \
                                                                           \
    if (!at::native::can_use_fast_route(                                   \
            {input, tensors1, tensors2}, scalar) ||                        \
        at::native::has_integral_tensor(input, /* includeBool */ true)) {  \
      return at::native::foreach_tensor_##NAME##_scalar_slow_(             \
          input, tensors1, tensors2, scalar);                              \
    }                                                                      \
                                                                           \
    foreach_pointwise_op_<OP>(input, tensors1, tensors2, scalar);          \
  }

FOREACH_POINTWISE_OP_SCALAR(addcmul, std::multiplies);
FOREACH_POINTWISE_OP_SCALAR(addcdiv, std::divides);

template <template <class> class Op>
std::vector<Tensor> foreach_binary_op(
    TensorList tensors,
    const Scalar& scalar) {
  std::vector<std::vector<at::Tensor>> tensor_lists;
  std::vector<at::Tensor> vec_res;
  vec_res.reserve(tensors.size());
  for (const auto& t : tensors) {
    vec_res.emplace_back(at::native::empty_like(t));
  }

  tensor_lists.emplace_back(tensors.vec());
  tensor_lists.emplace_back(std::move(vec_res));

  IPEX_DISPATCH_ALL_TYPES_AND_COMPLEX_AND3(
      kBool,
      kBFloat16,
      kHalf,
      tensors[0].scalar_type(),
      "foreach_binary_op_scalar_dpcpp",
      [&]() {
        using opmath_t = at::opmath_type<scalar_t>;
        multi_tensor_apply<2>(
            tensor_lists,
            BinaryOpScalarFunctor<
                scalar_t,
                /* depth */ 2,
                /* r_args_depth */ 1,
                /* res_arg_index */ 1>(),
            Op<opmath_t>(),
            scalar.to<opmath_t>());
      });
  return tensor_lists[1];
}

template <template <class> class Op>
void foreach_binary_op_(TensorList tensors, const Scalar& scalar) {
  std::vector<std::vector<at::Tensor>> tensor_lists;
  tensor_lists.emplace_back(tensors.vec());

  IPEX_DISPATCH_ALL_TYPES_AND_COMPLEX_AND3(
      kBool,
      kBFloat16,
      kHalf,
      tensors[0].scalar_type(),
      "foreach_binary_op_scalar_dpcpp_",
      [&]() {
        using opmath_t = at::opmath_type<scalar_t>;
        multi_tensor_apply<1>(
            tensor_lists,
            BinaryOpScalarFunctor<
                scalar_t,
                /* depth */ 1,
                /* r_args_depth */ 1,
                /* res_arg_index */ 0>(),
            Op<opmath_t>(),
            scalar.to<opmath_t>());
      });
}

#define FOREACH_BINARY_OP_SCALAR(NAME, OP, DIVISION_OP)                  \
  void _foreach_##NAME##_(TensorList tensors, const Scalar& scalar) {    \
    at::native::check_foreach_api_restrictions(tensors);                 \
    if (!at::native::can_use_fast_route(tensors, scalar, DIVISION_OP)) { \
      return at::native::foreach_tensor_##NAME##_scalar_kernel_slow_(    \
          tensors, scalar);                                              \
    }                                                                    \
                                                                         \
    foreach_binary_op_<OP>(tensors, scalar);                             \
  }                                                                      \
                                                                         \
  std::vector<Tensor> _foreach_##NAME(                                   \
      TensorList tensors, const at::Scalar& scalar) {                    \
    at::native::check_foreach_api_restrictions(tensors);                 \
    if (!at::native::can_use_fast_route(tensors, scalar, DIVISION_OP)) { \
      return at::native::foreach_tensor_##NAME##_scalar_kernel_slow(     \
          tensors, scalar);                                              \
    }                                                                    \
                                                                         \
    return foreach_binary_op<OP>(tensors, scalar);                       \
  }

FOREACH_BINARY_OP_SCALAR(add, std::plus, false);
FOREACH_BINARY_OP_SCALAR(mul, std::multiplies, false);

// In the case of division, integer inputs will result in float.
// Currently multi tensor apply can only return result of the same type as
// input.
FOREACH_BINARY_OP_SCALAR(div, std::divides, true);

// In the case of subtraction, we dont allow scalar to be boolean following the
// torch.sub logic
void _foreach_sub_(TensorList tensors, const Scalar& scalar) {
  at::native::check_foreach_api_restrictions(tensors);
  at::native::sub_check(tensors[0], scalar);

  if (!at::native::can_use_fast_route(tensors, scalar)) {
    return at::native::foreach_tensor_sub_scalar_kernel_slow_(tensors, scalar);
  }

  foreach_binary_op_<std::minus>(tensors, scalar);
}

std::vector<Tensor> _foreach_sub(TensorList tensors, const Scalar& scalar) {
  at::native::check_foreach_api_restrictions(tensors);
  at::native::sub_check(tensors[0], scalar);

  if (!at::native::can_use_fast_route(tensors, scalar)) {
    return at::native::foreach_tensor_sub_scalar_kernel_slow(tensors, scalar);
  }

  return foreach_binary_op<std::minus>(tensors, scalar);
}

} // namespace AtenIpexTypeXPU
} // namespace at