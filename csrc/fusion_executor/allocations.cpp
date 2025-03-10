// clang-format off
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-present NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include <fusion_executor/allocations.h>

#include <expr_evaluator.h>
#include <fusion_executor/executor_kernel_arg.h>
#include <fusion_executor/executor_utils.h>
#include <instrumentation.h>
#include <polymorphic_value.h>
#include <tensor_metadata.h>

namespace nvfuser {

KernelArgumentHolder inferOutputSizes(
    Fusion* fusion,
    const KernelArgumentHolder& args,
    PrecomputedValues* evaluator_precomputed_values) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::inferOutputSizes");
  ExpressionEvaluator expr_eval;

  std::unique_ptr<PrecomputedValues> evaluator_precomputed_values_up = nullptr;
  if (evaluator_precomputed_values == nullptr) {
    evaluator_precomputed_values_up =
        std::make_unique<PrecomputedValues>(fusion);
    evaluator_precomputed_values_up->bindInputs(args);
    evaluator_precomputed_values_up->evaluate();
    evaluator_precomputed_values = evaluator_precomputed_values_up.get();
  }
  NVF_ERROR(evaluator_precomputed_values != nullptr);
  expr_eval.precomputedValues() = evaluator_precomputed_values;

  auto arg_index_type = args.getSmallestIndexTypeOfArguments();

  KernelArgumentHolder output_tensor_proxies;
  output_tensor_proxies.setDeviceIndex(args.getDeviceIndex());

  for (Val* output : fusion->outputs()) {
    NVF_ERROR(
        output->isA<TensorView>(),
        "Cannot allocate outputs that are not tensors.");
    auto output_tv = output->as<TensorView>();
    const auto& [sizes, strides] = inferShapeOfOutput(output_tv, expr_eval);
    const auto dtype = (output_tv->dtype() == DataType::Index)
        ? data_type_to_aten(arg_index_type)
        : data_type_to_aten(output_tv->dtype());
    output_tensor_proxies.pushTensorProxy(sizes, strides, dtype);
  }
  return output_tensor_proxies;
}

int64_t computeSharedMemory(
    ExpressionEvaluator& expr_eval,
    const std::vector<const kir::Allocate*>& buffers,
    DataType index_type,
    int64_t smem_offset) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::computeSharedMemory");
  int64_t total = smem_offset;
  // align smem_offset at 16 bytes
  smem_offset = (smem_offset + 15) & (~15);
  for (auto smem_alloc : buffers) {
    // If this buffer aliases another buffer,
    // then do not allocate memory for this buffer.
    if (smem_alloc->alias() == nullptr) {
      NVF_ERROR(
          smem_alloc->address(),
          "Smem address is not set for buffer T",
          smem_alloc->buffer()->name());
      const auto address_val = expr_eval.evaluate(smem_alloc->address());
      NVF_ERROR(
          address_val.hasValue(),
          "Failed to evaluate the address ",
          smem_alloc->address()->toInlineString(),
          " of shared memory buffer T",
          smem_alloc->buffer()->name());
      NVF_ERROR(
          address_val.is<int64_t>(),
          "Address val ",
          smem_alloc->address()->toInlineString(),
          " of shared memory buffer T",
          smem_alloc->buffer()->name(),
          " should be int64 but found ",
          address_val);
      const auto size_val = expr_eval.evaluate(smem_alloc->size());
      NVF_ERROR(
          size_val.hasValue(),
          "Failed to evaluate the size ",
          smem_alloc->size(),
          " of shared memory buffer - T",
          smem_alloc->buffer()->name());

      const auto first_byte = smem_offset + address_val.as<int64_t>();
      const auto data_size =
          dataTypeSize(smem_alloc->buffer()->dtype(), index_type);
      const int64_t size_bytes = size_val.as<int64_t>() * data_size;
      const auto last_byte = first_byte + size_bytes;

      total = std::max(total, last_byte);
    }
  }
  return total;
}

