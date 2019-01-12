#include <c10/core/TensorOptions.h>

#include <c10/Device.h>
#include <c10/core/Layout.h>
#include <c10/core/ScalarType.h>
#include <c10/util/Optional.h>

#include <iostream>

namespace c10 {

/// NOTE [ Treating Variables as non-Variables in `is_variable()` ]
///
/// Previously, in VariableType_*.cpp (generated by gen_variable_type.py), when
/// a function is using the 'use_derived' strategy, we call its implementation
/// on the base non-Variable type (`baseType`), passing unwrapped tensors to the
/// call so that any `.type()` calls in the implementation can treat the passed
/// tensors as non-Variables and won't dispatch back to functions in VariableType.
///
/// However, after the Variable/Tensor merge, there is no concept of unwrapping
/// a tensor anymore, and directly passing variables to the base type calls will
/// cause the `.type()` dispatch in the implementation to treat the tensor as a
/// variable, and any function dispatch based on `.type()` will dispatch back to
/// VariableType, which is not what we want.
///
/// The solution to the above problem is to add `at::NonVariableTypeMode`, which
/// when enabled will cause `is_variable()` to always return false, so that
/// `.type()` can return non-Variable type when needed, even if the tensor being
/// called on is a variable.

/// In the CAFFE2_FB_LIMITED_MOBILE_CAPABILITY build setting,
/// thread_local is not supported. In that case, we don't provide
/// `at::NonVariableTypeMode`.
#if !C10_MOBILE && !defined(CAFFE2_FB_LIMITED_MOBILE_CAPABILITY)

thread_local bool NonVariableTypeMode_enabled = false;

bool NonVariableTypeMode::is_enabled() {
  return NonVariableTypeMode_enabled;
}

void NonVariableTypeMode::set_enabled(bool enabled) {
  NonVariableTypeMode_enabled = enabled;
}

#else // C10_MOBILE

bool NonVariableTypeMode::is_enabled() {
  throw std::runtime_error("NonVariableTypeMode is not supported on mobile");
}

void NonVariableTypeMode::set_enabled(bool enabled) {
  throw std::runtime_error("NonVariableTypeMode is not supported on mobile");
}

#endif // C10_MOBILE

std::ostream& operator<<(
    std::ostream& stream,
    const TensorOptions& options) {
  return stream << "TensorOptions(dtype=" << options.dtype()
                << ", device=" << options.device()
                << ", layout=" << options.layout()
                << ", requires_grad=" << std::boolalpha
                << options.requires_grad() << ")";
}

} // namespace c10
