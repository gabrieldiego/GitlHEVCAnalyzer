##
## Copyright (c) 2017, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 2 Clause License and
## the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
## was not distributed with this source code in the LICENSE file, you can
## obtain it at www.aomedia.org/license/software. If the Alliance for Open
## Media Patent License 1.0 was not distributed with this source code in the
## PATENTS file, you can obtain it at www.aomedia.org/license/patent.
##
if (NOT AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_)
set(AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_ 1)

# Adjusts CONFIG_* CMake variables to address conflicts between active AV1
# experiments.
macro (fix_experiment_configs)
  if (CONFIG_AMVR)
    if (NOT CONFIG_HASH_ME)
      change_config_and_warn(CONFIG_HASH_ME 1 CONFIG_AMVR)
    endif ()
  endif ()

  if (CONFIG_ANALYZER)
    if (NOT CONFIG_INSPECTION)
      change_config_and_warn(CONFIG_INSPECTION 1 CONFIG_ANALYZER)
    endif ()
  endif ()

  if (CONFIG_EOB_FIRST)
    if (NOT CONFIG_LV_MAP)
      change_config_and_warn(CONFIG_LV_MAP 1 CONFIG_EOB_FIRST)
    endif ()
  endif ()

  if (CONFIG_DAALA_TX)
     set(CONFIG_DAALA_TX_DST32 1)
     set(CONFIG_DAALA_TX4 1)
     set(CONFIG_DAALA_TX8 1)
     set(CONFIG_DAALA_TX16 1)
     set(CONFIG_DAALA_TX32 1)
     set(CONFIG_DAALA_TX64 1)
  endif ()

  if (NOT CONFIG_DAALA_TX)
     set(CONFIG_DAALA_TX_DST32 0)
     set(CONFIG_DAALA_TX4 0)
     set(CONFIG_DAALA_TX8 0)
     set(CONFIG_DAALA_TX16 0)
     set(CONFIG_DAALA_TX32 0)
     set(CONFIG_DAALA_TX64 0)
  endif ()

  if (CONFIG_DAALA_TX_DST8)
    if (NOT CONFIG_DAALA_TX8)
      set(CONFIG_DAALA_TX_DST8 0)
      message("--- DAALA_TX_DST8 requires DAALA_TX8: disabled DAALA_TX_DST8")
    endif ()
  endif ()

  if (CONFIG_DAALA_TX_DST32)
    if (NOT CONFIG_DAALA_TX32)
      set(CONFIG_DAALA_TX_DST32 0)
      message("--- DAALA_TX_DST32 requires DAALA_TX32: disabled DAALA_TX_DST32")
    endif ()
  endif ()

  if (CONFIG_DAALA_TX64)
    if (NOT CONFIG_TX64X64)
      set(CONFIG_DAALA_TX64 0)
      message("--- DAALA_TX64 requires TX64X64: disabled DAALA_TX64")
    endif ()
  endif ()

  if (CONFIG_DAALA_TX4 OR CONFIG_DAALA_TX8 OR CONFIG_DAALA_TX16 OR
      CONFIG_DAALA_TX32 OR CONFIG_DAALA_TX64)
    if (NOT CONFIG_LOWBITDEPTH)
      change_config_and_warn(CONFIG_LOWBITDEPTH 1 CONFIG_DAALA_TXx)
    endif ()
    if (CONFIG_TXMG)
      change_config_and_warn(CONFIG_TXMG 0 CONFIG_DAALA_DCTx)
    endif ()
  endif ()

  if (CONFIG_EXT_INTRA_MOD)
    if (NOT CONFIG_INTRA_EDGE)
      change_config_and_warn(CONFIG_INTRA_EDGE 1 CONFIG_EXT_INTRA_MOD)
    endif ()
    if (NOT CONFIG_EXT_INTRA)
      change_config_and_warn(CONFIG_EXT_INTRA 1 CONFIG_EXT_INTRA_MOD)
    endif ()
  endif ()

  if (CONFIG_EXT_PARTITION_TYPES)
    if (CONFIG_FP_MB_STATS)
      change_config_and_warn(CONFIG_FP_MB_STATS 0 CONFIG_EXT_PARTITION_TYPES)
    endif ()
  endif ()

  if (CONFIG_EXT_SKIP)
    if (NOT CONFIG_FRAME_MARKER)
      change_config_and_warn(CONFIG_FRAME_MARKER 1 CONFIG_EXT_SKIP)
    endif ()
  endif ()

  if (CONFIG_FRAME_SIGN_BIAS)
    if (NOT CONFIG_FRAME_MARKER)
      change_config_and_warn(CONFIG_FRAME_MARKER 1 CONFIG_FRAME_SIGN_BIAS)
    endif ()
  endif ()

  if (CONFIG_INTRA_EDGE)
    if (NOT CONFIG_EXT_INTRA)
      change_config_and_warn(CONFIG_EXT_INTRA 1 CONFIG_INTRA_EDGE)
    endif ()
  endif ()

  if (CONFIG_JNT_COMP)
    if (NOT CONFIG_FRAME_MARKER)
      change_config_and_warn(CONFIG_FRAME_MARKER 1 CONFIG_JNT_COMP)
    endif ()
  endif ()

  if (CONFIG_LOOPFILTER_LEVEL)
    if (NOT CONFIG_EXT_DELTA_Q)
      change_config_and_warn(CONFIG_EXT_DELTA_Q 1 CONFIG_LOOPFILTER_LEVEL)
    endif  ()
    if (NOT CONFIG_PARALLEL_DEBLOCKING)
      change_config_and_warn(CONFIG_PARALLEL_DEBLOCKING 1 CONFIG_LOOPFILTER_LEVEL)
    endif  ()
  endif ()

  if (CONFIG_MFMV)
    if (NOT CONFIG_FRAME_MARKER)
      change_config_and_warn(CONFIG_FRAME_MARKER 1 CONFIG_MFMV)
    endif ()
  endif ()

  if (CONFIG_STRIPED_LOOP_RESTORATION)
    if (NOT CONFIG_LOOP_RESTORATION)
      change_config_and_warn(CONFIG_LOOP_RESTORATION 1 CONFIG_STRIPED_LOOP_RESTORATION)
    endif ()
  endif ()

  if (CONFIG_TXK_SEL)
    if (NOT CONFIG_LV_MAP)
      change_config_and_warn(CONFIG_LV_MAP 1 CONFIG_TXK_SEL)
    endif ()
  endif ()
endmacro ()

endif ()  # AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_