namespace {
std::vector<int64_t> getContiguousStrides(
    const std::vector<int64_t>& sizes,
    const std::vector<bool>& expand_flags) {
  NVF_ERROR(sizes.size() == expand_flags.size());

  std::vector<int64_t> strides(sizes.size());
  int64_t cur_stride = 1;
  for (auto i = sizes.size(); i > 0; --i) {
    auto size = sizes.at(i - 1);
    NVF_ERROR(
        size >= 0,
        "Positive size is assumed non-negative but received: ",
        size);

    int64_t stride = cur_stride;

    // If expanded, stride is 0
    if (expand_flags.at(i - 1)) {
      stride = 0;
    } else if (size == 0) {
      // If the size is 0, the stride is 1.
      stride = 1;
    } else {
      cur_stride *= size;
    }

    strides.at(i - 1) = stride;
  }

  return strides;
}

// Infer the size and stride of each dimension
std::pair<std::vector<int64_t>, std::vector<int64_t>> inferShape(
    const TensorView* tv,
    std::vector<Val*> symbolic_sizes,
    std::vector<bool> expand_flags,
    ExpressionEvaluator& expr_eval) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::inferShape");

  // Allocate should be provided for intermediates. We just need to
  // grab a chunk of memory of the size dicatated by
  // Allocate::shape(). Fusion outputs do not come with Allocate and
  // need to be allocated while taking expanded broadcasts into
  // account.

  std::vector<int64_t> concrete_sizes(symbolic_sizes.size(), 0);

  for (const auto i : c10::irange(symbolic_sizes.size())) {
    auto symbolic_size = symbolic_sizes.at(i);
    const auto inferred_val = expr_eval.evaluate(symbolic_size);
    NVF_ERROR(
        inferred_val.hasValue(),
        "Could not launch kernel as program could not infer ",
        symbolic_size->toInlineString(),
        "(",
        symbolic_size->toString(),
        ") for the buffer ",
        tv->toString());

    auto concrete_size = inferred_val.as<int64_t>();
    concrete_sizes.at(i) = concrete_size;
  }

  auto strides = getContiguousStrides(concrete_sizes, expand_flags);

  return {concrete_sizes, strides};
}
} // namespace

std::pair<std::vector<int64_t>, std::vector<int64_t>> inferShapeOfIntermediate(
    const TensorView* tv,
    const kir::Allocate* alloc,
    ExpressionEvaluator& expr_eval) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::inferShapeOfIntermediate");
  // The allocation domain represents the logical allocation domain,
  // bu its actual allocation size may be different, e.g., for
  // supporting halo accesses. The actual size is currently computed
  // when creating the Allocate expr.
  NVF_ERROR(alloc != nullptr);
  const auto& symbolic_sizes = alloc->shape();
  // For intermediate tensors, we just need to allocate a memory chunk
  // of the specified size. Broadcast expansion does not need to be considered.
  const auto expand_flags = std::vector<bool>(symbolic_sizes.size(), false);

  return inferShape(tv, symbolic_sizes, expand_flags, expr_eval);
}

bool fill_allocation_with_nan_ = false;

bool shouldFillAllocationWithNan() {
  return fill_allocation_with_nan_;
}

void setFillAllocationWithNan(bool value) {
  fill_allocation_with_nan_ = value;
}

void fillTensorWithNan(at::Tensor& t) {
  switch (t.scalar_type()) {
    case at::ScalarType::Byte:
      t.fill_(0xFF);
      break;
    case at::ScalarType::Char:
      t.fill_(0x7F);
      break;
    case at::ScalarType::Short:
      t.fill_(0x7FFF);
      break;
    case at::ScalarType::Int:
      t.fill_(0x7FFFFFFF);
      break;
    case at::ScalarType::Long:
      t.fill_(0x7FFFFFFFFFFFFFFFL);
      break;
    case at::ScalarType::Bool:
      t.fill_(true);
      break;
    case at::ScalarType::Half:
    case at::ScalarType::Float:
    case at::ScalarType::Double:
    case at::ScalarType::BFloat16:
    case at::ScalarType::Float8_e4m3fn:
    case at::ScalarType::Float8_e5m2:
      t.fill_(std::nan(""));
      break;
    case at::ScalarType::ComplexHalf:
    case at::ScalarType::ComplexFloat:
    case at::ScalarType::ComplexDouble:
      t.fill_(c10::complex<double>(std::nan(""), std::nan("")));
      break;
    default:
      NVF_ERROR(false, "Unknown dtype");
  }
}

