// Include block: one per GALOSH_RAW_KERNELS entry (keep in sync
// with app/src/main/cpp/CMakeLists.txt). App-authored aggregator (not upstream).
#include "develop/galosh/GaloshShaderTable.h"

#include "galosh_galosh_bridge_norm.h"
#include "galosh_galosh_bridge_quant.h"
#include "galosh_galosh_yuv_bridge_in.h"
#include "galosh_galosh_yuv_bridge_out.h"
#include "galosh_o32_box_downsample_2x.h"
#include "galosh_o32_box_downsample_2x_3p.h"
#include "galosh_o32_box_downsample_2x_h16.h"
#include "galosh_o32_build_inv_lut.h"
#include "galosh_o32_chroma_extract_halfres.h"
#include "galosh_o32_crop_2d_topleft.h"
#include "galosh_o32_crop_2d_topleft_h16.h"
#include "galosh_o32_dark_ref_finalize_mwg.h"
#include "galosh_o32_dark_ref_reduce_mwg.h"
#include "galosh_o32_dark_resid_finalize_mwg.h"
#include "galosh_o32_dark_resid_reduce_mwg.h"
#include "galosh_o32_dark_sub_full.h"
#include "galosh_o32_fastup_3p.h"
#include "galosh_o32_fastup_3p_f16.h"
#include "galosh_o32_fastup_inverse_fused.h"
#include "galosh_o32_forward_l_stride1.h"
#include "galosh_o32_gat_forward_full.h"
#include "galosh_o32_k16_inverse_fused.h"
#include "galosh_o32_k16_jbu_3p.h"
#include "galosh_o32_k16_jbu_3p_f16.h"
#include "galosh_o32_loess_chroma_3p_tiled.h"
#include "galosh_o32_loess_chroma_3p_tiled_g16.h"
#include "galosh_o32_lpixel_lh_den_fused.h"
#include "galosh_o32_lut_finalize.h"
#include "galosh_o32_ne_block_stats.h"
#include "galosh_o32_ne_dark_finalize.h"
#include "galosh_o32_ne_dark_lap_hist.h"
#include "galosh_o32_ne_dark_thresh_finalize.h"
#include "galosh_o32_ne_dark_thresh_hist.h"
#include "galosh_o32_ne_finalize.h"
#include "galosh_o32_normalize_apply.h"
#include "galosh_o32_pad_2d_edge_3p.h"
#include "galosh_o32_pass12.h"
#include "galosh_o32_pass12_wht4.h"
#include "galosh_o32_sigma_fin_mwg.h"
#include "galosh_o32_sigma_hist_mwg.h"
#include "galosh_o32_smoothstep_blend_3p.h"
#include "galosh_o32_unified_sigma.h"
#include "galosh_yuv_env_block_stats.h"
#include "galosh_yuv_env_dark_lap_hist.h"
#include "galosh_yuv_env_dark_thresh_hist.h"
#include "galosh_yuv_env_select.h"
#include "galosh_yuv_gat_fwd.h"
#include "galosh_yuv_lap_mad.h"
#include "galosh_yuv_lap_mad_h16.h"
#include "galosh_yuv_loess.h"
#include "galosh_yuv_makitalo.h"
#include "galosh_yuv_sigma_denorm.h"
#include "galosh_yuv_sigma_norm.h"
#include "galosh_yuv_synth_alpha.h"

