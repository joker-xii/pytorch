#pragma once

#include <ATen/core/Backend.h>
#include <c10/core/ScalarType.h>
#include <c10/util/Registry.h>

namespace at {
  class LegacyTypeDispatch;
  struct Type;
}

namespace at {
struct AutogradMetaInterface {
  virtual void set_requires_grad(bool requires_grad) {
    AT_ERROR("set_requires_grad is not implemented for Tensor");
  }
  virtual bool requires_grad() const {
    AT_ERROR("requires_grad is not implemented for Tensor");
  }
  virtual Tensor& grad() {
    AT_ERROR("grad is not implemented for Tensor");
  }
  virtual const Tensor& grad() const {
    AT_ERROR("grad is not implemented for Tensor");
  }
  virtual ~AutogradMetaInterface() {}
};
}

namespace at {
struct CAFFE2_API GradMode {
  static bool is_enabled();
  static void set_enabled(bool enabled);
};
// A RAII, thread local (!) guard that enables or disables grad mode upon
// construction, and sets it back to the original value upon destruction.
struct CAFFE2_API AutoGradMode {
  AutoGradMode(bool enabled) : prev_mode(GradMode::is_enabled()) {
    GradMode::set_enabled(enabled);
  }
  ~AutoGradMode() {
    GradMode::set_enabled(prev_mode);
  }
  bool prev_mode;
};
}

// NB: Registry class not actually in the namespace detail, due to limitations
// of Registry.h
namespace at {

// The VariableHooksInterface is an interface for autograd functionality
// which currently doesn't live in libATen.so AND needs to be called from
// ATen.  In this case, it is only the type registry for Variable types,
// letting us add extra variables types if CUDA types are initialized lazily.
//
// We may choose to absorb autograd into ATen, in which case this interface is obsolete.
//
struct CAFFE2_API VariableHooksInterface {
  // This should never actually be implemented, but it is used to
  // squelch -Werror=non-virtual-dtor
  virtual ~VariableHooksInterface() {}

  virtual Type& getVariableTypeFromBaseType(const at::Type& baseType) const {
    AT_ERROR("cannot getVariableTypeFromBaseType without libtorch");
  }

  virtual void registerVariableTypeFor(LegacyTypeDispatch*, Backend backend, ScalarType scalar_type) const {
    // no-op if Variable not available; it'll get handled (if at all) when
    // libtorch.so gets loaded
  }
};

// NB: dummy argument to suppress "ISO C++11 requires at least one argument
// for the "..." in a variadic macro"
struct CAFFE2_API VariableHooksArgs {};

C10_DECLARE_REGISTRY(
    VariableHooksRegistry,
    VariableHooksInterface,
    VariableHooksArgs);
#define REGISTER_VARIABLE_HOOKS(clsname) \
  C10_REGISTER_CLASS(VariableHooksRegistry, clsname, clsname)

namespace detail {
CAFFE2_API const VariableHooksInterface& getVariableHooks();
}

} // namespace at