namespace {
// Allocate an `at::Tensor` for `out_info` or compute it as an alias.
at::Tensor allocateOutput(
    const GlobalBufferInfo& out_info,
    const AliasInfo& alias_info,
    const c10::Device& device,
    ExpressionEvaluator& ee) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::allocateOutput");
  // Handle a fusion with duplicated outputs.
  TensorView* out_tv = out_info.tv;
  if (ee.isKnown(out_tv)) {
    return ee.evaluate(out_tv).as<at::Tensor>();
  }

  std::optional<at::Tensor> aliased_io_tensor = std::nullopt;
  Val* aliased_io = alias_info.aliased_io;
  if (aliased_io != nullptr) {
    NVF_ERROR(
        aliased_io->isFusionInput() || aliased_io->isFusionOutput(),
        aliased_io->toInlineString(),
        " is expected to be a fusion input/output. `ee.evaluate` ",
        "an intermediate tensor may involve GPU computation to materialize it ",
        "to global memory.");
    const PolymorphicValue& aliased_io_val = ee.evaluate(aliased_io);
    NVF_ERROR(
        aliased_io_val.is<at::Tensor>(),
        "Alias io only supports tensor. Found ",
        PolymorphicValue_functions::toString(aliased_io_val));
    aliased_io_tensor = aliased_io_val.as<at::Tensor>();
  }

  switch (alias_info.type) {
    case AllocationType::New: {
      auto alloc_tensor = at::native::empty_strided_cuda(
          out_info.sizes,
          out_info.strides,
          out_info.type,
          c10::nullopt,
          device,
          c10::nullopt);
      if (shouldFillAllocationWithNan()) {
        fillTensorWithNan(alloc_tensor);
      }
      return alloc_tensor;
    }
    case AllocationType::ReuseBuffer:
      // Unlike for `AllocationType::Evaluate`, don't use
      // ExpressionEvaluator to compute the output tensor. This is because
      // the output tensor may hold different data from the input, e.g., an
      // updated running mean.  `ExpressionEvaluator::evaluate(out_tv)`
      // would trigger non-trivial host computation.
      return aliased_io_tensor.value();
    case AllocationType::Evaluate: {
      auto out_tensor = ee.evaluate(out_tv).as<at::Tensor>();
      if (aliased_io_tensor.has_value()) {
        NVF_ERROR(
            out_tensor.is_alias_of(aliased_io_tensor.value()),
            "ExpressionEvaluator failed to evaluate ",
            out_tv->toString(),
            " as an alias of ",
            aliased_io->toString());
        inferAndValidateAllocationSizesAndStrides(out_tensor, out_tv, ee);
      }
      return out_tensor;
    }
    default:
      NVF_ERROR(false, "Unrecognized AllocationType.");
  }
}
} // namespace

std::vector<at::Tensor> allocateOutputs(
    const Fusion* fusion,
    const std::vector<GlobalBufferInfo>& output_info,
    const c10::Device& device,
    ExpressionEvaluator& ee) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::allocateOutputs");

  const auto num_outs = output_info.size();

  // Sort the outputs so we compute aliases after allocating non-aliases. The
  // order between aliases can be arbitrary. E.g.,
  //
  // ```
  // non_alias_out = ...
  // alias_out_0 = reshape(non_alias_out, ...)
  // alias_out_1 = reshape(alias_out_0, ...)
  // ```
  //
  // It's fine to compute `alias_out_1` before computing `alias_out_0`: when we
  // compute `alias_out_1`, `alias_out_0` will be recursively
  // `ExpressionEvaluator::evaluate`ed. However, `non_alias_out` must be
  // allocated first so `alias_out_*` can refer them.
  std::vector<std::pair<int64_t, Val*>> sorted_outs;
  sorted_outs.reserve(num_outs);
  for (const auto out_index : c10::irange(num_outs)) {
    sorted_outs.emplace_back(out_index, fusion->outputs()[out_index]);
  }
  std::sort(
      sorted_outs.begin(),
      sorted_outs.end(),
      [fusion](
          const std::pair<int64_t, Val*>& lhs,
          const std::pair<int64_t, Val*>& rhs) {
        return (
            fusion->getOutputAlias(lhs.second).type == AllocationType::New &&
            fusion->getOutputAlias(rhs.second).type != AllocationType::New);
      });

  std::vector<at::Tensor> out_tensors(num_outs);
  for (const auto& [out_index, out] : sorted_outs) {
    at::Tensor out_tensor = allocateOutput(
        output_info[out_index], fusion->getOutputAlias(out), device, ee);
    // Bind `out_tensor` so
    // 1. duplicated outputs map to the same tensor,
    // 2. an output that aliases another output can be evaluated via
    // ExpressionEvaluator cheaply.
    ee.bind(out, out_tensor);
    out_tensors[out_index] = out_tensor;
  }
  return out_tensors;
}

