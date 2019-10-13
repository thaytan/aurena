
#include "odas-objects.h"

void
odas_objects_construct (odas_objects * objs, const odas_configs * cfgs)
{
  objs->src_hops_mics_object = src_hops_construct (cfgs->src_hops_mics_config,
      cfgs->msg_hops_mics_raw_config);

  objs->con_hops_mics_raw_object =
      con_hops_construct (1, cfgs->msg_hops_mics_raw_config);

  objs->mod_mapping_mics_object =
      mod_mapping_construct (cfgs->mod_mapping_mics_config,
      cfgs->msg_hops_mics_map_config);

  objs->con_hops_mics_map_object =
      con_hops_construct (1, cfgs->msg_hops_mics_map_config);

  objs->mod_resample_mics_object =
      mod_resample_construct (cfgs->mod_resample_mics_config,
      cfgs->msg_hops_mics_map_config, cfgs->msg_hops_mics_rs_config);

  objs->con_hops_mics_rs_object =
      con_hops_construct (2, cfgs->msg_hops_mics_rs_config);

  objs->mod_stft_mics_object = mod_stft_construct (cfgs->mod_stft_mics_config,
      cfgs->msg_hops_mics_rs_config, cfgs->msg_spectra_mics_config);

  objs->con_spectra_mics_object =
      con_spectra_construct (3, cfgs->msg_spectra_mics_config);

  objs->mod_noise_mics_object =
      mod_noise_construct (cfgs->mod_noise_mics_config,
      cfgs->msg_spectra_mics_config, cfgs->msg_powers_mics_config);

  objs->con_powers_mics_object =
      con_powers_construct (1, cfgs->msg_powers_mics_config);

  objs->mod_ssl_object = mod_ssl_construct (cfgs->mod_ssl_config,
      cfgs->msg_spectra_mics_config, cfgs->msg_pots_ssl_config);

  objs->snk_pots_ssl_object = snk_pots_construct (cfgs->snk_pots_ssl_config,
      cfgs->msg_pots_ssl_config);

  objs->con_pots_ssl_object = con_pots_construct (2, cfgs->msg_pots_ssl_config);

  objs->inj_targets_sst_object =
      inj_targets_construct (cfgs->inj_targets_sst_config,
      cfgs->msg_hops_mics_rs_config, cfgs->msg_targets_sst_config);

  objs->con_targets_sst_object =
      con_targets_construct (1, cfgs->msg_targets_sst_config);

  objs->mod_sst_object = mod_sst_construct (cfgs->mod_sst_config,
      cfgs->mod_ssl_config,
      cfgs->msg_pots_ssl_config,
      cfgs->msg_targets_sst_config, cfgs->msg_tracks_sst_config);

  objs->snk_tracks_sst_object =
      snk_tracks_construct (cfgs->snk_tracks_sst_config,
      cfgs->msg_tracks_sst_config);

  objs->con_tracks_sst_object =
      con_tracks_construct (3, cfgs->msg_tracks_sst_config);

  objs->mod_sss_object = mod_sss_construct (cfgs->mod_sss_config,
      cfgs->msg_tracks_sst_config, cfgs->msg_spectra_mics_config);

  objs->con_spectra_seps_object =
      con_spectra_construct (1, cfgs->msg_spectra_seps_config);
  objs->con_spectra_pfs_object =
      con_spectra_construct (1, cfgs->msg_spectra_pfs_config);

  objs->mod_istft_seps_object =
      mod_istft_construct (cfgs->mod_istft_seps_config,
      cfgs->msg_spectra_seps_config, cfgs->msg_hops_seps_config);

  objs->mod_istft_pfs_object = mod_istft_construct (cfgs->mod_istft_pfs_config,
      cfgs->msg_spectra_pfs_config, cfgs->msg_hops_pfs_config);

  objs->con_hops_seps_object =
      con_hops_construct (2, cfgs->msg_hops_seps_config);
  objs->con_hops_pfs_object = con_hops_construct (1, cfgs->msg_hops_pfs_config);

  objs->mod_resample_seps_object =
      mod_resample_construct (cfgs->mod_resample_seps_config,
      cfgs->msg_hops_seps_config, cfgs->msg_hops_seps_rs_config);

  objs->mod_resample_pfs_object =
      mod_resample_construct (cfgs->mod_resample_pfs_config,
      cfgs->msg_hops_pfs_config, cfgs->msg_hops_pfs_rs_config);

  objs->con_hops_seps_rs_object =
      con_hops_construct (1, cfgs->msg_hops_seps_rs_config);
  objs->con_hops_pfs_rs_object =
      con_hops_construct (1, cfgs->msg_hops_pfs_rs_config);

  objs->mod_volume_seps_object =
      mod_volume_construct (cfgs->mod_volume_seps_config,
      cfgs->msg_hops_seps_vol_config);

  objs->mod_volume_pfs_object =
      mod_volume_construct (cfgs->mod_volume_pfs_config,
      cfgs->msg_hops_pfs_vol_config);

  objs->con_hops_seps_vol_object =
      con_hops_construct (1, cfgs->msg_hops_seps_vol_config);
  objs->con_hops_pfs_vol_object =
      con_hops_construct (1, cfgs->msg_hops_pfs_vol_config);

  objs->snk_hops_seps_vol_object =
      snk_hops_construct (cfgs->snk_hops_seps_vol_config,
      cfgs->msg_hops_seps_vol_config);

  objs->snk_hops_pfs_vol_object =
      snk_hops_construct (cfgs->snk_hops_pfs_vol_config,
      cfgs->msg_hops_pfs_vol_config);

  objs->mod_classify_object = mod_classify_construct (cfgs->mod_classify_config,
      cfgs->msg_hops_seps_config,
      cfgs->msg_tracks_sst_config, cfgs->msg_categories_config);

  objs->con_categories_object =
      con_categories_construct (1, cfgs->msg_categories_config);

  objs->snk_categories_object =
      snk_categories_construct (cfgs->snk_categories_config,
      cfgs->msg_categories_config);

  src_hops_connect (objs->src_hops_mics_object,
      objs->con_hops_mics_raw_object->in);

  mod_mapping_connect (objs->mod_mapping_mics_object,
      objs->con_hops_mics_raw_object->outs[0],
      objs->con_hops_mics_map_object->in);

  mod_resample_connect (objs->mod_resample_mics_object,
      objs->con_hops_mics_map_object->outs[0],
      objs->con_hops_mics_rs_object->in);

  mod_stft_connect (objs->mod_stft_mics_object,
      objs->con_hops_mics_rs_object->outs[0],
      objs->con_spectra_mics_object->in);

  mod_noise_connect (objs->mod_noise_mics_object,
      objs->con_spectra_mics_object->outs[0], objs->con_powers_mics_object->in);

  mod_ssl_connect (objs->mod_ssl_object,
      objs->con_spectra_mics_object->outs[1], objs->con_pots_ssl_object->in);

  snk_pots_connect (objs->snk_pots_ssl_object,
      objs->con_pots_ssl_object->outs[1]);

  inj_targets_connect (objs->inj_targets_sst_object,
      objs->con_hops_mics_rs_object->outs[1], objs->con_targets_sst_object->in);

  mod_sst_connect (objs->mod_sst_object,
      objs->con_pots_ssl_object->outs[0],
      objs->con_targets_sst_object->outs[0], objs->con_tracks_sst_object->in);

  snk_tracks_connect (objs->snk_tracks_sst_object,
      objs->con_tracks_sst_object->outs[2]);

  mod_sss_connect (objs->mod_sss_object,
      objs->con_spectra_mics_object->outs[2],
      objs->con_powers_mics_object->outs[0],
      objs->con_tracks_sst_object->outs[0],
      objs->con_spectra_seps_object->in, objs->con_spectra_pfs_object->in);

  mod_istft_connect (objs->mod_istft_seps_object,
      objs->con_spectra_seps_object->outs[0], objs->con_hops_seps_object->in);

  mod_istft_connect (objs->mod_istft_pfs_object,
      objs->con_spectra_pfs_object->outs[0], objs->con_hops_pfs_object->in);

  mod_resample_connect (objs->mod_resample_seps_object,
      objs->con_hops_seps_object->outs[0], objs->con_hops_seps_rs_object->in);

  mod_resample_connect (objs->mod_resample_pfs_object,
      objs->con_hops_pfs_object->outs[0], objs->con_hops_pfs_rs_object->in);

  mod_volume_connect (objs->mod_volume_seps_object,
      objs->con_hops_seps_rs_object->outs[0],
      objs->con_hops_seps_vol_object->in);

  mod_volume_connect (objs->mod_volume_pfs_object,
      objs->con_hops_pfs_rs_object->outs[0], objs->con_hops_pfs_vol_object->in);

  snk_hops_connect (objs->snk_hops_seps_vol_object,
      objs->con_hops_seps_vol_object->outs[0]);

  snk_hops_connect (objs->snk_hops_pfs_vol_object,
      objs->con_hops_pfs_vol_object->outs[0]);

  mod_classify_connect (objs->mod_classify_object,
      objs->con_hops_seps_object->outs[1],
      objs->con_tracks_sst_object->outs[1], objs->con_categories_object->in);

  snk_categories_connect (objs->snk_categories_object,
      objs->con_categories_object->outs[0]);

  mod_mapping_enable (objs->mod_mapping_mics_object);
  mod_resample_enable (objs->mod_resample_mics_object);
  mod_stft_enable (objs->mod_stft_mics_object);

  if (cfgs->snk_pots_ssl_config->interface->type != interface_blackhole) {
    mod_ssl_enable (objs->mod_ssl_object);
  }

  if (cfgs->snk_tracks_sst_config->interface->type != interface_blackhole) {
    mod_ssl_enable (objs->mod_ssl_object);
    mod_sst_enable (objs->mod_sst_object);
  }

  if (cfgs->snk_hops_seps_vol_config->interface->type != interface_blackhole) {
    mod_ssl_enable (objs->mod_ssl_object);
    mod_sst_enable (objs->mod_sst_object);
    mod_noise_enable (objs->mod_noise_mics_object);
    mod_sss_enable (objs->mod_sss_object);
    mod_istft_enable (objs->mod_istft_seps_object);
    mod_resample_enable (objs->mod_resample_seps_object);
    mod_volume_enable (objs->mod_volume_seps_object);
  }

  if (cfgs->snk_hops_pfs_vol_config->interface->type != interface_blackhole) {
    mod_ssl_enable (objs->mod_ssl_object);
    mod_sst_enable (objs->mod_sst_object);
    mod_noise_enable (objs->mod_noise_mics_object);
    mod_sss_enable (objs->mod_sss_object);
    mod_istft_enable (objs->mod_istft_pfs_object);
    mod_resample_enable (objs->mod_resample_pfs_object);
    mod_volume_enable (objs->mod_volume_pfs_object);
  }

  if (cfgs->snk_categories_config->interface->type != interface_blackhole) {
    mod_ssl_enable (objs->mod_ssl_object);
    mod_sst_enable (objs->mod_sst_object);
    mod_noise_enable (objs->mod_noise_mics_object);
    mod_sss_enable (objs->mod_sss_object);
    mod_istft_enable (objs->mod_istft_seps_object);
    mod_classify_enable (objs->mod_classify_object);
  }
}

