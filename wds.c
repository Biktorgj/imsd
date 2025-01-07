/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "wds.h"
#include "dcm.h"
#include "imsd.h"
#include <arpa/inet.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <netinet/in.h>
#define VALIDATE_MASK_NONE(str) (str ? str : "none")
#define QMI_WDS_IP_FAMILY_IPV4 4

/*
 * Let's put some order in here
 *
 *
 *
 */
/* Context */
typedef struct {
  QmiDevice *device;
  GCancellable *cancellable;
} Context;
static Context *ctx;

/*
 * Helpers
 */

guint8 wds_get_readiness_step(_WDS_Client *wds_client) {
  return wds_client->packet_session.connection_readiness_step;
}

guint32 wds_get_packet_handle(_WDS_Client *wds_client) {
  return wds_client->packet_session.packet_data_handle;
}

guint8 wds_get_profile_id(_WDS_Client *wds_client) {
  return wds_client->packet_session.profile_id;
}

guint8 wds_get_mux_id(_WDS_Client *wds_client) {
  return wds_client->packet_session.mux_id;
}

/*
 * BEGIN: Profile handling
 */


static void wds_get_profile_settings_ready(QmiClientWds *client, GAsyncResult *res,
                                       gpointer user_data) {
  QmiMessageWdsGetProfileSettingsOutput *output;
  QmiMessageWdsGetProfileListOutputProfileListProfile *profile;

  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  _Profile_List *inner_ctx = wds_client->profile_list;
  
  profile = &g_array_index(inner_ctx->profile_list,
                           QmiMessageWdsGetProfileListOutputProfileListProfile,
                           inner_ctx->i);
  GError *error = NULL;
  g_printerr(" Sim Slot %u: Profile %i\n", wds_client->slot_id, profile->profile_index);

  output = qmi_client_wds_get_profile_settings_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
  } else if (!qmi_message_wds_get_profile_settings_output_get_result(output,
                                                                     &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_get_profile_settings_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't get profile settings: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't get profile settings: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_get_profile_settings_output_unref(output);
  } else {
    const gchar *str;
    guint8 context_number;
    QmiWdsPdpType pdp_type;
    QmiWdsAuthentication auth;
    QmiWdsApnTypeMask apn_type;
    gboolean flag;

    if (qmi_message_wds_get_profile_settings_output_get_apn_name(output, &str,
                                                                 NULL))
      g_print("  -->APN: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_apn_type_mask(
            output, &apn_type, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_apn_type_mask_build_string_from_mask(apn_type);
      g_print("  -->APN type: '%s'\n", VALIDATE_MASK_NONE(aux));
      if (apn_type == APN_TYPE_MASK_IMS) {
        g_printerr("    --> We found a valid APN type: %i \n",
                   profile->profile_index);
        wds_client->packet_session.profile_id = profile->profile_index;
        wds_client->packet_session.connection_readiness_step =
            WDS_CONNECTION_STATE_PROFILE_READY;
      }
    }
    if (qmi_message_wds_get_profile_settings_output_get_pdp_type(
            output, &pdp_type, NULL))
      g_print("  -->PDP type: '%s'\n", qmi_wds_pdp_type_get_string(pdp_type));
    if (qmi_message_wds_get_profile_settings_output_get_pdp_context_number(
            output, &context_number, NULL))
      g_print("  -->PDP context number: '%d'\n", context_number);
    if (qmi_message_wds_get_profile_settings_output_get_username(output, &str,
                                                                 NULL))
      g_print("  -->Username: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_password(output, &str,
                                                                 NULL))
      g_print("  -->Password: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_authentication(
            output, &auth, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_authentication_build_string_from_mask(auth);
      g_print("  -->Auth: '%s'\n", VALIDATE_MASK_NONE(aux));
    }
    if (qmi_message_wds_get_profile_settings_output_get_roaming_disallowed_flag(
            output, &flag, NULL))
      g_print("  -->No roaming: '%s'\n", flag ? "yes" : "no");
    if (qmi_message_wds_get_profile_settings_output_get_apn_disabled_flag(
            output, &flag, NULL))
      g_print("  -->APN disabled: '%s'\n", flag ? "yes" : "no");
    qmi_message_wds_get_profile_settings_output_unref(output);
  }

  /* Keep on */
  inner_ctx->i++;
  wds_get_next_profile_settings(wds_client, inner_ctx);
}

