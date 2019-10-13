#ifndef __ODAS_OBJECTS__
#define __ODAS_OBJECTS__

#include <odas/odas.h>

#include "odas-configs.h"

typedef struct odas_objects odas_objects;

struct odas_objects {
    src_hops_obj * src_hops_mics_object;
    con_hops_obj * con_hops_mics_raw_object; 

    mod_mapping_obj * mod_mapping_mics_object;
    con_hops_obj * con_hops_mics_map_object;              

    mod_resample_obj * mod_resample_mics_object;
    con_hops_obj * con_hops_mics_rs_object;

    mod_stft_obj * mod_stft_mics_object;
    con_spectra_obj * con_spectra_mics_object;

    mod_noise_obj * mod_noise_mics_object;
    con_powers_obj * con_powers_mics_object;

    mod_ssl_obj * mod_ssl_object;
    snk_pots_obj * snk_pots_ssl_object;
    con_pots_obj * con_pots_ssl_object;

    inj_targets_obj * inj_targets_sst_object;
    con_targets_obj * con_targets_sst_object;

    mod_sst_obj * mod_sst_object;
    snk_tracks_obj * snk_tracks_sst_object;
    con_tracks_obj * con_tracks_sst_object;

    mod_sss_obj * mod_sss_object;
    con_spectra_obj * con_spectra_seps_object;
    con_spectra_obj * con_spectra_pfs_object;

    mod_istft_obj * mod_istft_seps_object;
    mod_istft_obj * mod_istft_pfs_object;
    con_hops_obj * con_hops_seps_object;
    con_hops_obj * con_hops_pfs_object;

    mod_resample_obj * mod_resample_seps_object;
    mod_resample_obj * mod_resample_pfs_object;
    con_hops_obj * con_hops_seps_rs_object;
    con_hops_obj * con_hops_pfs_rs_object;

    mod_volume_obj * mod_volume_seps_object;
    mod_volume_obj * mod_volume_pfs_object;
    con_hops_obj * con_hops_seps_vol_object;
    con_hops_obj * con_hops_pfs_vol_object;
    snk_hops_obj * snk_hops_seps_vol_object;
    snk_hops_obj * snk_hops_pfs_vol_object;

    mod_classify_obj * mod_classify_object;
    con_categories_obj * con_categories_object;
    snk_categories_obj * snk_categories_object;
};   

void odas_objects_construct(odas_objects *objs, const odas_configs* cfg);
void odas_objects_destroy(odas_objects * objs);

#endif