std::vector<at::Tensor> allocOutputSpace(
    const at::ArrayRef<c10::IValue>& inputs,
    Fusion* fusion,
    const c10::Device& device) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::allocOutputSpace");
  auto fusion_inputs = KernelArgumentHolder::createKernelArgumentHolder(inputs);
  auto expr_eval = executor_utils::bindInputs(fusion_inputs, fusion);

  auto output_info =
      getBufferInfos(expr_eval, PrimDataType::Int, fusion->outputs());

  return allocateOutputs(fusion, output_info, device, expr_eval);
}

namespace {
GlobalBufferInfo getBufferInfo(
    ExpressionEvaluator& expr_eval,
    DataType index_dtype,
    TensorView* tv) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::getBufferInfo");
  GlobalBufferInfo info;
  info.tv = tv;
  std::tie(info.sizes, info.strides) = inferShapeOfOutput(info.tv, expr_eval);
  auto dtype =
      (info.tv->dtype() == DataType::Index ? index_dtype : info.tv->dtype());
  info.type = data_type_to_aten(dtype);
  return info;
}

} // namespace
std::vector<GlobalBufferInfo> getBufferInfos(
    ExpressionEvaluator& expr_eval,
    DataType index_dtype,
    const std::vector<Val*>& fusion_outputs) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::getOutbufferInfo");
  std::vector<GlobalBufferInfo> output_buffer_infos;
  output_buffer_infos.reserve(fusion_outputs.size());
  for (const auto out : fusion_outputs) {
    NVF_ERROR(
        out->isA<TensorView>(),
        "Cannot allocate outputs that are not tensors.");

    output_buffer_infos.emplace_back(
        getBufferInfo(expr_eval, index_dtype, out->as<TensorView>()));
  }
  return output_buffer_infos;
}

namespace {

class ForwardTraverseFromAllocToLogical {
  at::Tensor tensor_;
  ExpressionEvaluator& ee_;
  std::list<IterDomain*>& frontier_;

  // Forward traverse split from allocation to logical. Needs to, for example,
  // view tensor with shape [..., 15, ...] as [..., 3, 5, ...]
  void handle(Split* split) {
    auto in = split->in();
    auto inner = split->inner();
    auto outer = split->outer();
    auto factor = ee_.evaluate(split->factor()).as<int64_t>();
    auto in_it = std::find(frontier_.begin(), frontier_.end(), in);
    // NVF_ERROR(in_it != frontier_.end());
    if (in_it == frontier_.end()) {
      // TODO: We should get rid of this return and enable the above assert.
      // Note [Allocation domain on both side of logical]
      // For cases where the allocation domain is on both side of logical, for
      // example, in Tensor3d_To_NHWC4d_FwdBwd_CUDA:
      // [alloc,root]   [alloc,root]           [root]
      //          \     /                      /    |
      //         [logical]                  split   [logical]
      //                                    /  \         |
      //                      [alloc,logical] [logical]  |
      //                                             \   |
      //                                             [alloc]
      // I have no idea why StmtSort::getExprsBetween is not returning the
      // expected set of exprs, but for now, I will just skip these illegal
      // exprs.
      return;
    }
    // view tensor
    int64_t dim = std::distance(frontier_.begin(), in_it);
    std::vector<int64_t> new_shape;
    for (auto i : c10::irange(tensor_.dim())) {
      if (i == dim) {
        new_shape.emplace_back(-1);
        new_shape.emplace_back(factor);
      } else {
        new_shape.emplace_back(tensor_.size(i));
      }
    }
    tensor_ = tensor_.view(new_shape);
    // update frontier
    frontier_.insert(in_it, outer);
    frontier_.insert(in_it, inner);
    frontier_.erase(in_it);
  }