void
objects_destroy (odas_objects * objs)
{
  src_hops_destroy (objs->src_hops_mics_object);

  con_hops_destroy (objs->con_hops_mics_raw_object);

  mod_mapping_destroy (objs->mod_mapping_mics_object);

  con_hops_destroy (objs->con_hops_mics_map_object);

  mod_resample_destroy (objs->mod_resample_mics_object);

  con_hops_destroy (objs->con_hops_mics_rs_object);

  mod_stft_destroy (objs->mod_stft_mics_object);

  con_spectra_destroy (objs->con_spectra_mics_object);

  mod_noise_destroy (objs->mod_noise_mics_object);

  con_powers_destroy (objs->con_powers_mics_object);

  mod_ssl_destroy (objs->mod_ssl_object);

  snk_pots_destroy (objs->snk_pots_ssl_object);

  con_pots_destroy (objs->con_pots_ssl_object);

  inj_targets_destroy (objs->inj_targets_sst_object);

  con_targets_destroy (objs->con_targets_sst_object);

  mod_sst_destroy (objs->mod_sst_object);

  snk_tracks_destroy (objs->snk_tracks_sst_object);

  con_tracks_destroy (objs->con_tracks_sst_object);

  mod_sss_destroy (objs->mod_sss_object);

  con_spectra_destroy (objs->con_spectra_seps_object);
  con_spectra_destroy (objs->con_spectra_pfs_object);

  mod_istft_destroy (objs->mod_istft_seps_object);
  mod_istft_destroy (objs->mod_istft_pfs_object);

  con_hops_destroy (objs->con_hops_seps_object);
  con_hops_destroy (objs->con_hops_pfs_object);

  mod_resample_destroy (objs->mod_resample_seps_object);
  mod_resample_destroy (objs->mod_resample_pfs_object);

  con_hops_destroy (objs->con_hops_seps_rs_object);
  con_hops_destroy (objs->con_hops_pfs_rs_object);

  mod_volume_destroy (objs->mod_volume_seps_object);
  mod_volume_destroy (objs->mod_volume_pfs_object);

  con_hops_destroy (objs->con_hops_seps_vol_object);
  con_hops_destroy (objs->con_hops_pfs_vol_object);

  snk_hops_destroy (objs->snk_hops_seps_vol_object);
  snk_hops_destroy (objs->snk_hops_pfs_vol_object);

  mod_classify_destroy (objs->mod_classify_object);

  con_categories_destroy (objs->con_categories_object);

  snk_categories_destroy (objs->snk_categories_object);
}