void wds_get_next_profile_settings(_WDS_Client *wds_client, _Profile_List *inner_ctx) {
  QmiMessageWdsGetProfileListOutputProfileListProfile *profile;
  QmiMessageWdsGetProfileSettingsInput *input;

  if (inner_ctx->i >= inner_ctx->profile_list->len) {
    /* All done */
    g_print("We reached the end of the profiles\n");
    if (wds_client->packet_session.profile_id == 0) {
      wds_client->packet_session.connection_readiness_step =
          WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY;
    }
    g_array_unref(inner_ctx->profile_list);
    g_slice_free(_Profile_List, inner_ctx);
    return;
  }

  profile = &g_array_index(inner_ctx->profile_list,
                           QmiMessageWdsGetProfileListOutputProfileListProfile,
                           inner_ctx->i);
  g_print(" [%u] %s - %s\n", profile->profile_index,
          qmi_wds_profile_type_get_string(profile->profile_type),
          profile->profile_name);

  input = qmi_message_wds_get_profile_settings_input_new();
  qmi_message_wds_get_profile_settings_input_set_profile_id(
      input, profile->profile_type, profile->profile_index, NULL);
  qmi_client_wds_get_profile_settings(
      QMI_CLIENT_WDS(wds_client->wds), input, 3, NULL,
      (GAsyncReadyCallback)wds_get_profile_settings_ready, wds_client);
  qmi_message_wds_get_profile_settings_input_unref(input);
}

static void wds_get_profile_list_ready(QmiClientWds *client, GAsyncResult *res,
                                   gpointer user_data) {
  GError *error = NULL;
  QmiMessageWdsGetProfileListOutput *output;
  GArray *profile_list = NULL;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;

  output = qmi_client_wds_get_profile_list_finish(client, res, &error);
  if (!output) {
    g_printerr("[WDS][%s] Fatal: operation failed: %s\n",__func__, error->message);
    g_error_free(error);
    return;
  }
  if (!qmi_message_wds_get_profile_list_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_get_profile_list_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't get profile list: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't get profile list: %s\n", error->message);
    }

    g_error_free(error);
    qmi_message_wds_get_profile_list_output_unref(output);
    return;
  }

  qmi_message_wds_get_profile_list_output_get_profile_list(output,
                                                           &profile_list, NULL);

  if (!profile_list || !profile_list->len) {
    g_printerr("Profile list empty\n");
    wds_client->packet_session.connection_readiness_step =
        WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY;
    qmi_message_wds_get_profile_list_output_unref(output);
    return;
  }

  g_debug("Profile list retrieved:\n");

  wds_client->profile_list = g_slice_new(_Profile_List);
  wds_client->profile_list->profile_list = g_array_ref(profile_list);
  wds_client->profile_list->i = 0;
  wds_get_next_profile_settings(wds_client, wds_client->profile_list);
}

void wds_get_profile_list(_WDS_Client *wds_client, uint8_t profile_type) {
  QmiMessageWdsGetProfileListInput *input;
  input = qmi_message_wds_get_profile_list_input_new();
  switch (profile_type) {
  case 0: // 3gpp
    g_print("[WDS] Retrieve 3GPP Profiles\n");
    qmi_message_wds_get_profile_list_input_set_profile_type(input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);
    qmi_client_wds_get_profile_list(QMI_CLIENT_WDS(wds_client->wds), input, 10, ctx->cancellable,(GAsyncReadyCallback)wds_get_profile_list_ready, wds_client);
    qmi_message_wds_get_profile_list_input_unref(input);
    break;
  case 1: // 3gpp2
    g_print("[WDS] Retrieve 3GPP2 Profiles\n");
    qmi_message_wds_get_profile_list_input_set_profile_type(input, QMI_WDS_PROFILE_TYPE_3GPP2, NULL);
    qmi_client_wds_get_profile_list(QMI_CLIENT_WDS(wds_client->wds), input, 10, ctx->cancellable,(GAsyncReadyCallback)wds_get_profile_list_ready, wds_client);
    qmi_message_wds_get_profile_list_input_unref(input);
    break;
  default:
    g_printerr("[WDS] Error retrieiving profiles: invalid type %u\n",
               profile_type);
    break;
  }

  return;
}