  // Forward traverse split from allocation to logical. Needs to, for example,
  // view tensor with shape [..., 3, 5, ...] as [..., 15, ...]
  void handle(Merge* merge) {
    auto inner = merge->inner();
    auto outer = merge->outer();
    auto out = merge->out();
    auto inner_it = std::find(frontier_.begin(), frontier_.end(), inner);
    auto outer_it = std::find(frontier_.begin(), frontier_.end(), outer);
    // NVF_ERROR(inner_it != frontier_.end());
    // NVF_ERROR(outer_it != frontier_.end());
    if (inner_it == frontier_.end() || outer_it == frontier_.end()) {
      // TODO: see [Allocation domain on both side of logical]
      return;
    }
    int64_t inner_dim = std::distance(frontier_.begin(), inner_it);
    int64_t outer_dim = std::distance(frontier_.begin(), outer_it);
    int64_t left = std::min(inner_dim, outer_dim);
    // view the tensor
    if (outer_dim + 1 != inner_dim) {
      // need to permute the tensor in order to do a merging view
      // before: [..., outer, ..., inner, ...]
      // after: [..., outer, inner, ...]
      std::vector<int64_t> dims;
      int64_t i = 0;
      while (i < tensor_.dim() && i != left) {
        dims.emplace_back(i);
        i++;
      }
      dims.emplace_back(outer_dim);
      dims.emplace_back(inner_dim);
      while (i < tensor_.dim()) {
        if (i != outer_dim && i != inner_dim) {
          dims.emplace_back(i);
        }
        i++;
      }
      tensor_ = tensor_.permute(dims);
    }
    std::vector<int64_t> new_shape;
    for (auto i : c10::irange(tensor_.dim())) {
      if (i == left) {
        new_shape.emplace_back(-1);
      } else if (i != left + 1) {
        new_shape.emplace_back(tensor_.size(i));
      }
    }
    tensor_ = tensor_.view(new_shape);
    // update frontier
    if (inner_dim < outer_dim) {
      *inner_it = out;
      frontier_.erase(outer_it);
    } else {
      *outer_it = out;
      frontier_.erase(inner_it);
    }
  }

  void handle(Expr* expr) {
    if (auto split = dynamic_cast<Split*>(expr)) {
      handle(split);
    } else if (auto merge = dynamic_cast<Merge*>(expr)) {
      handle(merge);
    } else {
      NVF_ERROR(false, "Unsupported transormation in allocation domain");
    }
  }

 public:
  ForwardTraverseFromAllocToLogical(
      at::Tensor tensor,
      ExpressionEvaluator& ee,
      std::list<IterDomain*>& frontier)
      : tensor_(std::move(tensor)), ee_(ee), frontier_(frontier) {}

  at::Tensor run(
      const std::vector<IterDomain*>& logical,
      const std::vector<IterDomain*>& alloc) {
    auto forward_exprs = StmtSort::getExprsBetween(
        {alloc.begin(), alloc.end()}, {logical.begin(), logical.end()});
    for (auto expr : forward_exprs) {
      handle(expr);
    }
    return tensor_;
  }
};

// Backward traverse is similar to forward traverse, but we need to do opposite
// transformations.
class BackwardTraverseFromAllocToLogical {
  at::Tensor tensor_;
  ExpressionEvaluator& ee_;
  std::list<IterDomain*>& frontier_;