namespace rawrcam::develop::galosh {

::galosh::GaloshShaderMap rawShaderMap() {
    ::galosh::GaloshShaderMap map;
    map["o32_ne_block_stats"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_block_stats_spv),
                                 galosh_o32_ne_block_stats_spv_size / 4u};
    map["o32_ne_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_finalize_spv),
                              galosh_o32_ne_finalize_spv_size / 4u};
    map["o32_ne_dark_thresh_hist"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_thresh_hist_spv),
                                      galosh_o32_ne_dark_thresh_hist_spv_size / 4u};
    map["o32_ne_dark_thresh_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_thresh_finalize_spv),
                                          galosh_o32_ne_dark_thresh_finalize_spv_size / 4u};
    map["o32_ne_dark_lap_hist"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_lap_hist_spv),
                                   galosh_o32_ne_dark_lap_hist_spv_size / 4u};
    map["o32_ne_dark_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_finalize_spv),
                                   galosh_o32_ne_dark_finalize_spv_size / 4u};
    map["o32_build_inv_lut"] = {reinterpret_cast<const uint32_t*>(galosh_o32_build_inv_lut_spv),
                                galosh_o32_build_inv_lut_spv_size / 4u};
    map["o32_lut_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_lut_finalize_spv),
                               galosh_o32_lut_finalize_spv_size / 4u};
    map["o32_gat_forward_full"] = {reinterpret_cast<const uint32_t*>(galosh_o32_gat_forward_full_spv),
                                   galosh_o32_gat_forward_full_spv_size / 4u};
    map["o32_unified_sigma"] = {reinterpret_cast<const uint32_t*>(galosh_o32_unified_sigma_spv),
                                galosh_o32_unified_sigma_spv_size / 4u};
    map["o32_normalize_apply"] = {reinterpret_cast<const uint32_t*>(galosh_o32_normalize_apply_spv),
                                  galosh_o32_normalize_apply_spv_size / 4u};
    map["o32_dark_ref_reduce_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_dark_ref_reduce_mwg_spv),
                                      galosh_o32_dark_ref_reduce_mwg_spv_size / 4u};
    map["o32_dark_ref_finalize_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_dark_ref_finalize_mwg_spv),
                                        galosh_o32_dark_ref_finalize_mwg_spv_size / 4u};
    map["o32_dark_resid_reduce_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_dark_resid_reduce_mwg_spv),
                                        galosh_o32_dark_resid_reduce_mwg_spv_size / 4u};
    map["o32_dark_resid_finalize_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_dark_resid_finalize_mwg_spv),
                                          galosh_o32_dark_resid_finalize_mwg_spv_size / 4u};
    map["o32_dark_sub_full"] = {reinterpret_cast<const uint32_t*>(galosh_o32_dark_sub_full_spv),
                                galosh_o32_dark_sub_full_spv_size / 4u};
    map["o32_forward_l_stride1"] = {reinterpret_cast<const uint32_t*>(galosh_o32_forward_l_stride1_spv),
                                    galosh_o32_forward_l_stride1_spv_size / 4u};
    map["o32_chroma_extract_halfres"] = {reinterpret_cast<const uint32_t*>(galosh_o32_chroma_extract_halfres_spv),
                                         galosh_o32_chroma_extract_halfres_spv_size / 4u};
    map["o32_pass12"] = {reinterpret_cast<const uint32_t*>(galosh_o32_pass12_spv), galosh_o32_pass12_spv_size / 4u};
    map["o32_lpixel_lh_den_fused"] = {reinterpret_cast<const uint32_t*>(galosh_o32_lpixel_lh_den_fused_spv),
                                      galosh_o32_lpixel_lh_den_fused_spv_size / 4u};
    map["o32_box_downsample_2x"] = {reinterpret_cast<const uint32_t*>(galosh_o32_box_downsample_2x_spv),
                                    galosh_o32_box_downsample_2x_spv_size / 4u};
    map["o32_box_downsample_2x_3p"] = {reinterpret_cast<const uint32_t*>(galosh_o32_box_downsample_2x_3p_spv),
                                       galosh_o32_box_downsample_2x_3p_spv_size / 4u};
    map["o32_loess_chroma_3p_tiled"] = {reinterpret_cast<const uint32_t*>(galosh_o32_loess_chroma_3p_tiled_spv),
                                        galosh_o32_loess_chroma_3p_tiled_spv_size / 4u};
    map["o32_crop_2d_topleft"] = {reinterpret_cast<const uint32_t*>(galosh_o32_crop_2d_topleft_spv),
                                  galosh_o32_crop_2d_topleft_spv_size / 4u};
    map["o32_k16_jbu_3p"] = {reinterpret_cast<const uint32_t*>(galosh_o32_k16_jbu_3p_spv),
                             galosh_o32_k16_jbu_3p_spv_size / 4u};
    map["o32_smoothstep_blend_3p"] = {reinterpret_cast<const uint32_t*>(galosh_o32_smoothstep_blend_3p_spv),
                                      galosh_o32_smoothstep_blend_3p_spv_size / 4u};
    map["o32_pass12_wht4"] = {reinterpret_cast<const uint32_t*>(galosh_o32_pass12_wht4_spv),
                              galosh_o32_pass12_wht4_spv_size / 4u};
    map["o32_fastup_3p"] = {reinterpret_cast<const uint32_t*>(galosh_o32_fastup_3p_spv),
                            galosh_o32_fastup_3p_spv_size / 4u};
    map["o32_box_downsample_2x_h16"] = {reinterpret_cast<const uint32_t*>(galosh_o32_box_downsample_2x_h16_spv),
                                        galosh_o32_box_downsample_2x_h16_spv_size / 4u};
    map["o32_crop_2d_topleft_h16"] = {reinterpret_cast<const uint32_t*>(galosh_o32_crop_2d_topleft_h16_spv),
                                      galosh_o32_crop_2d_topleft_h16_spv_size / 4u};
    map["o32_loess_chroma_3p_tiled_g16"] = {reinterpret_cast<const uint32_t*>(galosh_o32_loess_chroma_3p_tiled_g16_spv),
                                            galosh_o32_loess_chroma_3p_tiled_g16_spv_size / 4u};
    map["o32_k16_jbu_3p_f16"] = {reinterpret_cast<const uint32_t*>(galosh_o32_k16_jbu_3p_f16_spv),
                                 galosh_o32_k16_jbu_3p_f16_spv_size / 4u};
    map["o32_fastup_3p_f16"] = {reinterpret_cast<const uint32_t*>(galosh_o32_fastup_3p_f16_spv),
                                galosh_o32_fastup_3p_f16_spv_size / 4u};
    map["o32_sigma_hist_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_sigma_hist_mwg_spv),
                                 galosh_o32_sigma_hist_mwg_spv_size / 4u};
    map["o32_sigma_fin_mwg"] = {reinterpret_cast<const uint32_t*>(galosh_o32_sigma_fin_mwg_spv),
                                galosh_o32_sigma_fin_mwg_spv_size / 4u};
    map["o32_pad_2d_edge_3p"] = {reinterpret_cast<const uint32_t*>(galosh_o32_pad_2d_edge_3p_spv),
                                 galosh_o32_pad_2d_edge_3p_spv_size / 4u};
    map["o32_k16_inverse_fused"] = {reinterpret_cast<const uint32_t*>(galosh_o32_k16_inverse_fused_spv),
                                    galosh_o32_k16_inverse_fused_spv_size / 4u};
    map["o32_fastup_inverse_fused"] = {reinterpret_cast<const uint32_t*>(galosh_o32_fastup_inverse_fused_spv),
                                       galosh_o32_fastup_inverse_fused_spv_size / 4u};
    map["galosh_bridge_norm"] = {reinterpret_cast<const uint32_t*>(galosh_galosh_bridge_norm_spv),
                                 galosh_galosh_bridge_norm_spv_size / 4u};
    map["galosh_bridge_quant"] = {reinterpret_cast<const uint32_t*>(galosh_galosh_bridge_quant_spv),
                                  galosh_galosh_bridge_quant_spv_size / 4u};
    return map;
}