/* Add new Profile callback */
static void wds_add_new_profile_ready(QmiClientWds *client, GAsyncResult *res, gpointer user_data) {
  QmiMessageWdsCreateProfileOutput *output;
  GError *error = NULL;
  QmiWdsProfileType profile_type;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;

  output = qmi_client_wds_create_profile_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_create_profile_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_create_profile_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't create profile: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't create profile: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_create_profile_output_unref(output);
    return;
  }

  g_print("New profile created:\n");
  if (qmi_message_wds_create_profile_output_get_profile_identifier(
          output, &profile_type, &wds_client->packet_session.profile_id,
          NULL)) {
    g_print(" Profile type: '%s'\n",
            qmi_wds_profile_type_get_string(profile_type));
    g_print(" Profile index: '%d'\n", wds_client->packet_session.profile_id);
    wds_client->packet_session.connection_readiness_step =
        WDS_CONNECTION_STATE_PROFILE_READY;
  }
  qmi_message_wds_create_profile_output_unref(output);
}

/* Add New Profile */
void wds_add_new_profile(_WDS_Client *wds_client) {
  QmiMessageWdsCreateProfileInput *input = NULL;
  g_info("Sim Slot %u: Adding new IMS profile\n", wds_client->slot_id);
  input = qmi_message_wds_create_profile_input_new();

  /* We're going to hardcode the fuck out of this for now */
  qmi_message_wds_create_profile_input_set_profile_type(
      input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);
  qmi_message_wds_create_profile_input_set_pdp_context_number(input, 1, NULL);
  qmi_message_wds_create_profile_input_set_pdp_type(input, PDP_TYPE_IPV4, NULL);
  qmi_message_wds_create_profile_input_set_apn_type_mask(
      input, APN_TYPE_MASK_IMS, NULL);
  qmi_message_wds_create_profile_input_set_profile_name(input, "ims", NULL);
  qmi_message_wds_create_profile_input_set_apn_name(input, "ims", NULL);
  qmi_message_wds_create_profile_input_set_authentication(input, 0, NULL);
  qmi_message_wds_create_profile_input_set_username(input, "", NULL);
  qmi_message_wds_create_profile_input_set_password(input, "", NULL);
  qmi_message_wds_create_profile_input_set_roaming_disallowed_flag(input, 0,
                                                                   NULL);
  qmi_message_wds_create_profile_input_set_apn_disabled_flag(input, 0, NULL);

  qmi_client_wds_create_profile(QMI_CLIENT_WDS(wds_client->wds), input, 10, ctx->cancellable,
                                (GAsyncReadyCallback)wds_add_new_profile_ready,
                                wds_client);
  qmi_message_wds_create_profile_input_unref(input);
  return;
}

/* Modify a 3gpp / 3gpp2 Profile */

/*
 * Modify the first profile ID we find
 */

static void wds_modify_profile_ready(QmiClientWds *client, GAsyncResult *res, gpointer user_data) {
  QmiMessageWdsModifyProfileOutput *output;
  GError *error = NULL;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  g_print("[WDS] [SIM %u] Modify Profile finished\n", wds_client->slot_id);
  output = qmi_client_wds_modify_profile_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_modify_profile_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_modify_profile_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't modify profile: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't modify profile: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_modify_profile_output_unref(output);
    return;
  }
  qmi_message_wds_modify_profile_output_unref(output);
  g_print("Profile successfully modified.\n");
}

void wds_modify_profile_by_id(_WDS_Client *wds_client, guint8 profile_id) {
  QmiMessageWdsModifyProfileInput *input = NULL;
  g_info("Modify profile ID %u\n", profile_id);
  input = qmi_message_wds_modify_profile_input_new();
  /* We're going to hardcode the fuck out of this for now */
  qmi_message_wds_modify_profile_input_set_profile_identifier(
      input, PROFILE_TYPE_3GPP, 1, NULL);

  //  qmi_message_wds_modify_profile_input_set_profile_type(input, 0, NULL);
  qmi_message_wds_modify_profile_input_set_pdp_context_number(input, profile_id,
                                                              NULL);
  qmi_message_wds_modify_profile_input_set_pdp_type(input, PDP_TYPE_IPV4, NULL);
  qmi_message_wds_modify_profile_input_set_apn_type_mask(
      input, APN_TYPE_MASK_IMS, NULL);
  qmi_message_wds_modify_profile_input_set_profile_name(input, "ims", NULL);
  qmi_message_wds_modify_profile_input_set_apn_name(input, "ims", NULL);
  qmi_message_wds_modify_profile_input_set_authentication(input, 0, NULL);
  qmi_message_wds_modify_profile_input_set_username(input, "", NULL);
  qmi_message_wds_modify_profile_input_set_password(input, "", NULL);
  qmi_message_wds_modify_profile_input_set_roaming_disallowed_flag(input, 0,
                                                                   NULL);
  qmi_message_wds_modify_profile_input_set_apn_disabled_flag(input, 0, NULL);

  qmi_client_wds_modify_profile(QMI_CLIENT_WDS(wds_client->wds), input, 10, ctx->cancellable,
                                (GAsyncReadyCallback)wds_modify_profile_ready,
                                wds_client);
  qmi_message_wds_modify_profile_input_unref(input);
  wds_client->packet_session.connection_readiness_step =
      WDS_CONNECTION_STATE_PROFILE_READY;
  return;
}


