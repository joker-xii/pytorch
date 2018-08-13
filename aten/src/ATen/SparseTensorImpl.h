#pragma once

#include "ATen/Tensor.h"
#include "ATen/TensorImpl.h"
#include "ATen/core/Error.h"

namespace at {
struct AT_API SparseTensorImpl : public TensorImpl {
  // Stored in COO format, indices + values.

  // INVARIANTS:
  // _sparseDims: range [0, len(shape)]; _sparseDims + _denseDims = len(shape)
  // _denseDims : range [0, len(shape)]; _sparseDims + _denseDims = len(shape)
  // _indices.shape: dimensionality: 2,  shape: (_sparseDims, nnz)
  // _values.shape:  dimensionality: 1 + _denseDims.  shape: (nnz, shape[_sparseDims:])

  // The true size of the sparse tensor (e.g., if you called to_dense()
  // on it).  When THTensor merges into TensorImpl, this field
  // should move to the parent class.
  std::vector<int64_t> size_;

  int64_t sparseDims_ = 0; // number of sparse dimensions
  int64_t denseDims_ = 0; // number of dense dimensions

  Tensor indices_; // always a LongTensor
  Tensor values_;

  // A sparse tensor is 'coalesced' if every index occurs at most once in
  // the indices tensor, and the indices are in sorted order.  (This means
  // that it is very easy to convert a coalesced tensor to CSR format: you
  // need only compute CSR format indices.)
  //
  // Most math operations can only be performed on coalesced sparse tensors,
  // because many algorithms proceed by merging two sorted lists (of indices).
  bool coalesced_ = false;

public:
  // Public for now...
  explicit SparseTensorImpl(at::Backend, at::ScalarType);

  int64_t nnz() const { return values_.size(0); }
  int64_t sparseDims() const { return sparseDims_; }
  int64_t denseDims() const { return denseDims_; }
  bool coalesced() const { return coalesced_; }
  Tensor indices() const { return indices_; }
  Tensor values() const { return values_; }

  IntList sizes() const override;
  IntList strides() const override;
  int64_t dim() const override;
  TensorImpl* maybe_zero_dim(bool condition_when_zero_dim) override;
  void * unsafeGetTH(bool retain) override;
  std::unique_ptr<Storage> storage() override;

  // WARNING: This function does NOT preserve invariants of sparseDims/denseDims with
  // respect to indices and values
  void raw_resize_(int64_t sparseDims, int64_t denseDims, IntList size) {
    size_ = size.vec();
    sparseDims_ = sparseDims;
    denseDims_ = denseDims;
  }

  // NOTE: This function preserves invariants of sparseDims/denseDims with respect to
  // indices and values.
  //
  // NOTE: This function supports the following cases:
  // 1. When we only change the shape of dense dimensions (adding/removing dims, or changing
  // the size of some dims) and keeping the shape of sparse dimensions unchanged.
  // 2. When we keep the number of sparse dimensions unchanged, and NOT shrinking the size of
  // any of the sparse dimensions.
  // 3. When the sparse tensor has zero nnz, in which case we are free to change the shapes of
  // both its sparse and dense dimensions.
  //
  // This function DOESN'T support (and will throw an error) the following cases:
  // 1. When we attempt to change the number of sparse dimensions on a non-empty sparse tensor
  // (such an operation will invalidate the indices stored).
  // 2. When we attempt to shrink the size of any of the sparse dimensions (this could make
  // some of the stored indices out-of-bound and thus unsafe).
  void resize_(int64_t sparseDims, int64_t denseDims, IntList size) {
    AT_CHECK(sparseDims + denseDims == size.size(), "number of dimensions must be sparseDims (", sparseDims, ") + denseDims (", denseDims, "), but got ", size.size());
    if (nnz() > 0) {
      bool shrinking_sparse_dims = false;
      auto sparse_size_original = sizes().slice(0, sparseDims);
      auto sparse_size_new = size.slice(0, sparseDims);
      for (int i = 0; i < sparseDims; i++) {
        if (sparse_size_new[i] < sparse_size_original[i]) {
          shrinking_sparse_dims = true;
          break;
        }
      }

      auto alt_options_msg = "You could try the following options:\n\
1. If you need an empty sparse tensor of this size, call `x=torch.sparse_coo_tensor(size)`.\n\
2. If you need to resize this tensor, you have the following options:\n\
    1. Keep the number of sparse dimensions constant and the size of them non-shrinking, and try the same call again.\n\
    2. Or, create a new sparse tensor with this tensor's `values` and the correct indices.";

      AT_CHECK(sparseDims == sparseDims_,
        "changing the number of sparse dimensions (from ", sparseDims_, " to ", sparseDims, ") on a non-empty sparse tensor is not supported.\n", alt_options_msg);

      AT_CHECK(!shrinking_sparse_dims,
        "shrinking the size of sparse dimensions (from ", sparse_size_original, " to ", sparse_size_new, ") on a non-empty sparse tensor is not supported.\n", alt_options_msg);
    }

    if ((!size.equals(size_)) || (sparseDims != sparseDims_) || (denseDims != denseDims_)) {
      std::vector<int64_t> values_size = {values().size(0)};
      auto dense_size = size.slice(sparseDims);
      values_size.insert(values_size.end(), dense_size.begin(), dense_size.end());
      values_.resize_(values_size);

      std::vector<int64_t> indices_size = indices().sizes().vec();
      indices_size[0] = sparseDims;
      indices_.resize_(indices_size);
    }

    size_ = size.vec();
    sparseDims_ = sparseDims;
    denseDims_ = denseDims;
  }

  // NOTE: this function will resize the sparse tensor and also set `indices` and `values` to empty.
  void resize_and_clear_(int64_t sparseDims, int64_t denseDims, IntList size) {
    AT_CHECK(sparseDims + denseDims == size.size(), "number of dimensions must be sparseDims (", sparseDims, ") + denseDims (", denseDims, "), but got ", size.size());

    size_ = size.vec();
    sparseDims_ = sparseDims;
    denseDims_ = denseDims;

    auto empty_indices = indices().type().tensor({sparseDims, 0});
    std::vector<int64_t> values_size = {0};
    auto dense_size = sizes().slice(sparseDims);
    values_size.insert(values_size.end(), dense_size.begin(), dense_size.end());
    auto empty_values = values().type().tensor(values_size);
    set_indices_and_values_unsafe(empty_indices, empty_values);
  }

  void set_coalesced(bool coalesced) { coalesced_ = coalesced; }

  // NOTE: this function is only used internally and not exposed to Python frontend
  void set_nnz_and_narrow(int64_t nnz) {
    indices_ = indices_.narrow(1, 0, nnz);
    values_ = values_.narrow(0, 0, nnz);
  }

  // Takes indices and values and directly puts them into the sparse tensor, no copy.
  // NOTE: this function is unsafe because it doesn't check whether any indices are
  // out of boundaries of `sizes`, so it should ONLY be used where we know that the
  // indices are guaranteed to be within bounds.
  // This used to be called THSTensor_(_move)
  // NB: This used to be able to avoid a refcount bump, but I was too lazy to
  // make it happen
  void set_indices_and_values_unsafe(const Tensor& indices, const Tensor& values);
};

} // namespace at
