// clang-format off
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-present NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include <ATen/cuda/CUDAContext.h>
#include <debug.h>
#include <inlining.h>
#include <instrumentation.h>
#include <multidevice/utils.h>
#include <scheduler/cache_policy_refiner.h>
#include <scheduler/debug_utils.h>
#include <scheduler/mark_aliases.h>
#include <scheduler/pointwise.h>
#include <scheduler/reduction_utils.h>
#include <scheduler/registry_utils.h>
#include <scheduler/transpose.h>
#include <scheduler/utils.h>
#include <scheduler/vectorize_helper.h>

namespace nvfuser {

PointWiseScheduler::PointWiseScheduler(
    Fusion* fusion,
    SchedulerRuntimeInfo& runtime_info,
    HeuristicSummary* data_cache)
    : SchedulerEntry(heuristicType()) {
  computeHeuristics(fusion, runtime_info, data_cache);
}

bool PointWiseScheduler::canScheduleCompileTime(Fusion* fusion) {
  if (scheduler_utils::isResharding(fusion)) {
    FUSER_PERF_SCOPE("PointWiseScheduler::canScheduleCompileTime");
    scheduler_debug_utils::canScheduleRejectReason(
        heuristicType(), "Fusion is resharding.");
    return false;
  }

  // Currently using the same path as the scheduler
  // to eliminate mismatch between canSchedule and
  // schedule pointwise.
  if (!hasReferenceTensorView(fusion)) {
    scheduler_debug_utils::canScheduleRejectReason(
        heuristicType(), "cannot find reference tensor");
    return false;
  }

  // Check that inputs of all select/gather-like ops are fusion inputs
  if (registry_utils::rejectScheduleForMemoryPromotion(
          fusion, heuristicType())) {
    return false;
  }

  if (!ir_utils::getViewOps(fusion).empty()) {
    ComputeAtMap ca_map(fusion);
    if (registry_utils::requiresForwardViewReplay(fusion, ca_map)) {
      scheduler_debug_utils::canScheduleRejectReason(
          heuristicType(), "Fusion requires view being reversible.");
      return false;
    }
  }

  if (ir_utils::hasAnyReductionOps(fusion)) {
    scheduler_debug_utils::canScheduleRejectReason(
        heuristicType(), "no support for reduction ops");
    return false;
  }

  if (registry_utils::hasNonUniqueBcast(fusion)) {
    scheduler_debug_utils::canScheduleRejectReason(
        heuristicType(),
        "Broadcasting dimension might be broadcasting to multiple sizes.");
    return false;
  }

  return true;
}

bool PointWiseScheduler::canScheduleRunTime(
    Fusion* fusion,
    SchedulerRuntimeInfo& runtime_info,
    HeuristicSummary* data_cache) {
  FUSER_PERF_SCOPE("PointWiseScheduler::canScheduleRunTime");
  auto can_schedule_transpose_entry =
      HeuristicSummaryEntry<HeuristicCompileTime::CanScheduleTranspose>(
          data_cache, [fusion]() {
            return std::make_unique<bool>(
                TransposeScheduler::canScheduleCompileTime(fusion));
          });
  if (can_schedule_transpose_entry.get()) {
    auto reason =
        getTransposeRuntimeRejectReason(fusion, data_cache, runtime_info);
    return !reason.empty();
  }

  return true;
}

void PointWiseScheduler::schedule(Fusion* fusion) {
  FUSER_PERF_SCOPE("PointWiseScheduler::schedule");
  schedulePointwise(fusion, pointwiseParams());
}

void PointWiseScheduler::computeHeuristics(
    Fusion* fusion,
    SchedulerRuntimeInfo& runtime_info,
    HeuristicSummary* data_cache) {
  FUSER_PERF_SCOPE("PointWiseScheduler::computeHeuristics");
  params_ = getPointwiseHeuristics(fusion, runtime_info, data_cache);
  NVF_ERROR(params_ != nullptr);
}

namespace {
// constexpr int64_t x_grid_limit = ((int64_t)1 << (int64_t)31) - (int64_t)1;
// Unused at the moment, commenting for clang tidy
constexpr int64_t kThreadX = 128;

class DomainMap : public pointwise_utils::DomainMap {
 public:
  using pointwise_utils::DomainMap::DomainMap;