/*
 * END: Profile handling
 */

 
 /* FIXME: Set data format
  *
  */

void wds_qmi_set_data_format(_WDS_Client *wds_client) {
  g_printerr("*** %s: dummy\n", __func__);
  wds_client->packet_session.connection_readiness_step++;
}

/*
 * BEGIN: SETUP DATA LINK
 */
static void wds_connection_setup_link_ready(QmiDevice *device,
                                            GAsyncResult *res, gpointer user_data) {
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  
  g_print("[WDS] Setup Link Ready\n");
  GError *error = NULL;
  g_printerr("[WDS] Setup Link Ready: No link name yet\n");
  wds_client->packet_session.link_name = qmi_device_add_link_with_flags_finish(
      device, res, &wds_client->packet_session.mux_id, &error);

  if (!wds_client->packet_session.link_name) {
    g_prefix_error(&error,
                   "[WDS][FATAL] Failed to add the link name to the context\n");

  } else {
    g_print("[WDS] Link Ready: name: %s, mux id: %u\n",
            wds_client->packet_session.link_name,
            wds_client->packet_session.mux_id);
    wds_client->packet_session.connection_readiness_step++;
  }
}

void wds_connection_setup_link(_WDS_Client *wds_client) {
  /* For some reason we get a MUX ID bigger than the link name all the time */
  QmiDeviceAddLinkFlags flags = (QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV4 |
                                 QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV4);
  if (!wds_client->packet_session.setup_link_done) {
    g_print("[WDS] Set link with flags!\n");
    // FIXME: Need to stop hardcoding these
    qmi_device_add_link_with_flags(
        ctx->device, QMI_DEVICE_MUX_ID_AUTOMATIC, "rmnet_ipa0", "qmapmux0.",
        flags, NULL, (GAsyncReadyCallback)wds_connection_setup_link_ready,
        wds_client);
    wds_client->packet_session.setup_link_done = 1;
  } else {
    g_print("[WDS] Ignoring Spurious call\n");
  }
}

/*
 * END: Setup link
 */


 /*
  * Begin: Bind mux data port
  */

static void wds_bind_mux_data_port_ready(QmiClientWds *client, GAsyncResult *res,
                                     gpointer user_data) {
  GError *error = NULL;
  g_autoptr(QmiMessageWdsBindMuxDataPortOutput) output = NULL;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  g_print("[WDS] Bind Mux Data Port finished (SIM %u): ", wds_client->slot_id);
  output = qmi_client_wds_bind_mux_data_port_finish(client, res, &error);
  if (!output ||
      !qmi_message_wds_bind_mux_data_port_output_get_result(output, &error)) {
    g_print("Couldn't bind mux data port: %s\n", error->message);
    return;
  }
  g_print("OK\n");
  /* Keep on */
  wds_client->packet_session.connection_readiness_step++;
}

void wds_bind_data_port(_WDS_Client *wds_client) {
  g_autoptr(QmiMessageWdsBindMuxDataPortInput) input = NULL;
  wds_client->packet_session.endpoint_ifnum = 1;
  wds_client->packet_session.endpoint_type = QMI_DATA_ENDPOINT_TYPE_EMBEDDED;
  g_print("[WDS] Attempting to bind to mux ID %d\n", wds_client->packet_session.mux_id);

  input = qmi_message_wds_bind_mux_data_port_input_new();

  qmi_message_wds_bind_mux_data_port_input_set_endpoint_info(
      input, wds_client->packet_session.endpoint_type,
      wds_client->packet_session.endpoint_ifnum, NULL);
  qmi_message_wds_bind_mux_data_port_input_set_mux_id(
      input, wds_client->packet_session.mux_id, NULL);

  qmi_client_wds_bind_mux_data_port(
      QMI_CLIENT_WDS(wds_client->wds), input, 10, ctx->cancellable,
      (GAsyncReadyCallback)wds_bind_mux_data_port_ready, wds_client);
}
 /*
  * END: Bind mux data port
  */

/*
 * BEGIN: Stop network
 */

