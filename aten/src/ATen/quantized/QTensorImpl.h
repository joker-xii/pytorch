#pragma once

#include <ATen/quantized/Quantizer.h>
#include <c10/core/TensorImpl.h>
#include <c10/util/Exception.h>

namespace at {

/**
 * QTensorImpl is a TensorImpl for Quantized Tensors, it stores Quantizer which
 * specifies the quantization scheme and parameters, for more information please
 * see ATen/quantized/Quantizer.h
 *
 * We'll use QTensor in code or documentation to refer to a Tensor with QTensorImpl.
 */
struct CAFFE2_API QTensorImpl : public c10::TensorImpl {
 public:
  QTensorImpl(
      Storage&& storage,
      TensorTypeId type_id,
      QuantizerPtr quantizer);

  // TODO: Expose in PyTorch Frontend
  QuantizerPtr quantizer() {
    return quantizer_;
  }

  /**
   * Return a TensorImpl that is a shallow-copy of this TensorImpl.
   *
   * For usage of `version_counter` and `allow_tensor_metadata_change`,
   * see NOTE [ TensorImpl Shallow-Copying ].
   */
  c10::intrusive_ptr<TensorImpl> shallow_copy_and_detach(
      const c10::VariableVersion& version_counter,
      bool allow_tensor_metadata_change) const override {
    auto impl = c10::make_intrusive<QTensorImpl>(
        Storage(storage()), type_id(), quantizer_);
    copy_tensor_data(
      /*src_impl=*/this,
      /*dest_impl=*/impl.get(),
      /*version_counter=*/version_counter,
      /*allow_tensor_metadata_change=*/allow_tensor_metadata_change);
    impl->refresh_numel();
    impl->refresh_contiguous();
    return impl;
  }

  /**
   * Shallow-copies data from another TensorImpl into this TensorImpl.
   */
  void shallow_copy_from(c10::intrusive_ptr<TensorImpl> impl) override {
    copy_tensor_data(
      /*src_impl=*/impl.get(),
      /*dest_impl=*/this,
      /*version_counter=*/version_counter(),
      /*allow_tensor_metadata_change=*/allow_tensor_metadata_change());
    refresh_numel();
    refresh_contiguous();
  }

 private:
  QuantizerPtr quantizer_;

  /**
   * Copy the storage pointer and the tensor metadata fields (e.g. sizes / strides / storage_offset)
   * from one TensorImpl to another TensorImpl.
   *
   * For usage of `version_counter` and `allow_tensor_metadata_change`, see NOTE [ TensorImpl Shallow-Copying ].
   */
  static void copy_tensor_data(
      const QTensorImpl* src_q_impl,
      QTensorImpl* dest_q_impl,
      const c10::VariableVersion& version_counter,
      bool allow_tensor_metadata_change) {
    TensorImpl::copy_tensor_data(src_q_impl, dest_q_impl, version_counter, allow_tensor_metadata_change);

    // OpaqueTensorImpl-specific fields.
    dest_q_impl->quantizer_ = src_q_impl->quantizer_;
  }
};

} // namespace at
