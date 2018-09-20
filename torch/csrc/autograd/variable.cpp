#include "torch/csrc/autograd/variable.h"

#include "torch/csrc/autograd/edge.h"
#include "torch/csrc/autograd/engine.h"
#include "torch/csrc/autograd/function.h"
#include "torch/csrc/autograd/functions/accumulate_grad.h"
#include "torch/csrc/autograd/functions/tensor.h"
#include "torch/csrc/autograd/generated/Functions.h"
#include "torch/csrc/autograd/generated/VariableType.h"
#include "torch/csrc/autograd/variable_version.h"

#include <ATen/ATen.h>
#include <ATen/core/Error.h>

#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace torch {
namespace autograd {
Variable::Impl::Impl(Edge gradient_edge)
    : grad_fn_(std::move(gradient_edge.function)),
      is_view_(false),
      output_nr_(gradient_edge.input_nr),
      pyobj_(nullptr) {
  AT_CHECK(
      !grad_fn_ || !requires_grad_,
      "requires_grad should be false if grad_fn is set");
  // if (!data_.defined()) {
  //   throw std::runtime_error("data is undefined");
  // }
}

Variable::Impl::~Impl() = default;

// int64_t Variable::Impl::numel() const {
//   return data_.numel();
// }

// IntList Variable::Impl::sizes() const {
//   return data_.sizes();
// }

// IntList Variable::Impl::strides() const {
//   return data_.strides();
// }

// bool Variable::Impl::is_contiguous() const {
//   AT_ERROR("variable impl does not have is_contiguous");
// }

// int64_t Variable::Impl::dim() const {
//   return data_.dim();
// }

// int64_t Variable::Impl::size(int64_t d) const {
//   return data_.size(d);
// }

// int64_t Variable::Impl::stride(int64_t d) const {
//   return data_.stride(d);
// }

// void Variable::Impl::resize_dim(int64_t ndim) {
//   AT_ERROR("variable impl does not have resize_dim");
// }

// void Variable::Impl::set_size(int64_t dim, int64_t new_size) {
//   AT_ERROR("variable impl does not have set_size");
// }

// void Variable::Impl::set_stride(int64_t dim, int64_t new_stride) {
//   AT_ERROR("variable impl does not have set_stride");
// }

// void Variable::Impl::set_storage_offset(int64_t storage_offset) {
//   AT_ERROR("variable impl does not have set_storage_offset");
// }

// const at::Storage& Variable::Impl::storage() const {
//   return data_.storage();
// }

// int64_t Variable::Impl::storage_offset() const {
//   return data_.storage_offset();
// }

Variable& Variable::set_requires_grad(bool requires_grad) {
  AT_CHECK(
      !requires_grad || at::isFloatingType(type().scalarType()),
      "Only Tensors of floating point dtype can require gradients");
  get_variable_impl()->requires_grad_ = requires_grad;
  return *this;
}

bool Variable::requires_grad() const {
  return get_variable_impl()->requires_grad_ || get_variable_impl()->grad_fn_ || (get_variable_impl()->is_view_ && get_variable_impl()->base().requires_grad());
}

Variable& Variable::grad() {
  return get_variable_impl()->grad_;
}

const Variable& Variable::grad() const {
  return get_variable_impl()->grad_;
}

std::shared_ptr<Function> Variable::grad_accumulator() const {
  if (get_variable_impl()->grad_fn_) {
    throw std::logic_error(
        "grad_accumulator() should be only called on leaf Variables");
  }
  if (!get_variable_impl()->requires_grad_) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(get_variable_impl()->mutex_);

  auto result = get_variable_impl()->grad_accumulator_.lock();
  if (result)
    return result;

  // TODO: old code:
  // c10::raw::intrusive_ptr::incref(this);
  // auto intrusive_from_this = c10::intrusive_ptr<Variable::Impl>::reclaim(this);
  // result = std::make_shared<AccumulateGrad>(Variable(std::move(intrusive_from_this)));

  // NOTE: This increments the refcount for this->tensor_impl_
  result = std::make_shared<AccumulateGrad>(Variable(c10::intrusive_ptr<at::TensorImpl>(this->getIntrusivePtr())));
  get_variable_impl()->grad_accumulator_ = result;
  return result;
}

void Variable::backward(
    at::optional<Tensor> gradient,
    bool keep_graph,
    bool create_graph) const {
  std::vector<Edge> edges;
  edges.emplace_back(grad_fn(), output_nr());

  std::vector<Variable> inputs;
  if (!gradient.has_value()) {
    gradient = make_variable(at::ones_like(at::Tensor(*this)), /*requires_grad=*/false);
  }
  inputs.push_back(std::move(as_variable_ref(*gradient)));
  Engine::get_default_engine().execute(edges, inputs, keep_graph, create_graph);
}

void Variable::set_data(Tensor new_data) {
  // Resets gradient accumulator if metadata is out of date
  std::lock_guard<std::mutex> lock(get_variable_impl()->mutex_);
  auto prior_accumulator = get_variable_impl()->grad_accumulator_.lock();
  if (prior_accumulator) {
    const auto prior_device = prior_accumulator->input_metadata(0).device();
    const auto new_device = new_data.is_cuda() ? new_data.get_device() : -1;

    if (new_data.type() != type() || prior_device != new_device) {
      get_variable_impl()->grad_accumulator_.reset();
    }
  }

  // Updates metadata
  tensor_impl_ = new_data.getIntrusivePtr();
  AT_ASSERT(tensor_impl_->is_variable());
}

void Variable::Impl::release_resources() {
  grad_.reset();
  grad_fn_.reset();
  hooks_.clear();
}

Variable::ViewImpl::ViewImpl(Variable base, Edge gradient_edge)
    : Variable::Impl(std::move(gradient_edge)),
      base_(std::move(base)) {
  AT_CHECK(base_.defined(), "base is undefined");
  if (base_.is_view()) {
    base_ = base_.base();
  }
  is_view_ = true;
  version_counter_ = base_.version_counter();
  attr_version = version_counter_.current_version();
}

const std::shared_ptr<Function>& Variable::grad_fn() const {
  if (!is_view()) {
    return get_variable_impl()->grad_fn_;
  }
  auto view_impl = static_cast<Variable::ViewImpl*>(get_variable_impl());
  std::lock_guard<std::mutex> lock(view_impl->mutex_);
  if (!view_impl->grad_fn_ && !(view_impl->base_.requires_grad())) {
    return view_impl->grad_fn_;
  }
  auto current_version = view_impl->version_counter_.current_version();
  if (view_impl->attr_version != current_version) {
    AT_ASSERT(view_impl->output_nr_ == 0);
    auto fn = std::make_shared<generated::AsStridedBackward>();
    fn->self_geometry = at::TensorGeometry(view_impl->base_);
    fn->size = sizes().vec();
    fn->stride = strides().vec();
    fn->storage_offset = tensor_impl_->storage_offset();
    fn->set_next_edges(collect_next_edges(view_impl->base_));
    fn->add_input_metadata(
      view_impl->base_.type()
    , sizes() // Note: sizes(), not base_.sizes(), is intentional
    , view_impl->base_.is_cuda() ? view_impl->base_.get_device() : -1);
    view_impl->grad_fn_ = std::move(fn);
    view_impl->attr_version = current_version;
  }
  return view_impl->grad_fn_;
}

void Variable::ViewImpl::release_resources() {
  Variable::Impl::release_resources();
  base_.reset();
}

void Variable::rebase_history(Edge gradient_edge) {
  AT_ASSERT(gradient_edge.function != nullptr);
  if (is_view()) {
    auto view_impl = static_cast<Variable::ViewImpl*>(get_variable_impl());
    gradient_edge = std::move(gradient_edge);
    AT_ASSERT(gradient_edge.input_nr == 0);
    AT_ASSERT(gradient_edge.function);
    AT_CHECK(
        gradient_edge.function->num_inputs() == 1,
        "Functions which modify views in-place must return a single Variable");
    view_impl->output_nr_ = gradient_edge.input_nr;
    auto copy_slices = std::make_shared<CopySlices>(
        view_impl->base_, at::TensorGeometry(at::Tensor(*this)), std::move(gradient_edge.function));
    view_impl->base_.set_gradient_edge({std::move(copy_slices), 0});
    grad_fn(); // trigger an update to the view's grad_fn
  } else {
    set_gradient_edge(std::move(gradient_edge));
  }
}

}} // namespace torch::autograd