static void wds_stop_network_ready(QmiClientWds *client, GAsyncResult *res, gpointer user_data) {
  GError *error = NULL;
  QmiMessageWdsStopNetworkOutput *output;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;

  g_print("[WDS] Network stopped (SIM %u)\n", wds_client->slot_id);

  output = qmi_client_wds_stop_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_stop_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't stop network: %s\n", error->message);
    g_error_free(error);
    qmi_message_wds_stop_network_output_unref(output);
    return;
  }

  g_print("[%s] Network stopped\n", qmi_device_get_path_display(ctx->device));
  qmi_message_wds_stop_network_output_unref(output);
}

void wds_stop_network(_WDS_Client *wds_client,
                         gboolean disable_autoconnect) {
  QmiMessageWdsStopNetworkInput *input;
  g_print("[WDS] Requesting Network stop (SIM %u)\n", wds_client->slot_id);
  input = qmi_message_wds_stop_network_input_new();
  qmi_message_wds_stop_network_input_set_packet_data_handle(
      input, wds_client->packet_session.packet_data_handle, NULL);
  if (disable_autoconnect)
    qmi_message_wds_stop_network_input_set_disable_autoconnect(input, TRUE,
                                                               NULL);

  g_print("Network cancelled... releasing resources\n");
  qmi_client_wds_stop_network(QMI_CLIENT_WDS(wds_client->wds), input, 120, ctx->cancellable,
                              (GAsyncReadyCallback)wds_stop_network_ready, wds_client);
  qmi_message_wds_stop_network_input_unref(input);
}


/*
 * END: Stop network
 */


/*
 * BEGIN: Start network
 */


static void wds_start_network_ready(QmiClientWds *client, GAsyncResult *res, gpointer user_data) {
  GError *error = NULL;
  QmiMessageWdsStartNetworkOutput *output;
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  g_print("[WDS] Start network ready: SIM %u\n", wds_client->slot_id);
  output = qmi_client_wds_start_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    wds_client->packet_session.connection_readiness_step--;
    return;
  }

  if (!qmi_message_wds_start_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't start network: %s\n", error->message);
    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_CALL_FAILED)) {
      QmiWdsCallEndReason cer;
      QmiWdsVerboseCallEndReasonType verbose_cer_type;
      gint16 verbose_cer_reason;

      if (qmi_message_wds_start_network_output_get_call_end_reason(output, &cer,
                                                                   NULL))
        g_printerr("call end reason (%u): %s\n", cer,
                   qmi_wds_call_end_reason_get_string(cer));

      if (qmi_message_wds_start_network_output_get_verbose_call_end_reason(
              output, &verbose_cer_type, &verbose_cer_reason, NULL))
        g_printerr(
            "verbose call end reason (%u,%d): [%s] %s\n", verbose_cer_type,
            verbose_cer_reason,
            qmi_wds_verbose_call_end_reason_type_get_string(verbose_cer_type),
            qmi_wds_verbose_call_end_reason_get_string(verbose_cer_type,
                                                       verbose_cer_reason));
      wds_client->packet_session.connection_readiness_step = 5;
    }

    g_error_free(error);
    qmi_message_wds_start_network_output_unref(output);
    wds_client->packet_session.connection_readiness_step++;
    return;
  }

  qmi_message_wds_start_network_output_get_packet_data_handle(
      output, &wds_client->packet_session.packet_data_handle, NULL);
  qmi_message_wds_start_network_output_unref(output);

  g_print("[WDS][%s] Network started, handle: '%u'\n",
          qmi_device_get_path_display(ctx->device),
          (guint)wds_client->packet_session.packet_data_handle);
  wds_client->packet_session.connection_readiness_step++;
}

void wds_start_network(_WDS_Client *wds_client) {
  QmiMessageWdsStartNetworkInput *input = NULL;
  g_print("[WDS] Starting network: Mux ID %d, SIM slot %u\n", wds_client->packet_session.mux_id, wds_client->slot_id);
  input = qmi_message_wds_start_network_input_new();
  qmi_message_wds_start_network_input_set_apn(input, "ims", NULL);
  qmi_message_wds_start_network_input_set_profile_index_3gpp(
      input, wds_client->packet_session.profile_id, NULL);
  qmi_message_wds_start_network_input_set_ip_family_preference(
      input, QMI_WDS_IP_FAMILY_IPV4, NULL);
  qmi_message_wds_start_network_input_set_enable_autoconnect(input, TRUE, NULL);
  qmi_client_wds_start_network(QMI_CLIENT_WDS(wds_client->wds), input, 180, ctx->cancellable,
                               (GAsyncReadyCallback)wds_start_network_ready, wds_client);
  if (input)
    qmi_message_wds_start_network_input_unref(input);

  wds_client->packet_session.connection_readiness_step++;
}