  // Backward traverse split from allocation to logical. Needs to, for example,
  // view tensor with shape [..., 3, 5, ...] as [..., 15, ...]
  void handle(Split* split) {
    auto inner = split->inner();
    auto outer = split->outer();
    auto in = split->in();
    auto inner_it = std::find(frontier_.begin(), frontier_.end(), inner);
    auto outer_it = std::find(frontier_.begin(), frontier_.end(), outer);
    // NVF_ERROR(inner_it != frontier_.end());
    // NVF_ERROR(outer_it != frontier_.end());
    if (inner_it == frontier_.end() || outer_it == frontier_.end()) {
      // TODO: see [Allocation domain on both side of logical]
      return;
    }
    int64_t inner_dim = std::distance(frontier_.begin(), inner_it);
    int64_t outer_dim = std::distance(frontier_.begin(), outer_it);
    int64_t left = std::min(inner_dim, outer_dim);
    // view the tensor
    if (outer_dim + 1 != inner_dim) {
      // need to permute the tensor in order to do a merging view
      // before: [..., outer, ..., inner, ...]
      // after: [..., outer, inner, ...]
      std::vector<int64_t> dims;
      int64_t i = 0;
      while (i < tensor_.dim() && i != left) {
        dims.emplace_back(i);
        i++;
      }
      dims.emplace_back(outer_dim);
      dims.emplace_back(inner_dim);
      while (i < tensor_.dim()) {
        if (i != outer_dim && i != inner_dim) {
          dims.emplace_back(i);
        }
        i++;
      }
      tensor_ = tensor_.permute(dims);
    }
    std::vector<int64_t> new_shape;
    for (auto i : c10::irange(tensor_.dim())) {
      if (i == left) {
        new_shape.emplace_back(-1);
      } else if (i != left + 1) {
        new_shape.emplace_back(tensor_.size(i));
      }
    }
    tensor_ = tensor_.view(new_shape);
    // update frontier
    if (inner_dim < outer_dim) {
      *inner_it = in;
      frontier_.erase(outer_it);
    } else {
      *outer_it = in;
      frontier_.erase(inner_it);
    }
  }

  // Backward traverse split from allocation to logical. Needs to, for example,
  // view tensor with shape [..., 15, ...] as [..., 3, 5, ...]
  void handle(Merge* merge) {
    auto out = merge->out();
    auto inner = merge->inner();
    auto outer = merge->outer();
    auto factor = ee_.evaluate(inner->extent()).as<int64_t>();
    auto out_it = std::find(frontier_.begin(), frontier_.end(), out);
    // NVF_ERROR(out_it != frontier_.end());
    if (out_it == frontier_.end()) {
      // TODO: see [Allocation domain on both side of logical]
      return;
    }
    // view tensor
    int64_t dim = std::distance(frontier_.begin(), out_it);
    std::vector<int64_t> new_shape;
    for (auto i : c10::irange(tensor_.dim())) {
      if (i == dim) {
        new_shape.emplace_back(-1);
        new_shape.emplace_back(factor);
      } else {
        new_shape.emplace_back(tensor_.size(i));
      }
    }
    tensor_ = tensor_.view(new_shape);
    // update frontier
    frontier_.insert(out_it, outer);
    frontier_.insert(out_it, inner);
    frontier_.erase(out_it);
  }

  void handle(Expr* expr) {
    if (auto split = dynamic_cast<Split*>(expr)) {
      handle(split);
    } else if (auto merge = dynamic_cast<Merge*>(expr)) {
      handle(merge);
    } else {
      NVF_ERROR(false, "Unsupported transormation in allocation domain");
    }
  }

 public:
  BackwardTraverseFromAllocToLogical(
      at::Tensor tensor,
      ExpressionEvaluator& ee,
      std::list<IterDomain*>& frontier)
      : tensor_(std::move(tensor)), ee_(ee), frontier_(frontier) {}