  // The pointwise scheduler heuristics requires a minimum number of axes.
  // The output reference tensor should respect this requirement.
  TensorView* findReferenceTensorView(int64_t minimum_num_axes = 0) const {
    TensorView* result = nullptr;
    int64_t max_dims = -1;
    for (auto output_tv :
         ir_utils::filterByType<TensorView>(fusion_->outputs())) {
      if (isValidReference(output_tv) &&
          hasMinimumSize(output_tv, minimum_num_axes) &&
          !output_tv->isFusionInput()) {
        int64_t n_dims = pointwise_utils::nRootDims(output_tv);
        if (n_dims > max_dims) {
          result = output_tv;
          max_dims = n_dims;
        }
      }
    }
    return result;
  }

 private:
  bool hasMinimumSize(TensorView* tv, int64_t num_axes) const {
    NVF_ERROR(tv != nullptr);
    return (num_axes == 0 || (int64_t)tv->getLogicalDomain().size() > num_axes);
  }
};

} // namespace

std::shared_ptr<PointwiseParams> getPointwiseHeuristics(
    Fusion* fusion,
    const at::ArrayRef<c10::IValue>& runtime_inputs,
    HeuristicSummary* data_cache) {
  SchedulerRuntimeInfo runtime_info(fusion, runtime_inputs);
  return getPointwiseHeuristics(fusion, runtime_info, data_cache);
}

std::shared_ptr<PointwiseParams> getPointwiseHeuristics(
    Fusion* fusion,
    SchedulerRuntimeInfo& runtime_info,
    HeuristicSummary* data_cache) {
  FusionGuard fg(fusion);

  // Incase any buffer is of type DataType::Index
  const auto index_type = runtime_info.getIndexType();

  auto params =
      std::make_shared<PointwiseParams>("Pointwise heuristics", index_type);

  auto in_tvs = ir_utils::filterByType<TensorView>(fusion->inputs());

  auto domain_map_entry =
      HeuristicSummaryEntry<HeuristicCompileTime::DomainMap>(
          data_cache,
          [fusion]() { return std::make_unique<DomainMap>(fusion); });
  const auto& domain_map = dynamic_cast<DomainMap&>(domain_map_entry.get());

  auto largest_out_entry =
      HeuristicSummaryEntry<HeuristicCompileTime::ReferenceTensors>(
          data_cache, [&domain_map]() {
            std::vector<TensorView*> data{domain_map.findReferenceTensorView()};
            return std::make_unique<std::vector<TensorView*>>(std::move(data));
          });
  TensorView* largest_out = largest_out_entry.get()[0];

  NVF_ERROR(largest_out != nullptr);

  const int64_t device_multiprocessor_count =
      (int64_t)at::cuda::getCurrentDeviceProperties()->multiProcessorCount;

  // TODO: Set to 1?
  int64_t max_input_dtype_size = 2;

  for (auto inp : in_tvs) {
    max_input_dtype_size = std::max(
        max_input_dtype_size,
        (int64_t)dataTypeSize(inp->getDataType().value(), index_type));
  }

  auto logical_reorder_map_entry =
      HeuristicSummaryEntry<HeuristicCompileTime::LogicalReorderMap>(
          data_cache, [&fusion, &largest_out]() {
            // NOTE: logical_reorder_map is only applied for fusion without view
            // op yet.
            if (!ir_utils::getViewOps(fusion).empty()) {
              return std::make_unique<std::unordered_map<int64_t, int64_t>>();
            }
            return std::make_unique<std::unordered_map<int64_t, int64_t>>(
                scheduler_utils::maybeLogicalReorderAsAllocationMap(
                    largest_out));
          });
  const std::unordered_map<int64_t, int64_t>& logical_reorder_map =
      logical_reorder_map_entry.get();

  auto ref_root = largest_out->getLogicalDomain();
  // reorder of root to align with logical map should always help with indexing,
  // even when vectorization isn't used.
  if (!logical_reorder_map.empty()) {
    ref_root = TensorDomain::orderedAs(ref_root, logical_reorder_map);
  }
  // We always cacheBefore output at the beginning of the scheduling. And after
  // cacheBefore, the reference tensor will have all reduction IDs removed.
  ref_root = TensorDomain::noDevices(TensorDomain::noReductions(ref_root));

  std::vector<int64_t> elem_counts(ref_root.size(), 1);
  int64_t n_elems = 1;
  for (size_t ref_i = 0; ref_i < ref_root.size(); ref_i++) {
    auto inferred_val =
        runtime_info.expressionEvaluator().evaluate(ref_root[ref_i]->extent());
    NVF_ERROR(
        inferred_val.hasValue(),
        "Error inferring size for pointwise scheduler: ",
        ref_root[ref_i]->extent()->toInlineString());
    elem_counts[ref_i] = inferred_val.as<int64_t>();
    n_elems *= elem_counts[ref_i];
  }

  // If zero dimensional or zero size, return default parameters
  if (TensorDomain::noDevices(
          TensorDomain::noReductions(
              TensorDomain::noBroadcasts(largest_out->getLoopDomain())))
          .empty() ||
      n_elems == 0) {
    auto vectorizable_inputs_outputs_entry = HeuristicSummaryEntry<
        HeuristicCompileTime::VectorizableInputsAndOutputs>(data_cache, []() {
      return std::make_unique<std::vector<TensorView*>>();
    });
    vectorizable_inputs_outputs_entry.get();

    auto broadcast_info = HeuristicSummaryEntry<
        HeuristicCompileTime::BroadcastMultiples>(data_cache, []() {
      return std::make_unique<scheduler_utils::BroadcastMultipleInformation>();
    });
    broadcast_info.get();

    vectorize_helper::getVectorizationFactor(
        runtime_info, largest_out, data_cache, 0);

    // All cache entries that are expected to be generated in the pointwise
    // scheduler by registry.cpp::HeuristicSummary::validate() must be created
    // before hitting this return.
    return std::make_shared<PointwiseParams>(
        "Pointwise heuristics", index_type);
  }

  // Find all vectorizable inputs/outputs
  auto vectorizable_inputs_outputs_entry =
      HeuristicSummaryEntry<HeuristicCompileTime::VectorizableInputsAndOutputs>(
          data_cache, [&largest_out]() {
            return std::make_unique<std::vector<TensorView*>>(
                scheduler_utils::getInputsOutputsWithInnerDim(
                    largest_out, true, true));
          });

  constexpr int64_t kSixteen = 16; // clang tidy

  auto max_unroll_factor = ceilDiv(
      // Available unrolling based on size of data type
      (int64_t)kSixteen / max_input_dtype_size,
      // Reduce max unrolling factor if we have many inputs/outputs to unroll
      // as it could start consuming a lot of registers.
      std::max(
          (scheduler_utils::lastPow2(
               (int64_t)vectorizable_inputs_outputs_entry.get().size()) >>
           2),
          (int64_t)1));

  // Don't unroll at the cost of getting a full wave on the GPU
  if (n_elems < device_multiprocessor_count * kThreadX &&
      max_unroll_factor > 1) {
    max_unroll_factor = std::min(
        max_unroll_factor,
        ceilDiv(n_elems, device_multiprocessor_count * kThreadX));
  }

  // See pointwise.h to understand what we're doing for this 2D analysis.
  // Ideal break point location
  int break_point = 0;

  // If break_point, mark if BIDy and BIDx should be positionally reversed
  // relative to root domains
  bool flip_grid_binding = false;

  // Elements on the right of break point (without break point all are on the
  // right)
  int64_t right_elem_count = 0;

  int64_t bdimx = kThreadX;

  // bdimy may be used if the right side of the break point is not large and we
  // need to expand block level parallelism into the left side of the break
  // point.
  int64_t bdimy = 1;

  // In 2D scheduler gdim_left is used to parallelize the left side of the break
  // point.
  int64_t gdim_left = 1;

  // gdim_right is used if there's too much parallelization in the right side of
  // the break point. We will expand grid parallelization into the right side of
  // the break point with gdim_left and use gdim_right for the left side of the
  // break point.
  int64_t gdim_right = 1;

  auto broadcast_info = HeuristicSummaryEntry<
      HeuristicCompileTime::BroadcastMultiples>(
      data_cache, [&largest_out, &index_type]() {
        return std::make_unique<scheduler_utils::BroadcastMultipleInformation>(
            scheduler_utils::getBroadcastMultiples(largest_out, index_type));
      });

  auto& view_disjoint_sets = broadcast_info.get().view_disjoint_set_ids;
  auto& broadcast_byte_multiples = broadcast_info.get().broadcast_multiples;
  NVF_ERROR(broadcast_byte_multiples.size() == ref_root.size());

  int64_t dtype_sum = 0;
  for (auto inp : ir_utils::filterByType<TensorView>(fusion->inputs())) {
    dtype_sum += (int64_t)dataTypeSize(inp->getDataType().value(), index_type);
  }
  for (auto out : ir_utils::filterByType<TensorView>(fusion->outputs())) {
    dtype_sum += (int64_t)dataTypeSize(out->getDataType().value(), index_type);
  }

  { // Figure out break point position. Empty scope, consider moving to a
    // separate function.
    //
    // How much would this transfer cost if it was done as a 1-D schedule
    int64_t transfer_size_1d = 1;

    for (const auto i : c10::irange(ref_root.size())) {
      transfer_size_1d = transfer_size_1d * elem_counts[i] * dtype_sum;
    }

    // If there isn't very much parallelism available, just use 1D scheduler
    if (n_elems * 2 > device_multiprocessor_count * kThreadX) {
      int64_t min_total_transfer = std::numeric_limits<int64_t>::max();

      // Don't check the inner most dimension, scheduler assumes there's always
      // an rhs
      for (const auto break_point_i : c10::irange((int64_t)ref_root.size())) {
        // If break point is incoherent with view, don't consider breaking here.
        if (!scheduler_utils::breakIsDisjoint(
                view_disjoint_sets, break_point_i)) {
          continue;
        }

        // Number of elements in the right side of reference tv with
        // break_point_i
        int64_t cur_right_elem_count = 1;
        for (const auto right_i : c10::irange(break_point_i, ref_root.size())) {
          cur_right_elem_count = cur_right_elem_count * elem_counts[right_i];
        }

        auto cur_left_elem_count = n_elems / cur_right_elem_count;
        if (cur_left_elem_count <= 1) {
          continue;
        }

        auto lhs_byte_multiple =
            broadcast_byte_multiples[break_point_i].lhs_multiple;
        auto rhs_byte_multiple =
            broadcast_byte_multiples[break_point_i].rhs_multiple;

        // Estimate transfer cost with this break point
        int64_t cur_transfer_size = 1;
        int64_t right_transfer_size = 1;

        for (const auto left_i : c10::irange(break_point_i)) {
          cur_transfer_size =
              cur_transfer_size * elem_counts[left_i] * lhs_byte_multiple;
        }

        for (const auto right_i : c10::irange(break_point_i, ref_root.size())) {
          right_transfer_size =
              right_transfer_size * elem_counts[right_i] * rhs_byte_multiple;
        }
        cur_transfer_size *= right_transfer_size;

        //  Continue if this break point doesn't save at least 10% of 1D
        //  scheduling or isn't better than previous break_points found.
        if (cur_transfer_size >= min_total_transfer ||
            cur_transfer_size * 10 >= transfer_size_1d * 9) {
          continue;
        }

        // Need to be able to parallelize, don't use break if there's not
        // at least an unrolled warp.
        if (ceilDiv(cur_right_elem_count, max_unroll_factor) <=
            at::cuda::getCurrentDeviceProperties()->warpSize) {
          continue;
        }

        // If outer broadcast, or balanced broadcast:
        if (lhs_byte_multiple <= rhs_byte_multiple &&
            // If right transfer size is bigger than half of L2
            at::cuda::getCurrentDeviceProperties()->l2CacheSize <
                right_transfer_size * 2) {
          // flip BIDx and BIDy bindings
          flip_grid_binding = true;
        } else {
          flip_grid_binding = false;
        }
        // Min transfer found, start setting values
        bdimx = std::min(
            ceilDiv(cur_right_elem_count, max_unroll_factor), kThreadX);
        bdimy = 1;
        // Put remainder in bdimy if there's at least a wave of grid level
        // parallelism.
        if (cur_left_elem_count > device_multiprocessor_count) {
          bdimy = kThreadX / bdimx;
        }
        auto remainder_left = ceilDiv(cur_left_elem_count, bdimy);
        auto remainder_right =
            ceilDiv(cur_right_elem_count, bdimx * max_unroll_factor);
        // Use this break point
        break_point = static_cast<int>(break_point_i);
        min_total_transfer = cur_transfer_size;
        right_elem_count = cur_right_elem_count;

        gdim_left = remainder_left;
        gdim_right = remainder_right;
      }
    }
  }

  // Don't try to vectorize if it's not recommended
  params->unroll_factor = 1;

  const auto vectorize_factor = std::min(
      max_unroll_factor,
      vectorize_helper::getVectorizationFactor(
          runtime_info,
          largest_out,
          data_cache,
          break_point,
          logical_reorder_map));

  if (vectorize_factor == 1) {
    params->vectorize = false;
    params->unroll_factor = max_unroll_factor;
  } else {
    params->vectorize = true;
    params->unroll_factor = vectorize_factor;
  }

  NVF_ERROR(right_elem_count > 0 || break_point == 0);
  NVF_ERROR(!(bdimy > 1 && gdim_right > 1));

  params->break_point = break_point;
  params->flip_grid_binding = flip_grid_binding;
  params->split_block = bdimy > 1;

  params->lparams.bind(bdimx, ParallelType::TIDx);
  if (params->split_block) {
    params->lparams.bind(bdimy, ParallelType::TIDy);
  }
  if ((flip_grid_binding && gdim_right > 65535) ||
      (!flip_grid_binding && gdim_left > 65535)) {
    params->split_grid_y_dim = true;
  }

  if (isDebugDumpEnabled(DebugDumpOption::SchedulerDebug)) {
    debug() << "\n===== Pointwise Stats ========\n"
            << "num_elems: " << n_elems << "\n"
            << "elem_counts: " << elem_counts << "\n"
            << "max_input_dtype_size: " << max_input_dtype_size << "\n"
            << "vectorize_factor: " << vectorize_factor << std::endl
            << "\n"
            << "logical_reorder_map: ";
    for (auto [i, j] : logical_reorder_map) {
      debug() << "(" << i << ", " << j << "), ";
    }
    debug() << "\nbroadcast_byte_multiples: ";
    for (auto multiple : broadcast_byte_multiples) {
      debug() << "(" << multiple.lhs_multiple << ", " << multiple.rhs_multiple
              << "), ";
    }
    debug() << "\nLHS elems: "
            << (right_elem_count > 0 ? n_elems / right_elem_count : 0)
            << " RHS elems: " << right_elem_count << std::endl;
    debug() << std::endl;
    debug() << params->toString() << std::endl;
  }

  return params;
}

// TODO: remove or return launch parameters
LaunchParams schedulePointwise(
    Fusion* fusion,
    const at::ArrayRef<c10::IValue>& runtime_inputs) {
  auto params = getPointwiseHeuristics(fusion, runtime_inputs);
  NVF_ERROR(params != nullptr, "Could not schedule pointwise operation.");
  schedulePointwise(fusion, *params);
  return params->lparams;
}

TensorView* getReferenceTensorView(Fusion* fusion) {
  FusionGuard fg(fusion);
  DomainMap domain_map(fusion);
  auto reference_tv = domain_map.findReferenceTensorView();
  return reference_tv;
}

bool hasReferenceTensorView(Fusion* fusion) {
  return getReferenceTensorView(fusion) != nullptr;
}

// TODO: Inline intermediate operations (avoid inlining unrolled/vectorized
// input/output caches)
void schedulePointwise(Fusion* fusion, const PointwiseParams& params) {
  FusionGuard fg(fusion);

  // Make sure we don't have global memory set on intermediate tensors from
  // fusion segmentation
  scheduler_utils::clearMemorySpace(fusion);

  // Cache inputs
  auto cached_inputs = scheduler_utils::cacheInputs(fusion, true);

  // Cache and fork outputs
  auto cached_outputs = scheduler_utils::cacheAndForkOutputs(fusion, true);

  scheduler_utils::prepareForMemoryTypePromotion(fusion);

  refineCachePolicy(fusion);

  std::vector<TensorView*> input_tvs;
  {
    auto filtered_tvs = ir_utils::filterByType<TensorView>(fusion->inputs());
    // Remove hanging tensor views
    for (auto tv : filtered_tvs) {
      if (tv->uses().empty()) {
        continue;
      }
      input_tvs.push_back(tv);
    }
  }
  auto output_tvs = ir_utils::filterByType<TensorView>(fusion->outputs());

  int64_t max_dims = 0;
  for (auto inp : input_tvs) {
    max_dims = std::max(pointwise_utils::nRootDims(inp), max_dims);
  }

  for (auto out : output_tvs) {
    max_dims = std::max(pointwise_utils::nRootDims(out), max_dims);
  }

  // If everything is zero dim tensors, just return.
  if (max_dims == 0) {
    return;
  }

  TensorView* reference_tv = getReferenceTensorView(fusion);

  NVF_ERROR(
      reference_tv != nullptr,
      "Could not find a fully broadcasted output to reference schedule on.");

  scheduler_utils::moveNonConcretizedBroadcastInnermost(fusion, {reference_tv});

  int64_t num_device_dims = numDeviceDims(reference_tv);
  int64_t device_aware_break_point = params.break_point + num_device_dims;

  // Positions of rhs and lhs after merging all dimensions.
  int64_t rhs_i = -1;
  int64_t lhs_i = -1;

  if (!ir_utils::getViewOps(fusion).empty()) {
    ComputeAtMap ca_map(fusion);
    // Propagate reshape transforms through the graph, expecially the reference.
    scheduler_utils::propagateReshapeTransforms(fusion, ca_map);

    // Reorder reference_tv after propagating the view operation. This will
    // reorder for better merging.
    reference_tv->reorder(
        scheduler_utils::domainReorderAsLogicalMap(reference_tv));
    // Reorder so that DeviceDims are in front
    reorderDIDToFront(reference_tv);

    // Break point is relative to logical domain, find the loop domain ID's in
    // the left/right side, we really need the values in domain, but easiest way
    // to do this is with Dependency check which will grab all intermediate
    // values too.
    auto lhs_all_vals = DependencyCheck::getAllValsBetween(
        {reference_tv->getLogicalDomain().begin(),
         reference_tv->getLogicalDomain().begin() + device_aware_break_point},
        {reference_tv->getLoopDomain().begin() + num_device_dims,
         reference_tv->getLoopDomain().end()});

    std::unordered_set<Val*> lhs_all_vals_set(
        lhs_all_vals.begin(), lhs_all_vals.end());

    auto rhs_all_vals = DependencyCheck::getAllValsBetween(
        {reference_tv->getLogicalDomain().begin() + device_aware_break_point,
         reference_tv->getLogicalDomain().end()},
        {reference_tv->getLoopDomain().begin() + num_device_dims,
         reference_tv->getLoopDomain().end()});

    std::unordered_set<Val*> rhs_all_vals_set(
        rhs_all_vals.begin(), rhs_all_vals.end());

    // Make sure lhs and rhs groups are disjoint.
    for (auto lhs_val : lhs_all_vals) {
      NVF_ERROR(
          rhs_all_vals_set.count(lhs_val) == 0,
          "Error in pointwise scheduler. LHS and RHS of the 2D scheduler are not disjoint.");
    }
    NVF_ERROR(
        !rhs_all_vals.empty(),
        "Expecting at least one dimension in the RHS of the pointwise scheduler.");

    // Merge rhs, then lhs.
    IterDomain* rhs_id = nullptr;
    IterDomain* lhs_id = nullptr;
    auto ndims = reference_tv->nDims();
    for (auto i : c10::irange(ndims)) {
      // Merge from right to left
      auto pos = ndims - 1 - i;
      auto id = reference_tv->axis(pos);
      if (lhs_all_vals_set.count(id) > 0) {
        if (lhs_id == nullptr) {
          lhs_id = id;
          lhs_i = pos;
        } else {
          reference_tv->merge(pos, lhs_i);
          lhs_i = pos;
          if (rhs_i > lhs_i) {
            rhs_i--;
          }
        }
      } else if (rhs_all_vals_set.count(id) > 0) {
        if (rhs_id == nullptr) {
          rhs_id = id;
          rhs_i = pos;
        } else {
          reference_tv->merge(pos, rhs_i);
          rhs_i = pos;
          if (lhs_i > rhs_i) {
            lhs_i--;
          }
        }
      }
    }
    // Find the iter domains that should be in the lhs, and rhs.
  } else {
    // Don't need to worry about view transformations, just merge reference tv
    // as we normally would.

    std::unordered_map<int64_t, int64_t> logical_reorder_map =
        scheduler_utils::maybeLogicalReorderAsAllocationMap(reference_tv);
    if (!logical_reorder_map.empty()) {
      reference_tv->reorder(logical_reorder_map);
    }
    reorderDIDToFront(reference_tv);

    // Merge right side of break point
    for (int64_t i = reference_tv->nDims(); i > device_aware_break_point; i--) {
      auto axis_i = i - 1;
      if (rhs_i == -1) {
        rhs_i = axis_i;
      } else {
        reference_tv->merge(axis_i, rhs_i);
        rhs_i = axis_i;
      }
    }
    if (rhs_i >= 0) {
      // If there's an rhs
      reference_tv->reorder({{rhs_i, -1}});
    }

    // Merge left side of break point
    for (int64_t i = device_aware_break_point; i > num_device_dims; i--) {
      auto axis_i = i - 1;
      if (lhs_i == -1) {
        lhs_i = axis_i;
      } else {
        reference_tv->merge(axis_i, lhs_i);
        lhs_i = axis_i;
      }
    }
  }

  int64_t unswitch_pos = 0;
  IterDomain* vectorize_id = nullptr;
  if (params.break_point) {
    // 2D parallelization scheme
    NVF_ERROR(rhs_i >= 0 && lhs_i >= 0);

    // Right (inner merged) dimension is at inner most position, left (outer
    // merged) dimension is at lhs_i. Order as [lhs_i, rhs_i, unmerged...]
    reference_tv->reorder({{lhs_i, 0}, {-1, 1}});

    if (params.vectorize) {
      reference_tv->split(1, params.unroll_factor);
      reference_tv->split(1, NamedScalar::getParallelDim(ParallelType::TIDx));
      reference_tv->split(0, 1);
      // [outer, Unswitch | i-remainder, TIDx, Vectorization]
      reference_tv->axis(1)->parallelize(ParallelType::Unswitch);
      reference_tv->axis(3)->parallelize(ParallelType::TIDx);
      // Vectorization are propagated separately
      vectorize_id = reference_tv->axis(4);

      // [outer, Unswitch | i-remainder, TIDx, Vectorization]
      // To make consistent with unrolling:
      reference_tv->reorder({{1, 2}, {2, 1}, {3, 4}, {4, 3}});
      //[outer | i-remainder, Unswitch, Vectorization, TIDx]
    } else {
      reference_tv->split(1, NamedScalar::getParallelDim(ParallelType::TIDx));
      reference_tv->split(1, params.unroll_factor);

      reference_tv->split(0, 1);
      // [outer, unswitch | i-remainder, unroll, TIDx ]
      reference_tv->reorder({{1, 2}});
      // [outer, i-remainder, unswitch, unroll, TIDx ]
      reference_tv->axis(2)->parallelize(ParallelType::Unswitch);
      // Here we do not set axis(3)->parallelize(Unroll) because we do not want
      // it to be propagated. We manually unroll by splitting the inline
      // propagation process into two steps:
      // step 1: inline at the unswitch position for cached inputs and outputs
      // step 2: inline at the inner most dim for the rest of the graph
      reference_tv->axis(4)->parallelize(ParallelType::TIDx);

      //[outer | i-remainder, Unswitch, Unroll, TIDx]
    }

    // Move out of the way to furthest left point
    reference_tv->reorder({{1, 0}});

    //[i-remainder | outer | Unswitch, Unroll, TIDx]
    if (params.split_block) {
      reference_tv->split(1, NamedScalar::getParallelDim(ParallelType::TIDy));
      if (params.flip_grid_binding) {
        // [BIDy | BIDx, TIDy | Unswitch, Unroll, TIDx]
        reference_tv->axis(1)->parallelize(ParallelType::BIDx);
        reference_tv->axis(2)->parallelize(ParallelType::TIDy);
        if (params.split_grid_y_dim) {
          // [i-remainder, BIDy{65535} | BIDx, TIDy | Unswitch, Unroll, TIDx]
          reference_tv->split(0, 65535);
          reference_tv->axis(1)->parallelize(ParallelType::BIDy);
          unswitch_pos = 5;
        } else {
          reference_tv->axis(0)->parallelize(ParallelType::BIDy);
          unswitch_pos = 4;
        }
      } else {
        // [BIDx | BIDy TIDy | Unswitch, Unroll, TIDx]
        reference_tv->axis(0)->parallelize(ParallelType::BIDx);
        reference_tv->axis(2)->parallelize(ParallelType::TIDy);
        if (params.split_grid_y_dim) {
          // [BIDx | i-remainder, BIDy{65535}, TIDy | Unswitch, Unroll, TIDx]
          reference_tv->split(1, 65535);
          reference_tv->axis(2)->parallelize(ParallelType::BIDy);
          unswitch_pos = 5;
        } else {
          reference_tv->axis(1)->parallelize(ParallelType::BIDy);
          unswitch_pos = 4;
        }
      }
    } else {
      // [BIDy | BIDx | Unswitch, Unroll, TIDx]
      if (params.flip_grid_binding) {
        // [BIDy | BIDx | Unswitch, Unroll, TIDx]
        reference_tv->axis(1)->parallelize(ParallelType::BIDx);
        if (params.split_grid_y_dim) {
          // [i-remainder, BIDy{65535} | BIDx | Unswitch, Unroll, TIDx]
          reference_tv->split(0, 65535);
          reference_tv->axis(1)->parallelize(ParallelType::BIDy);
          unswitch_pos = 4;
        } else {
          reference_tv->axis(0)->parallelize(ParallelType::BIDy);
          unswitch_pos = 3;
        }
      } else {
        // [BIDx | BIDy | Unswitch, Unroll, TIDx]
        reference_tv->axis(0)->parallelize(ParallelType::BIDx);
        if (params.split_grid_y_dim) {
          // [BIDx | i-remainder, BIDy{65535} | Unswitch, Unroll, TIDx]
          reference_tv->split(1, 65535);
          reference_tv->axis(2)->parallelize(ParallelType::BIDy);
          unswitch_pos = 4;
        } else {
          reference_tv->axis(1)->parallelize(ParallelType::BIDy);
          unswitch_pos = 3;
        }
      }
    }
  } else {
    // 1D Scheduler
    NVF_ERROR(rhs_i >= 0 && lhs_i == -1);

    // right hand side exists and is the only axis we care to schedule, move
    // it from the inner most position to left most. Order as [rhs_i,
    // unmerged...]
    reference_tv->reorder({{-1, 0}});

    if (params.vectorize) {
      // Vectorize
      reference_tv->split(0, params.unroll_factor);
      // Unswitch
      reference_tv->split(0, 1);
      // Threads
      reference_tv->split(0, kThreadX);

      reference_tv->axis(0)->parallelize(ParallelType::BIDx);
      reference_tv->axis(1)->parallelize(ParallelType::TIDx);
      reference_tv->axis(2)->parallelize(ParallelType::Unswitch);
      // Vectorization are propagated separately
      vectorize_id = reference_tv->axis(3);

      //[BIDx, TIDx, Unswitch, Vectorization]
      // To make consistent with unrolling:
      reference_tv->reorder({{1, 3}, {2, 1}, {3, 2}});
      //[BIDx, Unswitch, Vectorization, TIDx]
    } else {
      // Threads
      reference_tv->split(0, kThreadX);
      // Unroll
      reference_tv->split(0, params.unroll_factor);
      // Unswitch
      reference_tv->split(0, 1);

      // [BIDx, Unswitch, Unroll, TIDx]
      reference_tv->axis(0)->parallelize(ParallelType::BIDx);
      reference_tv->axis(1)->parallelize(ParallelType::Unswitch);
      // Here we do not set axis(2)->parallelize(Unroll) because we do not want
      // it to be propagated. We manually unroll by splitting the inline
      // propagation process into two steps:
      // step 1: inline at the unswitch position for cached inputs and outputs
      // step 2: inline at the inner most dim for the rest of the graph
      reference_tv->axis(3)->parallelize(ParallelType::TIDx);
    }
    unswitch_pos = 2;
  }

  TransformPropagator propagator(reference_tv);
  MaxLogicalDomainInfoSpanningTree spanning_tree(reference_tv);
  spanning_tree.traverse(&propagator);
  scheduler_utils::parallelizeAllLike(reference_tv);

  if (params.vectorize) {
    // Grab all tensor views that should be vectorized
    auto inputs_outputs =
        scheduler_utils::getInputsOutputsWithInnerDim(reference_tv, true, true);
    std::vector<TensorView*> vectorized_tvs;
    bool should_vectorize_reference_tv = false;
    for (auto tv : inputs_outputs) {
      if (tv == reference_tv) {
        should_vectorize_reference_tv = true;
      }
      if (!tv->isFusionInput()) {
        vectorized_tvs.emplace_back(tv);
        continue;
      }
      // move inputs to consumers of inputs
      auto consumer_tvs = ir_utils::consumerTvsOf(tv);
      vectorized_tvs.insert(
          vectorized_tvs.end(), consumer_tvs.begin(), consumer_tvs.end());
    }
    if (!vectorized_tvs.empty()) {
      // Aggressively mark with vectorized and cleanup later. That way we
      // don't have to manually specify parallelization outside the reference.
      vectorize_id->parallelize(ParallelType::Vectorize);
      scheduler_utils::parallelizeAllLike(
          reference_tv, vectorized_tvs, {ParallelType::Vectorize});
      if (!should_vectorize_reference_tv) {
        vectorize_id->parallelize(ParallelType::Serial);
      }
    }
  }

  // Begin by inlining at the unswitch position for the entire DAG. The cached
  // inputs, and outputs will keep this inline position, but other tensors will
  // get a higher position in later inline propagation. We need this separate
  // step because we were not using ParallelType::Unroll, so we have to do
  // unrolling manually.
  inlineAllAt(reference_tv, unswitch_pos, true);

  auto all_tvs = fusion->allTvs();

  // Inline at the inner most position. The CA position of all tensors except
  // inputs, cached inputs and outputs will be updated.
  std::unordered_set<TensorView*> inner_most_tensors(
      all_tvs.begin(), all_tvs.end());
  for (auto cached_input : cached_inputs) {
    inner_most_tensors.erase(cached_input);
  }
  for (auto entry : cached_outputs) {
    auto output = entry.second;
    inner_most_tensors.erase(output);
  }
  inlineMost(inner_most_tensors);

  scheduler_utils::promoteProducerMemoryTypes(fusion, cached_inputs);

  // TODO(#1401): We could let segmentation split a partially alias-producing
  // fusion into an alias-only segment and the rest. This way, the rest of the
  // fusion (which has fewer expressions) can potentially find a better
  // scheduler and we need to call markAliases only in NoOpScheduler.
  markAliases(fusion);
}

} // namespace nvfuser