/*
 * END: Start network
 */

/*
 * BEGIN: Get network interface settings
 */

 /* HELPERS: START */
 static void qmi_inet4_ntop(guint32 address, char *buf, const gsize buflen) {
  struct in_addr a = {.s_addr = GUINT32_TO_BE(address)};

  g_assert(buflen >= INET_ADDRSTRLEN);

  /* We can ignore inet_ntop() return value if 'buf' is
   * at least INET_ADDRSTRLEN in size. */
  memset(buf, 0, buflen);
  g_assert(inet_ntop(AF_INET, &a, buf, buflen));
}

guint mm_count_bits_set(gulong number) {
  guint c;

  for (c = 0; number; c++)
    number &= number - 1;
  return c;
}

guint mm_find_bit_set(gulong number) {
  guint c = 0;

  for (c = 0; !(number & 0x1); c++)
    number >>= 1;
  return c;
}

guint mm_netmask_to_cidr(const gchar *netmask) {
  guint32 num = 0;

  inet_pton(AF_INET, netmask, &num);
  return mm_count_bits_set(num);
}
/*
void calculate_subnet(char *ip_address, char *netmask, char *subnet) {
  struct in_addr ip, mask, subnet_addr;

  // Convert IP and netmask strings to binary form
  if (inet_pton(AF_INET, ip_address, &ip) != 1) {
    fprintf(stderr, "Invalid IP address: %s\n", ip_address);
    exit(EXIT_FAILURE);
  }

  if (inet_pton(AF_INET, netmask, &mask) != 1) {
    fprintf(stderr, "Invalid netmask: %s\n", netmask);
    exit(EXIT_FAILURE);
  }

  // Calculate subnet address by bitwise AND
  subnet_addr.s_addr = ip.s_addr & mask.s_addr;

  // Convert subnet address to dot-decimal notation
  if (inet_ntop(AF_INET, &subnet_addr, subnet, INET_ADDRSTRLEN) == NULL) {
    perror("inet_ntop");
    exit(EXIT_FAILURE);
  }
  g_print(" - Subnet: %s\n", subnet);
}
*/
 /* HELPERS: END*/
static void wds_get_current_settings_ready(QmiClientWds *client, GAsyncResult *res, gpointer user_data) {
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  GError *error = NULL;
  QmiMessageWdsGetCurrentSettingsOutput *output;
  char buf[16];
//  char address[32];
  //char network[32];
  char netmask[32];
  guint32 addr = 0;
  guint32 prefix = 0;
  QmiWdsIpFamily ip_family = QMI_WDS_IP_FAMILY_UNSPECIFIED;
  guint32 mtu = 0;
  //char temp[128] = {0};

  output = qmi_client_wds_get_current_settings_finish(client, res, &error);
  if (!output ||
      !qmi_message_wds_get_current_settings_output_get_result(output, &error)) {

    /* Otherwise, just go on as we're asking for DHCP */
    g_print("[WDS] Error: couldn't get current settings for SIM %u: %s", wds_client->slot_id, error->message);
    g_error_free(error);
  }

  if (!qmi_message_wds_get_current_settings_output_get_ip_family(
          output, &ip_family, &error)) {
    g_print(" IP Family: failed (%s); assuming IPv4", error->message);
    g_clear_error(&error);
    ip_family = QMI_WDS_IP_FAMILY_IPV4;
  }
  g_print(" - IP Family: %s \n", (ip_family == QMI_WDS_IP_FAMILY_IPV4) ? "IPv4"
                                 : (ip_family == QMI_WDS_IP_FAMILY_IPV6)
                                     ? "IPv6"
                                     : "unknown");

  if (!qmi_message_wds_get_current_settings_output_get_mtu(output, &mtu,
                                                           &error)) {
    g_print("MTU: failed (%s)", error->message);
    g_clear_error(&error);
  } else {
    g_print(" - MTU: %d\n", mtu);
  }
  /*
          if (ip_family == QMI_WDS_IP_FAMILY_IPV4)
              ctx->ipv4_config = get_ipv4_config (ctx->self, ctx->ip_method,
     output, mtu); else if (ip_family == QMI_WDS_IP_FAMILY_IPV6)
              ctx->ipv6_config = get_ipv6_config (ctx->self, ctx->ip_method,
     output, mtu);*/

  //    process_operator_reserved_pco (self, output);

  /* IPv4 subnet mask */
  if (!qmi_message_wds_get_current_settings_output_get_ipv4_gateway_subnet_mask(
          output, &addr, &error)) {
    g_print("failed to read IPv4 netmask: %s\n", error->message);
    g_clear_error(&error);
    return;
  }

  qmi_inet4_ntop(addr, buf, sizeof(buf));
  prefix = mm_netmask_to_cidr(buf);
  strncpy(netmask, buf, sizeof(netmask));
  /* IPv4 address */
  if (!qmi_message_wds_get_current_settings_output_get_ipv4_address(
          output, &addr, &error)) {
    g_print("IPv4 family but no IPv4 address: %s\n", error->message);
    g_clear_error(&error);
    return;
  }
  /* IPv4 address */

  qmi_inet4_ntop(addr, buf, sizeof(buf));
  g_print("IPv4 Address: %s/%d\n", buf, prefix);
  memcpy(wds_client->packet_session.ip_address, buf, strlen(buf));
 // strncpy(address, buf, sizeof(address));
  // sprintf(temp, "ip addr add %s/%d dev %s ", buf, prefix,
   //       wds_client->packet_session.link_name);
 // g_print("Executing system command %s \n", temp);
  // system(temp); // LIKE ANIMALS
//  calculate_subnet(address, netmask, network);

  /* IPv4 gateway address */
  if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address(
          output, &addr, &error)) {
    qmi_inet4_ntop(addr, buf, sizeof(buf));
    g_print(" - IPv4 Gateway: %s\n", buf);
  } else {
    g_print("- IPv4 Gateway: failed (%s)\n", error->message);
    g_clear_error(&error);
  }