  at::Tensor run(
      const std::vector<IterDomain*>& logical,
      const std::vector<IterDomain*>& alloc) {
    auto backward_exprs = StmtSort::getExprsBetween(
        {logical.begin(), logical.end()}, {alloc.begin(), alloc.end()});
    std::reverse(backward_exprs.begin(), backward_exprs.end());
    for (auto expr : backward_exprs) {
      handle(expr);
    }
    return tensor_;
  }
};

// Start from a tensor whose dimensions are consistent with the allocation
// domain of tv, apply a sequence of view/permute to the tensor to transform it
// into a format whose dimensions are consistent with the logical domain of tv.
// For example, if the logical domain is [I1, I2], and the allocation domain is
// [I2*I1], then we will allocate as [I2*I1], then do a tensor.view(I2, I1).t()
// to get a tensor whose semantics is [I1, I2] but its memory is [I2*I1].
// Another example, if the logical domain is [I1*I2] and the allocation domain
// is [I1, I2], then we will allocate as [I1, I2] and do a tensor.view(I1*I2) to
// get a tensor whose semantics is [I1*I2] but memory is [I1,I2]
at::Tensor transformOutputFromAllocationToLogical(
    at::Tensor tensor,
    TensorView* tv,
    ExpressionEvaluator& ee) {
  FUSER_PERF_SCOPE(
      "fusion_executor::allocations::transformOutputFromAllocationToLogical");
  // Ignore reductions because reductions does not exist in tensor's definition
  auto logical = TensorDomain::noReductions(tv->getLogicalDomain());
  auto alloc = TensorDomain::noReductions(tv->getMaybeAllocationDomain());
  // Traverse all affine transformations from allocation domain. Because
  // allocation domain can be before or after the logical domain, we need both a
  // forward and a backward traverse.
  std::list<IterDomain*> frontier(alloc.begin(), alloc.end());
  NVF_ERROR(tensor.dim() == (int64_t)frontier.size());
  tensor = ForwardTraverseFromAllocToLogical(tensor, ee, frontier)
               .run(logical, alloc);
  tensor = BackwardTraverseFromAllocToLogical(tensor, ee, frontier)
               .run(logical, alloc);
  NVF_ERROR(frontier.size() == logical.size());
  // Now that all affine transformations are handled, and frontiers should
  // contain the same set of IDs as logical. We still need to do a final
  // permutation so that their orders are also consistent.
  std::unordered_map<IterDomain*, int64_t> current_dims;
  int64_t counter = 0;
  for (auto id : frontier) {
    current_dims[id] = counter++;
  }
  std::vector<int64_t> dims;
  dims.reserve(frontier.size());
  for (auto id : logical) {
    dims.emplace_back(current_dims.at(id));
  }
  return tensor.permute(dims);
}

} // namespace

std::pair<std::vector<int64_t>, std::vector<int64_t>> inferShapeOfOutput(
    TensorView* tv,
    ExpressionEvaluator& expr_eval) {
  FUSER_PERF_SCOPE("fusion_executor::allocations::inferShapeOfOutput");
  // Fusion outputs do not come with Allocate and
  // need to be allocated while taking expanded broadcasts into
  // account.

  std::vector<Val*> symbolic_sizes;
  std::vector<bool> expand_flags;

  // Allocate the allocation domain
  for (const auto id : tv->getMaybeAllocationDomain()) {
    if (id->isReduction() || id->isStride()) {
      continue;
    }

    if (id->isDeviceDim()) {
      symbolic_sizes.push_back(id->container()->oneVal());
    } else {
      symbolic_sizes.push_back(id->getMaybeExpandedExtent());
    }
    if (id->hasExpandedExtent()) {
      NVF_ERROR(
          id->isBroadcast(),
          "Non-broadcast domain should not have an expanded extent: ",
          id->toString());
      expand_flags.push_back(true);
    } else {
      expand_flags.push_back(false);
    }
  }

  auto size_stride = inferShape(tv, symbolic_sizes, expand_flags, expr_eval);
  if (!tv->hasAllocation()) {
    return size_stride;
  }
  auto options =
      c10::TensorOptions().device(c10::Device(c10::DeviceType::Meta));
  auto meta_tensor =
      at::empty_strided(size_stride.first, size_stride.second, options);
  // TODO(jiej): we should refactor it here, there's no need to use
  // meta_tensor at all, size + stride should be used directly in the
  // `transformOutputFromAllocationToLogical`
  meta_tensor =
      transformOutputFromAllocationToLogical(meta_tensor, tv, expr_eval);
  return {meta_tensor.sizes().vec(), meta_tensor.strides().vec()};
}

} // namespace nvfuser
