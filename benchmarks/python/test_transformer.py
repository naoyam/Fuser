# SPDX-FileCopyrightText: Copyright (c) 2024-present NVIDIA CORPORATION & AFFILIATES.
# All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
This file micro-benchmarks the forward pass and the backprop of a Transformer
block used in GPT-3. The nvFusions are dumped from Thunder. To regenerate
the nvFusions and the inputs, run the following:

1. `git clone https://github.com/Lightning-AI/lightning-thunder.git`

2. `git fetch origin wjy/sharded`

3. `git checkout wjy/sharded`
This branch adds the GPT-3 block benchmark and turns on certain knobs so the
entire Transformer block fits into one nvFusion.

4. Apply the following patch
```
diff --git a/nvfuser/__init__.py b/nvfuser/__init__.py
index 8be5df3d..69bcf450 100644
--- a/nvfuser/__init__.py
+++ b/nvfuser/__init__.py
@@ -214,8 +214,8 @@ class FusionDefinition(_C._FusionDefinition):
                 capture_debug_output=capture_debug_output,
                 profile=profile,
             )
+            print(self.getReproErrorString("executing", inputs))
         except Exception as err:
-            logger.exception(self.getReproErrorString("executing", inputs))
             raise

         return result
```

5. `pytest thunder/benchmarks/targets.py -k 'test_nanogpt_block[backward-thunder]' -s`
In stdout, you'll find the forward nvFusion executed once followed by the
backward nvFusion executed many times.
"""

from nvfuser import FusionDefinition, DataType
from .core import run_benchmark
from nvfuser.pytorch_utils import clear_cuda_cache
import torch


def transformer_forward_fusion(fd: FusionDefinition) -> None:
    # x: input
    T0 = fd.define_tensor(
        shape=[1, -1, -1],
        contiguity=[None, True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # layer_norm0.weight
    T1 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # layer_norm0.bias
    T2 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA linear0.weight
    T3 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MHA linear0.bias
    T4 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA linear1.weight
    T5 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MHA linear1.bias
    T6 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA dropout.rng_offset
    S7 = fd.define_scalar(None, dtype=DataType.Int)
    # MHA dropout.rng_seed
    S8 = fd.define_scalar(None, dtype=DataType.Int)
    # layer_norm1.weight
    T9 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # layer_norm1.bias
    T10 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MLP linear0.weight
    T11 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MLP linear0.bias
    T12 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MLP linear1.weight
    T13 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MLP linear1.bias
    T14 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MLP dropout.rng_offset
    S15 = fd.define_scalar(None, dtype=DataType.Int)
    # MLP dropout.rng_seed
    S16 = fd.define_scalar(None, dtype=DataType.Int)

    T17 = fd.ops.cast(T0, dtype=DataType.Float)
    T18, T19 = fd.ops.var_mean(T17, dims=[2], correction=0, keepdim=False)
    S20 = fd.define_scalar(1, dtype=DataType.Int)
    S21 = fd.define_scalar(2048, dtype=DataType.Int)
    S22 = fd.define_scalar(1, dtype=DataType.Int)
    V23 = fd.define_vector([S20, S21, S22], dtype=DataType.Int)
    T24 = fd.ops.broadcast_in_dim(T18, shape=V23, broadcast_dims=[0, 1])
    S25 = fd.define_scalar(1, dtype=DataType.Int)
    S26 = fd.define_scalar(2048, dtype=DataType.Int)
    S27 = fd.define_scalar(1, dtype=DataType.Int)
    V28 = fd.define_vector([S25, S26, S27], dtype=DataType.Int)
    T29 = fd.ops.broadcast_in_dim(T19, shape=V28, broadcast_dims=[0, 1])
    S30 = fd.define_scalar(1.00000e-05, dtype=DataType.Double)
    T31 = fd.ops.add(T24, S30)
    T32 = fd.ops.rsqrt(T31)
    S33 = fd.define_scalar(1, dtype=DataType.Int)
    S34 = fd.define_scalar(2048, dtype=DataType.Int)
    S35 = fd.define_scalar(12288, dtype=DataType.Int)
    V36 = fd.define_vector([S33, S34, S35], dtype=DataType.Int)
    T37 = fd.ops.broadcast_in_dim(T29, shape=V36, broadcast_dims=[0, 1, 2])
    T38 = fd.ops.sub(T17, T37)
    S39 = fd.define_scalar(1, dtype=DataType.Int)
    S40 = fd.define_scalar(2048, dtype=DataType.Int)
    S41 = fd.define_scalar(12288, dtype=DataType.Int)
    V42 = fd.define_vector([S39, S40, S41], dtype=DataType.Int)
    T43 = fd.ops.broadcast_in_dim(T32, shape=V42, broadcast_dims=[0, 1, 2])
    T44 = fd.ops.mul(T38, T43)
    S45 = fd.define_scalar(1, dtype=DataType.Int)
    S46 = fd.define_scalar(2048, dtype=DataType.Int)
    S47 = fd.define_scalar(12288, dtype=DataType.Int)
    V48 = fd.define_vector([S45, S46, S47], dtype=DataType.Int)
    T49 = fd.ops.broadcast_in_dim(T1, shape=V48, broadcast_dims=[2])
    T50 = fd.ops.cast(T49, dtype=DataType.Float)
    T51 = fd.ops.mul(T44, T50)
    S52 = fd.define_scalar(1, dtype=DataType.Int)
    S53 = fd.define_scalar(2048, dtype=DataType.Int)
    S54 = fd.define_scalar(12288, dtype=DataType.Int)
    V55 = fd.define_vector([S52, S53, S54], dtype=DataType.Int)
    T56 = fd.ops.broadcast_in_dim(T2, shape=V55, broadcast_dims=[2])
    T57 = fd.ops.cast(T56, dtype=DataType.Float)
    T58 = fd.ops.add(T51, T57)
    T59 = fd.ops.cast(T58, dtype=DataType.BFloat16)
    T60 = fd.ops.linear(T59, T3, T4)
    T61 = fd.ops.slice(
        T60, start_indices=[0, 0, 0], end_indices=[1, 2048, 12288], strides=[1, 1, 1]
    )
    T62 = fd.ops.slice(
        T60,
        start_indices=[0, 0, 12288],
        end_indices=[1, 2048, 24576],
        strides=[1, 1, 1],
    )
    T63 = fd.ops.slice(
        T60,
        start_indices=[0, 0, 24576],
        end_indices=[1, 2048, 36864],
        strides=[1, 1, 1],
    )
    S64 = fd.define_scalar(1, dtype=DataType.Int)
    S65 = fd.define_scalar(2048, dtype=DataType.Int)
    S66 = fd.define_scalar(96, dtype=DataType.Int)
    S67 = fd.define_scalar(128, dtype=DataType.Int)
    V68 = fd.define_vector([S64, S65, S66, S67], dtype=DataType.Int)
    T69 = fd.ops.reshape(T62, new_shape=V68)
    T70 = fd.ops.permute(T69, dims=[0, 2, 1, 3])
    S71 = fd.define_scalar(1, dtype=DataType.Int)
    S72 = fd.define_scalar(2048, dtype=DataType.Int)
    S73 = fd.define_scalar(96, dtype=DataType.Int)
    S74 = fd.define_scalar(128, dtype=DataType.Int)
    V75 = fd.define_vector([S71, S72, S73, S74], dtype=DataType.Int)
    T76 = fd.ops.reshape(T61, new_shape=V75)
    T77 = fd.ops.permute(T76, dims=[0, 2, 1, 3])
    S78 = fd.define_scalar(1, dtype=DataType.Int)
    S79 = fd.define_scalar(2048, dtype=DataType.Int)
    S80 = fd.define_scalar(96, dtype=DataType.Int)
    S81 = fd.define_scalar(128, dtype=DataType.Int)
    V82 = fd.define_vector([S78, S79, S80, S81], dtype=DataType.Int)
    T83 = fd.ops.reshape(T63, new_shape=V82)
    T84 = fd.ops.permute(T83, dims=[0, 2, 1, 3])
    S85 = fd.define_scalar(0.100000, dtype=DataType.Double)
    S86 = fd.define_scalar(True, dtype=DataType.Bool)
    T87, T88, T89, T90 = fd.ops.sdpfa_fwd(T77, T70, T84, S85, S86, None)
    T91 = fd.ops.permute(T87, dims=[0, 2, 1, 3])
    T92 = fd.ops.stride_order(T91, stride_order=[3, 2, 1, 0])
    S93 = fd.define_scalar(1, dtype=DataType.Int)
    S94 = fd.define_scalar(2048, dtype=DataType.Int)
    S95 = fd.define_scalar(12288, dtype=DataType.Int)
    V96 = fd.define_vector([S93, S94, S95], dtype=DataType.Int)
    T97 = fd.ops.reshape(T92, new_shape=V96)
    T98 = fd.ops.linear(T97, T5, T6)
    S99 = fd.define_scalar(0.00000, dtype=DataType.Double)
    S100 = fd.define_scalar(1.00000, dtype=DataType.Double)
    S101 = fd.define_scalar(1, dtype=DataType.Int)
    S102 = fd.define_scalar(2048, dtype=DataType.Int)
    S103 = fd.define_scalar(12288, dtype=DataType.Int)
    V104 = fd.define_vector([S101, S102, S103], dtype=DataType.Int)
    T105 = fd.ops.uniform(
        S99, S100, shape=V104, rng_seed=S8, rng_offset=S7, dtype=DataType.BFloat16
    )
    S106 = fd.define_scalar(0.900000, dtype=DataType.Double)
    T107 = fd.ops.lt(T105, S106)
    T108 = fd.ops.cast(T98, dtype=DataType.Float)
    T109 = fd.ops.cast(T107, dtype=DataType.Float)
    T110 = fd.ops.mul(T108, T109)
    S111 = fd.define_scalar(1.11111, dtype=DataType.Double)
    T112 = fd.ops.mul(T110, S111)
    T113 = fd.ops.add(T17, T112)
    T114, T115 = fd.ops.var_mean(T113, dims=[2], correction=0, keepdim=False)
    S116 = fd.define_scalar(1, dtype=DataType.Int)
    S117 = fd.define_scalar(2048, dtype=DataType.Int)
    S118 = fd.define_scalar(1, dtype=DataType.Int)
    V119 = fd.define_vector([S116, S117, S118], dtype=DataType.Int)
    T120 = fd.ops.broadcast_in_dim(T114, shape=V119, broadcast_dims=[0, 1])
    S121 = fd.define_scalar(1, dtype=DataType.Int)
    S122 = fd.define_scalar(2048, dtype=DataType.Int)
    S123 = fd.define_scalar(1, dtype=DataType.Int)
    V124 = fd.define_vector([S121, S122, S123], dtype=DataType.Int)
    T125 = fd.ops.broadcast_in_dim(T115, shape=V124, broadcast_dims=[0, 1])
    S126 = fd.define_scalar(1.00000e-05, dtype=DataType.Double)
    T127 = fd.ops.add(T120, S126)
    T128 = fd.ops.rsqrt(T127)
    S129 = fd.define_scalar(1, dtype=DataType.Int)
    S130 = fd.define_scalar(2048, dtype=DataType.Int)
    S131 = fd.define_scalar(12288, dtype=DataType.Int)
    V132 = fd.define_vector([S129, S130, S131], dtype=DataType.Int)
    T133 = fd.ops.broadcast_in_dim(T125, shape=V132, broadcast_dims=[0, 1, 2])
    T134 = fd.ops.sub(T113, T133)
    S135 = fd.define_scalar(1, dtype=DataType.Int)
    S136 = fd.define_scalar(2048, dtype=DataType.Int)
    S137 = fd.define_scalar(12288, dtype=DataType.Int)
    V138 = fd.define_vector([S135, S136, S137], dtype=DataType.Int)
    T139 = fd.ops.broadcast_in_dim(T128, shape=V138, broadcast_dims=[0, 1, 2])
    T140 = fd.ops.mul(T134, T139)
    S141 = fd.define_scalar(1, dtype=DataType.Int)
    S142 = fd.define_scalar(2048, dtype=DataType.Int)
    S143 = fd.define_scalar(12288, dtype=DataType.Int)
    V144 = fd.define_vector([S141, S142, S143], dtype=DataType.Int)
    T145 = fd.ops.broadcast_in_dim(T9, shape=V144, broadcast_dims=[2])
    T146 = fd.ops.cast(T145, dtype=DataType.Float)
    T147 = fd.ops.mul(T140, T146)
    S148 = fd.define_scalar(1, dtype=DataType.Int)
    S149 = fd.define_scalar(2048, dtype=DataType.Int)
    S150 = fd.define_scalar(12288, dtype=DataType.Int)
    V151 = fd.define_vector([S148, S149, S150], dtype=DataType.Int)
    T152 = fd.ops.broadcast_in_dim(T10, shape=V151, broadcast_dims=[2])
    T153 = fd.ops.cast(T152, dtype=DataType.Float)
    T154 = fd.ops.add(T147, T153)
    T155 = fd.ops.cast(T154, dtype=DataType.BFloat16)
    T156 = fd.ops.linear(T155, T11, T12)
    T157 = fd.ops.cast(T156, dtype=DataType.Float)
    T158 = fd.ops.mul(T157, T157)
    T159 = fd.ops.mul(T158, T157)
    S160 = fd.define_scalar(0.500000, dtype=DataType.Double)
    T161 = fd.ops.mul(S160, T157)
    S162 = fd.define_scalar(0.0447150, dtype=DataType.Double)
    T163 = fd.ops.mul(S162, T159)
    T164 = fd.ops.add(T157, T163)
    S165 = fd.define_scalar(0.797885, dtype=DataType.Double)
    T166 = fd.ops.mul(S165, T164)
    T167 = fd.ops.tanh(T166)
    S168 = fd.define_scalar(1.00000, dtype=DataType.Double)
    T169 = fd.ops.add(S168, T167)
    T170 = fd.ops.mul(T161, T169)
    T171 = fd.ops.cast(T170, dtype=DataType.BFloat16)
    T172 = fd.ops.linear(T171, T13, T14)
    S173 = fd.define_scalar(0.00000, dtype=DataType.Double)
    S174 = fd.define_scalar(1.00000, dtype=DataType.Double)
    S175 = fd.define_scalar(1, dtype=DataType.Int)
    S176 = fd.define_scalar(2048, dtype=DataType.Int)
    S177 = fd.define_scalar(12288, dtype=DataType.Int)
    V178 = fd.define_vector([S175, S176, S177], dtype=DataType.Int)
    T179 = fd.ops.uniform(
        S173, S174, shape=V178, rng_seed=S16, rng_offset=S15, dtype=DataType.BFloat16
    )
    S180 = fd.define_scalar(0.900000, dtype=DataType.Double)
    T181 = fd.ops.lt(T179, S180)
    T182 = fd.ops.cast(T172, dtype=DataType.Float)
    T183 = fd.ops.cast(T181, dtype=DataType.Float)
    T184 = fd.ops.mul(T182, T183)
    S185 = fd.define_scalar(1.11111, dtype=DataType.Double)
    T186 = fd.ops.mul(T184, S185)
    T187 = fd.ops.add(T113, T186)
    T188 = fd.ops.cast(T187, dtype=DataType.BFloat16)
    fd.add_output(T19)  # layer_norm0.welford_out.avg
    fd.add_output(T32)  # layer_norm0.invstd
    fd.add_output(T87)  # MHA sdpa.output
    fd.add_output(T88)  # MHA sdpa.logsum_exp
    fd.add_output(T89)  # MHA sdpa.philox_seed
    fd.add_output(T90)  # MHA sdpa.philox_offset
    fd.add_output(T115)  # layer_norm1.welford_out.avg
    fd.add_output(T128)  # layer_norm1.invstd
    fd.add_output(T188)  # output


def test_transformer_forward(
    benchmark, disable_validation: bool, disable_benchmarking: bool
):
    clear_cuda_cache()

    with FusionDefinition() as fd:
        transformer_forward_fusion(fd)

    inputs = [
        torch.randn(25165824, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (1, 2048, 12288), (25165824, 12288, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(452984832, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (36864, 12288), (12288, 1)
        ),
        torch.randn(36864, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (36864,), (1,)
        ),
        torch.randn(150994944, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288, 12288), (12288, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        29,
        203641485758702,
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(603979776, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (49152, 12288), (12288, 1)
        ),
        torch.randn(49152, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (49152,), (1,)
        ),
        torch.randn(603979776, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288, 49152), (49152, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        30,
        203641485758702,
    ]

    if not disable_benchmarking:
        run_benchmark(benchmark, fd.execute, inputs)


def transformer_backward_fusion(fd: FusionDefinition) -> None:
    # x: input
    T0 = fd.define_tensor(
        shape=[1, -1, -1],
        contiguity=[None, True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # layer_norm0.welford_out.avg
    T1 = fd.define_tensor(
        shape=[1, -1],
        contiguity=[None, True],
        dtype=DataType.Float,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # layer_norm0.invstd
    T2 = fd.define_tensor(
        shape=[1, -1, 1],
        contiguity=[None, True, None],
        dtype=DataType.Float,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # layer_norm0.weight
    T3 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # layer_norm0.bias
    T4 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA linear0.weight
    T5 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MHA linear0.bias
    T6 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA sdpa.output
    T7 = fd.define_tensor(
        shape=[1, -1, -1, -1],
        contiguity=[None, True, True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[3, 1, 2, 0],
    )
    # MHA linear1.weight
    T8 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MHA linear1.bias
    T9 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MHA dropout.rng_offset
    S10 = fd.define_scalar(None, dtype=DataType.Int)
    # MHA dropout.rng_seed
    S11 = fd.define_scalar(None, dtype=DataType.Int)
    # layer_norm1.welford_out.avg
    T12 = fd.define_tensor(
        shape=[1, -1],
        contiguity=[None, True],
        dtype=DataType.Float,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # layer_norm1.invstd
    T13 = fd.define_tensor(
        shape=[1, -1, 1],
        contiguity=[None, True, None],
        dtype=DataType.Float,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # layer_norm1.weight
    T14 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # layer_norm1.bias
    T15 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MLP linear0.weight
    T16 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MLP linear0.bias
    T17 = fd.define_tensor(
        shape=[-1],
        contiguity=[True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[0],
    )
    # MLP dropout.rng_offset
    S18 = fd.define_scalar(None, dtype=DataType.Int)
    # MLP dropout.rng_seed
    S19 = fd.define_scalar(None, dtype=DataType.Int)
    # dy: incoming grad
    T20 = fd.define_tensor(
        shape=[1, -1, -1],
        contiguity=[None, True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # MLP linear1.weight
    T21 = fd.define_tensor(
        shape=[-1, -1],
        contiguity=[True, True],
        dtype=DataType.BFloat16,
        is_cpu=False,
        stride_order=[1, 0],
    )
    # MHA sdpa.logsum_exp
    T22 = fd.define_tensor(
        shape=[1, -1, -1],
        contiguity=[None, True, True],
        dtype=DataType.Float,
        is_cpu=False,
        stride_order=[2, 1, 0],
    )
    # MHA sdpa.philox_seed
    T23 = fd.define_tensor(shape=[], contiguity=[], dtype=DataType.Int, is_cpu=False)
    # MHA sdpa.philox_offset
    T24 = fd.define_tensor(shape=[], contiguity=[], dtype=DataType.Int, is_cpu=False)

    T25 = fd.ops.cast(T0, dtype=DataType.Float)
    S26 = fd.define_scalar(1, dtype=DataType.Int)
    S27 = fd.define_scalar(2048, dtype=DataType.Int)
    S28 = fd.define_scalar(1, dtype=DataType.Int)
    V29 = fd.define_vector([S26, S27, S28], dtype=DataType.Int)
    T30 = fd.ops.broadcast_in_dim(T1, shape=V29, broadcast_dims=[0, 1])
    S31 = fd.define_scalar(1, dtype=DataType.Int)
    S32 = fd.define_scalar(2048, dtype=DataType.Int)
    S33 = fd.define_scalar(12288, dtype=DataType.Int)
    V34 = fd.define_vector([S31, S32, S33], dtype=DataType.Int)
    T35 = fd.ops.broadcast_in_dim(T30, shape=V34, broadcast_dims=[0, 1, 2])
    T36 = fd.ops.sub(T25, T35)
    S37 = fd.define_scalar(1, dtype=DataType.Int)
    S38 = fd.define_scalar(2048, dtype=DataType.Int)
    S39 = fd.define_scalar(12288, dtype=DataType.Int)
    V40 = fd.define_vector([S37, S38, S39], dtype=DataType.Int)
    T41 = fd.ops.broadcast_in_dim(T2, shape=V40, broadcast_dims=[0, 1, 2])
    T42 = fd.ops.mul(T36, T41)
    S43 = fd.define_scalar(1, dtype=DataType.Int)
    S44 = fd.define_scalar(2048, dtype=DataType.Int)
    S45 = fd.define_scalar(12288, dtype=DataType.Int)
    V46 = fd.define_vector([S43, S44, S45], dtype=DataType.Int)
    T47 = fd.ops.broadcast_in_dim(T3, shape=V46, broadcast_dims=[2])
    T48 = fd.ops.cast(T47, dtype=DataType.Float)
    T49 = fd.ops.mul(T42, T48)
    S50 = fd.define_scalar(1, dtype=DataType.Int)
    S51 = fd.define_scalar(2048, dtype=DataType.Int)
    S52 = fd.define_scalar(12288, dtype=DataType.Int)
    V53 = fd.define_vector([S50, S51, S52], dtype=DataType.Int)
    T54 = fd.ops.broadcast_in_dim(T4, shape=V53, broadcast_dims=[2])
    T55 = fd.ops.cast(T54, dtype=DataType.Float)
    T56 = fd.ops.add(T49, T55)
    T57 = fd.ops.cast(T56, dtype=DataType.BFloat16)
    T58 = fd.ops.linear(T57, T5, T6)
    T59 = fd.ops.slice(
        T58, start_indices=[0, 0, 0], end_indices=[1, 2048, 12288], strides=[1, 1, 1]
    )
    T60 = fd.ops.slice(
        T58,
        start_indices=[0, 0, 12288],
        end_indices=[1, 2048, 24576],
        strides=[1, 1, 1],
    )
    T61 = fd.ops.slice(
        T58,
        start_indices=[0, 0, 24576],
        end_indices=[1, 2048, 36864],
        strides=[1, 1, 1],
    )
    S62 = fd.define_scalar(1, dtype=DataType.Int)
    S63 = fd.define_scalar(2048, dtype=DataType.Int)
    S64 = fd.define_scalar(96, dtype=DataType.Int)
    S65 = fd.define_scalar(128, dtype=DataType.Int)
    V66 = fd.define_vector([S62, S63, S64, S65], dtype=DataType.Int)
    T67 = fd.ops.reshape(T60, new_shape=V66)
    T68 = fd.ops.permute(T67, dims=[0, 2, 1, 3])
    S69 = fd.define_scalar(1, dtype=DataType.Int)
    S70 = fd.define_scalar(2048, dtype=DataType.Int)
    S71 = fd.define_scalar(96, dtype=DataType.Int)
    S72 = fd.define_scalar(128, dtype=DataType.Int)
    V73 = fd.define_vector([S69, S70, S71, S72], dtype=DataType.Int)
    T74 = fd.ops.reshape(T59, new_shape=V73)
    T75 = fd.ops.permute(T74, dims=[0, 2, 1, 3])
    S76 = fd.define_scalar(1, dtype=DataType.Int)
    S77 = fd.define_scalar(2048, dtype=DataType.Int)
    S78 = fd.define_scalar(96, dtype=DataType.Int)
    S79 = fd.define_scalar(128, dtype=DataType.Int)
    V80 = fd.define_vector([S76, S77, S78, S79], dtype=DataType.Int)
    T81 = fd.ops.reshape(T61, new_shape=V80)
    T82 = fd.ops.permute(T81, dims=[0, 2, 1, 3])
    T83 = fd.ops.permute(T7, dims=[0, 2, 1, 3])
    T84 = fd.ops.stride_order(T83, stride_order=[3, 2, 1, 0])
    S85 = fd.define_scalar(1, dtype=DataType.Int)
    S86 = fd.define_scalar(2048, dtype=DataType.Int)
    S87 = fd.define_scalar(12288, dtype=DataType.Int)
    V88 = fd.define_vector([S85, S86, S87], dtype=DataType.Int)
    T89 = fd.ops.reshape(T84, new_shape=V88)
    T90 = fd.ops.linear(T89, T8, T9)
    S91 = fd.define_scalar(0.00000, dtype=DataType.Double)
    S92 = fd.define_scalar(1.00000, dtype=DataType.Double)
    S93 = fd.define_scalar(1, dtype=DataType.Int)
    S94 = fd.define_scalar(2048, dtype=DataType.Int)
    S95 = fd.define_scalar(12288, dtype=DataType.Int)
    V96 = fd.define_vector([S93, S94, S95], dtype=DataType.Int)
    T97 = fd.ops.uniform(
        S91, S92, shape=V96, rng_seed=S11, rng_offset=S10, dtype=DataType.BFloat16
    )
    S98 = fd.define_scalar(0.900000, dtype=DataType.Double)
    T99 = fd.ops.lt(T97, S98)
    T100 = fd.ops.cast(T90, dtype=DataType.Float)
    T101 = fd.ops.cast(T99, dtype=DataType.Float)
    T102 = fd.ops.mul(T100, T101)
    S103 = fd.define_scalar(1.11111, dtype=DataType.Double)
    T104 = fd.ops.mul(T102, S103)
    T105 = fd.ops.add(T25, T104)
    S106 = fd.define_scalar(1, dtype=DataType.Int)
    S107 = fd.define_scalar(2048, dtype=DataType.Int)
    S108 = fd.define_scalar(1, dtype=DataType.Int)
    V109 = fd.define_vector([S106, S107, S108], dtype=DataType.Int)
    T110 = fd.ops.broadcast_in_dim(T12, shape=V109, broadcast_dims=[0, 1])
    S111 = fd.define_scalar(1, dtype=DataType.Int)
    S112 = fd.define_scalar(2048, dtype=DataType.Int)
    S113 = fd.define_scalar(12288, dtype=DataType.Int)
    V114 = fd.define_vector([S111, S112, S113], dtype=DataType.Int)
    T115 = fd.ops.broadcast_in_dim(T110, shape=V114, broadcast_dims=[0, 1, 2])
    T116 = fd.ops.sub(T105, T115)
    S117 = fd.define_scalar(1, dtype=DataType.Int)
    S118 = fd.define_scalar(2048, dtype=DataType.Int)
    S119 = fd.define_scalar(12288, dtype=DataType.Int)
    V120 = fd.define_vector([S117, S118, S119], dtype=DataType.Int)
    T121 = fd.ops.broadcast_in_dim(T13, shape=V120, broadcast_dims=[0, 1, 2])
    T122 = fd.ops.mul(T116, T121)
    S123 = fd.define_scalar(1, dtype=DataType.Int)
    S124 = fd.define_scalar(2048, dtype=DataType.Int)
    S125 = fd.define_scalar(12288, dtype=DataType.Int)
    V126 = fd.define_vector([S123, S124, S125], dtype=DataType.Int)
    T127 = fd.ops.broadcast_in_dim(T14, shape=V126, broadcast_dims=[2])
    T128 = fd.ops.cast(T127, dtype=DataType.Float)
    T129 = fd.ops.mul(T122, T128)
    S130 = fd.define_scalar(1, dtype=DataType.Int)
    S131 = fd.define_scalar(2048, dtype=DataType.Int)
    S132 = fd.define_scalar(12288, dtype=DataType.Int)
    V133 = fd.define_vector([S130, S131, S132], dtype=DataType.Int)
    T134 = fd.ops.broadcast_in_dim(T15, shape=V133, broadcast_dims=[2])
    T135 = fd.ops.cast(T134, dtype=DataType.Float)
    T136 = fd.ops.add(T129, T135)
    T137 = fd.ops.cast(T136, dtype=DataType.BFloat16)
    T138 = fd.ops.linear(T137, T16, T17)
    T139 = fd.ops.cast(T138, dtype=DataType.Float)
    T140 = fd.ops.mul(T139, T139)
    T141 = fd.ops.mul(T140, T139)
    S142 = fd.define_scalar(0.500000, dtype=DataType.Double)
    T143 = fd.ops.mul(S142, T139)
    S144 = fd.define_scalar(0.0447150, dtype=DataType.Double)
    T145 = fd.ops.mul(S144, T141)
    T146 = fd.ops.add(T139, T145)
    S147 = fd.define_scalar(0.797885, dtype=DataType.Double)
    T148 = fd.ops.mul(S147, T146)
    T149 = fd.ops.tanh(T148)
    S150 = fd.define_scalar(1.00000, dtype=DataType.Double)
    T151 = fd.ops.add(S150, T149)
    T152 = fd.ops.mul(T143, T151)
    T153 = fd.ops.cast(T152, dtype=DataType.BFloat16)
    S154 = fd.define_scalar(0.00000, dtype=DataType.Double)
    S155 = fd.define_scalar(1.00000, dtype=DataType.Double)
    S156 = fd.define_scalar(1, dtype=DataType.Int)
    S157 = fd.define_scalar(2048, dtype=DataType.Int)
    S158 = fd.define_scalar(12288, dtype=DataType.Int)
    V159 = fd.define_vector([S156, S157, S158], dtype=DataType.Int)
    T160 = fd.ops.uniform(
        S154, S155, shape=V159, rng_seed=S19, rng_offset=S18, dtype=DataType.BFloat16
    )
    S161 = fd.define_scalar(0.900000, dtype=DataType.Double)
    T162 = fd.ops.lt(T160, S161)
    T163 = fd.ops.cast(T162, dtype=DataType.Float)
    T164 = fd.ops.cast(T20, dtype=DataType.Float)
    S165 = fd.define_scalar(1.11111, dtype=DataType.Double)
    T166 = fd.ops.mul(S165, T164)
    T167 = fd.ops.mul(T163, T166)
    T168 = fd.ops.cast(T167, dtype=DataType.BFloat16)
    S169 = fd.define_scalar(2048, dtype=DataType.Int)
    S170 = fd.define_scalar(12288, dtype=DataType.Int)
    V171 = fd.define_vector([S169, S170], dtype=DataType.Int)
    T172 = fd.ops.reshape(T168, new_shape=V171)
    T173 = fd.ops.matmul(T172, T21)
    S174 = fd.define_scalar(1, dtype=DataType.Int)
    S175 = fd.define_scalar(2048, dtype=DataType.Int)
    S176 = fd.define_scalar(49152, dtype=DataType.Int)
    V177 = fd.define_vector([S174, S175, S176], dtype=DataType.Int)
    T178 = fd.ops.reshape(T173, new_shape=V177)
    T179 = fd.ops.permute(T172, dims=[1, 0])
    S180 = fd.define_scalar(2048, dtype=DataType.Int)
    S181 = fd.define_scalar(49152, dtype=DataType.Int)
    V182 = fd.define_vector([S180, S181], dtype=DataType.Int)
    T183 = fd.ops.reshape(T153, new_shape=V182)
    T184 = fd.ops.matmul(T179, T183)
    T185 = fd.ops.sum(T167, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T186 = fd.ops.cast(T185, dtype=DataType.BFloat16)
    T187 = fd.ops.cast(T178, dtype=DataType.Float)
    T188 = fd.ops.mul(T151, T187)
    T189 = fd.ops.mul(T143, T187)
    T190 = fd.ops.mul(T149, T149)
    S191 = fd.define_scalar(1.00000, dtype=DataType.Double)
    T192 = fd.ops.sub(S191, T190)
    T193 = fd.ops.mul(T189, T192)
    S194 = fd.define_scalar(0.797885, dtype=DataType.Double)
    T195 = fd.ops.mul(S194, T193)
    S196 = fd.define_scalar(0.0447150, dtype=DataType.Double)
    T197 = fd.ops.mul(S196, T195)
    S198 = fd.define_scalar(0.500000, dtype=DataType.Double)
    T199 = fd.ops.mul(S198, T188)
    T200 = fd.ops.add(T195, T199)
    T201 = fd.ops.mul(T139, T197)
    T202 = fd.ops.mul(T140, T197)
    T203 = fd.ops.add(T200, T202)
    T204 = fd.ops.mul(T139, T201)
    T205 = fd.ops.add(T203, T204)
    T206 = fd.ops.add(T205, T204)
    T207 = fd.ops.cast(T206, dtype=DataType.BFloat16)
    S208 = fd.define_scalar(2048, dtype=DataType.Int)
    S209 = fd.define_scalar(49152, dtype=DataType.Int)
    V210 = fd.define_vector([S208, S209], dtype=DataType.Int)
    T211 = fd.ops.reshape(T207, new_shape=V210)
    T212 = fd.ops.matmul(T211, T16)
    S213 = fd.define_scalar(1, dtype=DataType.Int)
    S214 = fd.define_scalar(2048, dtype=DataType.Int)
    S215 = fd.define_scalar(12288, dtype=DataType.Int)
    V216 = fd.define_vector([S213, S214, S215], dtype=DataType.Int)
    T217 = fd.ops.reshape(T212, new_shape=V216)
    T218 = fd.ops.permute(T211, dims=[1, 0])
    S219 = fd.define_scalar(2048, dtype=DataType.Int)
    S220 = fd.define_scalar(12288, dtype=DataType.Int)
    V221 = fd.define_vector([S219, S220], dtype=DataType.Int)
    T222 = fd.ops.reshape(T137, new_shape=V221)
    T223 = fd.ops.matmul(T218, T222)
    T224 = fd.ops.sum(T206, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T225 = fd.ops.cast(T224, dtype=DataType.BFloat16)
    T226 = fd.ops.cast(T217, dtype=DataType.Float)
    T227 = fd.ops.sum(T226, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T228 = fd.ops.cast(T227, dtype=DataType.BFloat16)
    T229 = fd.ops.mul(T128, T226)
    T230 = fd.ops.mul(T122, T226)
    T231 = fd.ops.sum(T230, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T232 = fd.ops.cast(T231, dtype=DataType.BFloat16)
    T233 = fd.ops.mul(T121, T229)
    T234 = fd.ops.mul(T116, T229)
    T235 = fd.ops.sum(T234, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S236 = fd.define_scalar(1, dtype=DataType.Int)
    S237 = fd.define_scalar(2048, dtype=DataType.Int)
    S238 = fd.define_scalar(1, dtype=DataType.Int)
    V239 = fd.define_vector([S236, S237, S238], dtype=DataType.Int)
    T240 = fd.ops.broadcast_in_dim(T235, shape=V239, broadcast_dims=[1])
    T241 = fd.ops.neg(T233)
    T242 = fd.ops.sum(T241, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S243 = fd.define_scalar(1, dtype=DataType.Int)
    S244 = fd.define_scalar(2048, dtype=DataType.Int)
    S245 = fd.define_scalar(1, dtype=DataType.Int)
    V246 = fd.define_vector([S243, S244, S245], dtype=DataType.Int)
    T247 = fd.ops.broadcast_in_dim(T242, shape=V246, broadcast_dims=[1])
    S248 = fd.define_scalar(-0.500000, dtype=DataType.Double)
    T249 = fd.ops.mul(S248, T240)
    S250 = fd.define_scalar(3.00000, dtype=DataType.Double)
    T251 = fd.ops.pow(T13, S250)
    T252 = fd.ops.mul(T249, T251)
    T253 = fd.ops.sum(T247, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S254 = fd.define_scalar(1, dtype=DataType.Int)
    S255 = fd.define_scalar(2048, dtype=DataType.Int)
    V256 = fd.define_vector([S254, S255], dtype=DataType.Int)
    T257 = fd.ops.broadcast_in_dim(T253, shape=V256, broadcast_dims=[1])
    T258 = fd.ops.sum(T252, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S259 = fd.define_scalar(1, dtype=DataType.Int)
    S260 = fd.define_scalar(2048, dtype=DataType.Int)
    V261 = fd.define_vector([S259, S260], dtype=DataType.Int)
    T262 = fd.ops.broadcast_in_dim(T258, shape=V261, broadcast_dims=[1])
    S263 = fd.define_scalar(1, dtype=DataType.Int)
    S264 = fd.define_scalar(2048, dtype=DataType.Int)
    S265 = fd.define_scalar(1, dtype=DataType.Int)
    V266 = fd.define_vector([S263, S264, S265], dtype=DataType.Int)
    T267 = fd.ops.broadcast_in_dim(T257, shape=V266, broadcast_dims=[0, 1])
    S268 = fd.define_scalar(1, dtype=DataType.Int)
    S269 = fd.define_scalar(2048, dtype=DataType.Int)
    S270 = fd.define_scalar(12288, dtype=DataType.Int)
    V271 = fd.define_vector([S268, S269, S270], dtype=DataType.Int)
    T272 = fd.ops.broadcast_in_dim(T267, shape=V271, broadcast_dims=[0, 1, 2])
    S273 = fd.define_scalar(8.13802e-05, dtype=DataType.Double)
    T274 = fd.ops.mul(S273, T272)
    S275 = fd.define_scalar(1, dtype=DataType.Int)
    S276 = fd.define_scalar(2048, dtype=DataType.Int)
    S277 = fd.define_scalar(1, dtype=DataType.Int)
    V278 = fd.define_vector([S275, S276, S277], dtype=DataType.Int)
    T279 = fd.ops.broadcast_in_dim(T262, shape=V278, broadcast_dims=[0, 1])
    S280 = fd.define_scalar(1, dtype=DataType.Int)
    S281 = fd.define_scalar(2048, dtype=DataType.Int)
    S282 = fd.define_scalar(12288, dtype=DataType.Int)
    V283 = fd.define_vector([S280, S281, S282], dtype=DataType.Int)
    T284 = fd.ops.broadcast_in_dim(T279, shape=V283, broadcast_dims=[0, 1, 2])
    S285 = fd.define_scalar(1, dtype=DataType.Int)
    S286 = fd.define_scalar(2048, dtype=DataType.Int)
    S287 = fd.define_scalar(1, dtype=DataType.Int)
    V288 = fd.define_vector([S285, S286, S287], dtype=DataType.Int)
    T289 = fd.ops.broadcast_in_dim(T12, shape=V288, broadcast_dims=[0, 1])
    S290 = fd.define_scalar(1, dtype=DataType.Int)
    S291 = fd.define_scalar(2048, dtype=DataType.Int)
    S292 = fd.define_scalar(12288, dtype=DataType.Int)
    V293 = fd.define_vector([S290, S291, S292], dtype=DataType.Int)
    T294 = fd.ops.broadcast_in_dim(T289, shape=V293, broadcast_dims=[0, 1, 2])
    S295 = fd.define_scalar(2.00000, dtype=DataType.Double)
    T296 = fd.ops.mul(S295, T284)
    T297 = fd.ops.sub(T105, T294)
    T298 = fd.ops.mul(T296, T297)
    S299 = fd.define_scalar(12288.0, dtype=DataType.Double)
    S300 = fd.ops.reciprocal(S299)
    T301 = fd.ops.mul(T298, S300)
    T302 = fd.ops.add(T274, T301)
    T303 = fd.ops.add(T233, T302)
    T304 = fd.ops.add(T164, T303)
    S305 = fd.define_scalar(1.11111, dtype=DataType.Double)
    T306 = fd.ops.mul(S305, T304)
    T307 = fd.ops.mul(T101, T306)
    T308 = fd.ops.cast(T307, dtype=DataType.BFloat16)
    S309 = fd.define_scalar(2048, dtype=DataType.Int)
    S310 = fd.define_scalar(12288, dtype=DataType.Int)
    V311 = fd.define_vector([S309, S310], dtype=DataType.Int)
    T312 = fd.ops.reshape(T308, new_shape=V311)
    T313 = fd.ops.matmul(T312, T8)
    S314 = fd.define_scalar(1, dtype=DataType.Int)
    S315 = fd.define_scalar(2048, dtype=DataType.Int)
    S316 = fd.define_scalar(12288, dtype=DataType.Int)
    V317 = fd.define_vector([S314, S315, S316], dtype=DataType.Int)
    T318 = fd.ops.reshape(T313, new_shape=V317)
    T319 = fd.ops.permute(T312, dims=[1, 0])
    S320 = fd.define_scalar(2048, dtype=DataType.Int)
    S321 = fd.define_scalar(12288, dtype=DataType.Int)
    V322 = fd.define_vector([S320, S321], dtype=DataType.Int)
    T323 = fd.ops.reshape(T89, new_shape=V322)
    T324 = fd.ops.matmul(T319, T323)
    T325 = fd.ops.sum(T307, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T326 = fd.ops.cast(T325, dtype=DataType.BFloat16)
    S327 = fd.define_scalar(1, dtype=DataType.Int)
    S328 = fd.define_scalar(2048, dtype=DataType.Int)
    S329 = fd.define_scalar(96, dtype=DataType.Int)
    S330 = fd.define_scalar(128, dtype=DataType.Int)
    V331 = fd.define_vector([S327, S328, S329, S330], dtype=DataType.Int)
    T332 = fd.ops.reshape(T318, new_shape=V331)
    T333 = fd.ops.permute(T332, dims=[0, 2, 1, 3])
    S334 = fd.define_scalar(0.100000, dtype=DataType.Double)
    S335 = fd.define_scalar(True, dtype=DataType.Bool)
    T336, T337, T338 = fd.ops.sdpfa_bwd(
        T333, T75, T68, T82, T7, T22, S334, S335, T23, T24, None
    )
    T339 = fd.ops.permute(T338, dims=[0, 2, 1, 3])
    S340 = fd.define_scalar(1, dtype=DataType.Int)
    S341 = fd.define_scalar(2048, dtype=DataType.Int)
    S342 = fd.define_scalar(12288, dtype=DataType.Int)
    V343 = fd.define_vector([S340, S341, S342], dtype=DataType.Int)
    T344 = fd.ops.reshape(T339, new_shape=V343)
    T345 = fd.ops.permute(T336, dims=[0, 2, 1, 3])
    S346 = fd.define_scalar(1, dtype=DataType.Int)
    S347 = fd.define_scalar(2048, dtype=DataType.Int)
    S348 = fd.define_scalar(12288, dtype=DataType.Int)
    V349 = fd.define_vector([S346, S347, S348], dtype=DataType.Int)
    T350 = fd.ops.reshape(T345, new_shape=V349)
    T351 = fd.ops.permute(T337, dims=[0, 2, 1, 3])
    S352 = fd.define_scalar(1, dtype=DataType.Int)
    S353 = fd.define_scalar(2048, dtype=DataType.Int)
    S354 = fd.define_scalar(12288, dtype=DataType.Int)
    V355 = fd.define_vector([S352, S353, S354], dtype=DataType.Int)
    T356 = fd.ops.reshape(T351, new_shape=V355)
    T357 = fd.ops.cat([T350, T356, T344], dim=2)
    S358 = fd.define_scalar(2048, dtype=DataType.Int)
    S359 = fd.define_scalar(36864, dtype=DataType.Int)
    V360 = fd.define_vector([S358, S359], dtype=DataType.Int)
    T361 = fd.ops.reshape(T357, new_shape=V360)
    T362 = fd.ops.matmul(T361, T5)
    S363 = fd.define_scalar(1, dtype=DataType.Int)
    S364 = fd.define_scalar(2048, dtype=DataType.Int)
    S365 = fd.define_scalar(12288, dtype=DataType.Int)
    V366 = fd.define_vector([S363, S364, S365], dtype=DataType.Int)
    T367 = fd.ops.reshape(T362, new_shape=V366)
    T368 = fd.ops.permute(T361, dims=[1, 0])
    S369 = fd.define_scalar(2048, dtype=DataType.Int)
    S370 = fd.define_scalar(12288, dtype=DataType.Int)
    V371 = fd.define_vector([S369, S370], dtype=DataType.Int)
    T372 = fd.ops.reshape(T57, new_shape=V371)
    T373 = fd.ops.matmul(T368, T372)
    T374 = fd.ops.cast(T357, dtype=DataType.Float)
    T375 = fd.ops.sum(T374, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T376 = fd.ops.cast(T375, dtype=DataType.BFloat16)
    T377 = fd.ops.cast(T367, dtype=DataType.Float)
    T378 = fd.ops.sum(T377, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T379 = fd.ops.cast(T378, dtype=DataType.BFloat16)
    T380 = fd.ops.mul(T48, T377)
    T381 = fd.ops.mul(T42, T377)
    T382 = fd.ops.sum(T381, dims=[0, 1], keepdim=False, dtype=DataType.Null)
    T383 = fd.ops.cast(T382, dtype=DataType.BFloat16)
    T384 = fd.ops.mul(T41, T380)
    T385 = fd.ops.mul(T36, T380)
    T386 = fd.ops.sum(T385, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S387 = fd.define_scalar(1, dtype=DataType.Int)
    S388 = fd.define_scalar(2048, dtype=DataType.Int)
    S389 = fd.define_scalar(1, dtype=DataType.Int)
    V390 = fd.define_vector([S387, S388, S389], dtype=DataType.Int)
    T391 = fd.ops.broadcast_in_dim(T386, shape=V390, broadcast_dims=[1])
    T392 = fd.ops.neg(T384)
    T393 = fd.ops.sum(T392, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S394 = fd.define_scalar(1, dtype=DataType.Int)
    S395 = fd.define_scalar(2048, dtype=DataType.Int)
    S396 = fd.define_scalar(1, dtype=DataType.Int)
    V397 = fd.define_vector([S394, S395, S396], dtype=DataType.Int)
    T398 = fd.ops.broadcast_in_dim(T393, shape=V397, broadcast_dims=[1])
    S399 = fd.define_scalar(-0.500000, dtype=DataType.Double)
    T400 = fd.ops.mul(S399, T391)
    S401 = fd.define_scalar(3.00000, dtype=DataType.Double)
    T402 = fd.ops.pow(T2, S401)
    T403 = fd.ops.mul(T400, T402)
    T404 = fd.ops.sum(T398, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S405 = fd.define_scalar(1, dtype=DataType.Int)
    S406 = fd.define_scalar(2048, dtype=DataType.Int)
    V407 = fd.define_vector([S405, S406], dtype=DataType.Int)
    T408 = fd.ops.broadcast_in_dim(T404, shape=V407, broadcast_dims=[1])
    T409 = fd.ops.sum(T403, dims=[0, 2], keepdim=False, dtype=DataType.Null)
    S410 = fd.define_scalar(1, dtype=DataType.Int)
    S411 = fd.define_scalar(2048, dtype=DataType.Int)
    V412 = fd.define_vector([S410, S411], dtype=DataType.Int)
    T413 = fd.ops.broadcast_in_dim(T409, shape=V412, broadcast_dims=[1])
    S414 = fd.define_scalar(1, dtype=DataType.Int)
    S415 = fd.define_scalar(2048, dtype=DataType.Int)
    S416 = fd.define_scalar(1, dtype=DataType.Int)
    V417 = fd.define_vector([S414, S415, S416], dtype=DataType.Int)
    T418 = fd.ops.broadcast_in_dim(T408, shape=V417, broadcast_dims=[0, 1])
    S419 = fd.define_scalar(1, dtype=DataType.Int)
    S420 = fd.define_scalar(2048, dtype=DataType.Int)
    S421 = fd.define_scalar(12288, dtype=DataType.Int)
    V422 = fd.define_vector([S419, S420, S421], dtype=DataType.Int)
    T423 = fd.ops.broadcast_in_dim(T418, shape=V422, broadcast_dims=[0, 1, 2])
    S424 = fd.define_scalar(8.13802e-05, dtype=DataType.Double)
    T425 = fd.ops.mul(S424, T423)
    S426 = fd.define_scalar(1, dtype=DataType.Int)
    S427 = fd.define_scalar(2048, dtype=DataType.Int)
    S428 = fd.define_scalar(1, dtype=DataType.Int)
    V429 = fd.define_vector([S426, S427, S428], dtype=DataType.Int)
    T430 = fd.ops.broadcast_in_dim(T413, shape=V429, broadcast_dims=[0, 1])
    S431 = fd.define_scalar(1, dtype=DataType.Int)
    S432 = fd.define_scalar(2048, dtype=DataType.Int)
    S433 = fd.define_scalar(12288, dtype=DataType.Int)
    V434 = fd.define_vector([S431, S432, S433], dtype=DataType.Int)
    T435 = fd.ops.broadcast_in_dim(T430, shape=V434, broadcast_dims=[0, 1, 2])
    S436 = fd.define_scalar(1, dtype=DataType.Int)
    S437 = fd.define_scalar(2048, dtype=DataType.Int)
    S438 = fd.define_scalar(1, dtype=DataType.Int)
    V439 = fd.define_vector([S436, S437, S438], dtype=DataType.Int)
    T440 = fd.ops.broadcast_in_dim(T1, shape=V439, broadcast_dims=[0, 1])
    S441 = fd.define_scalar(1, dtype=DataType.Int)
    S442 = fd.define_scalar(2048, dtype=DataType.Int)
    S443 = fd.define_scalar(12288, dtype=DataType.Int)
    V444 = fd.define_vector([S441, S442, S443], dtype=DataType.Int)
    T445 = fd.ops.broadcast_in_dim(T440, shape=V444, broadcast_dims=[0, 1, 2])
    S446 = fd.define_scalar(2.00000, dtype=DataType.Double)
    T447 = fd.ops.mul(S446, T435)
    T448 = fd.ops.sub(T25, T445)
    T449 = fd.ops.mul(T447, T448)
    S450 = fd.define_scalar(12288.0, dtype=DataType.Double)
    S451 = fd.ops.reciprocal(S450)
    T452 = fd.ops.mul(T449, S451)
    T453 = fd.ops.add(T425, T452)
    T454 = fd.ops.add(T384, T453)
    T455 = fd.ops.add(T304, T454)
    T456 = fd.ops.cast(T455, dtype=DataType.BFloat16)
    fd.add_output(T184)  # MLP linear1.weight_grad
    fd.add_output(T186)  # MLP linear1.bias_grad
    fd.add_output(T223)  # MLP linear0.weight_grad
    fd.add_output(T225)  # MLP linear0.bias_grad
    fd.add_output(T228)  # layer_norm1.bias_grad
    fd.add_output(T232)  # layer_norm1.weight_grad
    fd.add_output(T324)  # MHA linear1.weight_grad
    fd.add_output(T326)  # MHA linear1.bias_grad
    fd.add_output(T373)  # MHA linear0.weight_grad
    fd.add_output(T376)  # MHA linear0.bias_grad
    fd.add_output(T379)  # layer_norm0.bias_grad
    fd.add_output(T383)  # layer_norm0.weight_grad
    fd.add_output(T456)  # dx output grad


def test_transformer_backward(
    benchmark, disable_validation: bool, disable_benchmarking: bool
):
    clear_cuda_cache()

    with FusionDefinition() as fd:
        transformer_backward_fusion(fd)

    inputs = [
        torch.randn(25165824, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (1, 2048, 12288), (25165824, 12288, 1)
        ),
        torch.randn(2048, dtype=torch.float32, device="cuda:0").as_strided(
            (1, 2048), (2048, 1)
        ),
        torch.randn(2048, dtype=torch.float32, device="cuda:0").as_strided(
            (1, 2048, 1), (2048, 1, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(452984832, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (36864, 12288), (12288, 1)
        ),
        torch.randn(36864, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (36864,), (1,)
        ),
        torch.randn(25165824, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (1, 96, 2048, 128), (25165824, 128, 12288, 1)
        ),
        torch.randn(150994944, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288, 12288), (12288, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        29,
        203641485758702,
        torch.randn(2048, dtype=torch.float32, device="cuda:0").as_strided(
            (1, 2048), (2048, 1)
        ),
        torch.randn(2048, dtype=torch.float32, device="cuda:0").as_strided(
            (1, 2048, 1), (2048, 1, 1)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(12288, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288,), (1,)
        ),
        torch.randn(603979776, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (49152, 12288), (12288, 1)
        ),
        torch.randn(49152, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (49152,), (1,)
        ),
        30,
        203641485758702,
        torch.randn(25165824, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (1, 2048, 12288), (25165824, 12288, 1)
        ),
        torch.randn(603979776, dtype=torch.bfloat16, device="cuda:0").as_strided(
            (12288, 49152), (49152, 1)
        ),
        torch.randn(196608, dtype=torch.float32, device="cuda:0").as_strided(
            (1, 96, 2048), (196608, 2048, 1)
        ),
        torch.randint(0, 10, (1,), dtype=torch.int64, device="cpu").as_strided((), ()),
        torch.randint(0, 10, (1,), dtype=torch.int64, device="cpu").as_strided((), ()),
    ]

    if not disable_benchmarking:
        run_benchmark(benchmark, fd.execute, inputs)