//  temp[0] = 0x00;
//  sprintf(temp, "ip route change %s/%d via %s dev %s ", network, prefix, buf,
//          wds_client->packet_session.link_name);
//  g_print("Executing system command %s \n", temp);
// system(temp); // LIKE ANIMALS

  if (output)
    qmi_message_wds_get_current_settings_output_unref(output);

  /* Keep on */
  wds_client->packet_session.connection_readiness_step++;
}

static void wds_get_current_settings(_WDS_Client *wds_client) {
  QmiMessageWdsGetCurrentSettingsInput *input;
  QmiWdsRequestedSettings requested;

  requested = QMI_WDS_REQUESTED_SETTINGS_DNS_ADDRESS |
              QMI_WDS_REQUESTED_SETTINGS_GRANTED_QOS |
              QMI_WDS_REQUESTED_SETTINGS_IP_ADDRESS |
              QMI_WDS_REQUESTED_SETTINGS_GATEWAY_INFO |
              QMI_WDS_REQUESTED_SETTINGS_MTU |
              QMI_WDS_REQUESTED_SETTINGS_DOMAIN_NAME_LIST |
              QMI_WDS_REQUESTED_SETTINGS_IP_FAMILY |
              QMI_WDS_REQUESTED_SETTINGS_OPERATOR_RESERVED_PCO;

  input = qmi_message_wds_get_current_settings_input_new();
  qmi_message_wds_get_current_settings_input_set_requested_settings(
      input, requested, NULL);
  qmi_client_wds_get_current_settings(
      QMI_CLIENT_WDS(wds_client->wds), input, 10, NULL,
      (GAsyncReadyCallback)wds_get_current_settings_ready, wds_client);
  qmi_message_wds_get_current_settings_input_unref(input);
}

/*
 * END: Get network interface settings
 */



/*
 * Get ready to connect
 * This loops through all the stages to get
 * a network session started
 **/