::galosh::GaloshShaderMap yuvShaderMap() {
    ::galosh::GaloshShaderMap map;
    // Shared o32_* helpers (also in rawShaderMap; same blobs).
    map["o32_build_inv_lut"] = {reinterpret_cast<const uint32_t*>(galosh_o32_build_inv_lut_spv),
                                galosh_o32_build_inv_lut_spv_size / 4u};
    map["o32_lut_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_lut_finalize_spv),
                               galosh_o32_lut_finalize_spv_size / 4u};
    map["o32_pass12"] = {reinterpret_cast<const uint32_t*>(galosh_o32_pass12_spv), galosh_o32_pass12_spv_size / 4u};
    map["o32_ne_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_finalize_spv),
                              galosh_o32_ne_finalize_spv_size / 4u};
    map["o32_ne_dark_thresh_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_thresh_finalize_spv),
                                          galosh_o32_ne_dark_thresh_finalize_spv_size / 4u};
    map["o32_ne_dark_finalize"] = {reinterpret_cast<const uint32_t*>(galosh_o32_ne_dark_finalize_spv),
                                   galosh_o32_ne_dark_finalize_spv_size / 4u};
    map["yuv_lap_mad"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_lap_mad_spv), galosh_yuv_lap_mad_spv_size / 4u};
    map["yuv_lap_mad_h16"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_lap_mad_h16_spv),
                              galosh_yuv_lap_mad_h16_spv_size / 4u};
    map["yuv_synth_alpha"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_synth_alpha_spv),
                              galosh_yuv_synth_alpha_spv_size / 4u};
    map["yuv_gat_fwd"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_gat_fwd_spv), galosh_yuv_gat_fwd_spv_size / 4u};
    map["yuv_sigma_norm"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_sigma_norm_spv),
                             galosh_yuv_sigma_norm_spv_size / 4u};
    map["yuv_sigma_denorm"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_sigma_denorm_spv),
                               galosh_yuv_sigma_denorm_spv_size / 4u};
    map["yuv_makitalo"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_makitalo_spv),
                           galosh_yuv_makitalo_spv_size / 4u};
    map["yuv_loess"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_loess_spv), galosh_yuv_loess_spv_size / 4u};
    map["yuv_env_block_stats"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_env_block_stats_spv),
                                  galosh_yuv_env_block_stats_spv_size / 4u};
    map["yuv_env_select"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_env_select_spv),
                             galosh_yuv_env_select_spv_size / 4u};
    map["yuv_env_dark_thresh_hist"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_env_dark_thresh_hist_spv),
                                       galosh_yuv_env_dark_thresh_hist_spv_size / 4u};
    map["yuv_env_dark_lap_hist"] = {reinterpret_cast<const uint32_t*>(galosh_yuv_env_dark_lap_hist_spv),
                                    galosh_yuv_env_dark_lap_hist_spv_size / 4u};
    map["galosh_yuv_bridge_in"] = {reinterpret_cast<const uint32_t*>(galosh_galosh_yuv_bridge_in_spv),
                                   galosh_galosh_yuv_bridge_in_spv_size / 4u};
    map["galosh_yuv_bridge_out"] = {reinterpret_cast<const uint32_t*>(galosh_galosh_yuv_bridge_out_spv),
                                    galosh_galosh_yuv_bridge_out_spv_size / 4u};
    return map;
}

}  // namespace rawrcam::develop::galosh