gboolean get_wds_ready_to_connect(gpointer user_data) {
  _WDS_Client *wds_client = (_WDS_Client *)user_data;
  g_print("[WDS] Readiness step for SIM slot %u: %u\n", wds_client->slot_id,
             wds_client->packet_session.connection_readiness_step);
  switch (wds_client->packet_session.connection_readiness_step) {
  case WDS_CONNECTION_GET_PROFILES:
    g_print(" - Get available Profiles\n");
    wds_get_profile_list(wds_client, 0);
    break;
  case WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY:
    g_print(" - Find a suitable IMS profile for sim %u \n", wds_client->slot_id);
    if (wds_client->packet_session.profile_id == 0) {
       wds_add_new_profile(wds_client);
     } else {
       wds_modify_profile_by_id(wds_client, wds_client->packet_session.profile_id);
     }
    break;
  case WDS_CONNECTION_STATE_PROFILE_READY:
    g_print(" - Found a valid profile for SIM %u, profile ID is %u\n",
               wds_client->slot_id, wds_client->packet_session.profile_id);
    wds_client->packet_session.connection_readiness_step = WDS_CONNECTION_STATE_SETUP_DATA_FORMAT;
    break;
  case WDS_CONNECTION_STATE_SETUP_DATA_FORMAT:
    g_print(" - Set data format (sim %u) \n", wds_client->slot_id);
    wds_qmi_set_data_format(wds_client);
    break;
  case WDS_CONNECTION_STATE_SETUP_LINK:
    g_print("- Setting up link for SIM %u\n", wds_client->slot_id);
    wds_connection_setup_link(wds_client);
    break;
  case WDS_CONNECTION_STATE_LINK_BRINGUP:
    g_print(" - Bringup link for SIM %u [BYPASSED] \n", wds_client->slot_id);
//    char temp[32] = {0};
//    sprintf(temp, "ip link set %s up", wds_client->packet_session.link_name);
//    g_print("Executing system command %s\n", temp);
    // system(temp); // LIKE ANIMALS
    wds_client->packet_session.connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_SET_IP_BEARER_METHOD:
    g_print(" - Set Bearer method [dummy]\n");
    wds_client->packet_session.connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV4:
    g_print(" - Bund multiplexed data port \n");
    wds_bind_data_port(wds_client);
    break;
  case WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV4:
    g_print(" - Select IP Family (IPv4) [dummy]\n");
    wds_client->packet_session.connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_DO_START_NETWORK_IPV4:
    g_print(" - Start network for SIM %u\n", wds_client->slot_id);
    wds_start_network(wds_client);
    break;
  case WDS_CONNECTION_STATE_WAIT_FOR_COMPLETION_NET_START_IPV4:
    g_print(" - Start network for SIM %u: Waiting for completion...\n", wds_client->slot_id);
    break;
  case WDS_CONNECTION_STATE_REGISTER_WDS_INDICATIONS_IPV4:
    g_print(" - Register for changes in session for SIM %u\n", wds_client->slot_id);
    wds_client->packet_session.connection_readiness_step++;
    // Get packet service stats here
    break;
  case WDS_CONNECTION_STATE_GET_SETTINGS_IPV4:
    g_print(" - Get network settings (SIM %u)\n", wds_client->slot_id);
    wds_get_current_settings(wds_client);
    break;
  case WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV6:
    g_print("[WDS] WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV6 [stop here]\n");
    wds_client->packet_session.connection_readiness_step = 99;
    break;
  case WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV6:
    g_print("[WDS] WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_DO_START_NETWORK_IPV6:
    g_print("[WDS] WDS_CONNECTION_STATE_DO_START_NETWORK_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_ENABLE_INDICATIONS_IPV6:
    g_print("[WDS] WDS_CONNECTION_STATE_ENABLE_INDICATIONS_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_GET_SETTINGS_IPV6:
    g_print("[WDS] WDS_CONNECTION_STATE_GET_SETTINGS_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_FINISHED:
    g_print("[WDS] Bringup finished. Notifying DCM\n");
    notify_pdp_ipaddress_change(wds_client->slot_id, wds_client->packet_session.ip_address); 

    return FALSE;
  default:
    g_info("We hit the default case, giving up\n");
    return FALSE;
    break;
  }

  return TRUE;
}

/*
 * Device allocation and client initialization
 */
void wds_init_context(QmiDevice *device, GCancellable *cancellable) {

  /* Initialize context */
  if (!ctx) {
    ctx = g_slice_new(Context);
    ctx->device = g_object_ref(device);
    ctx->cancellable = g_object_ref(cancellable);
  } else {
    g_print("[WDS] Context already initialized :)\n");
  }
}

void initiate_wds_session(_WDS_Client *wds_client, uint32_t sim_slot) {
  g_print("Connection requested for SIM Slot %u\n", sim_slot);
  wds_client->packet_session.mux_id = 0;
  wds_client->packet_session.connection_readiness_step = 0;
  wds_client->packet_session.profile_id = 0;
  wds_client->packet_session.setup_link_done = 0;
  wds_client->packet_session.packet_data_handle = 0xFFFFFFFF;
  wds_client->slot_id = sim_slot;
  g_timeout_add(5, get_wds_ready_to_connect, wds_client);
  // g_timeout_add(10, get_pkt_svc_status, &wds_client);
}