#include <obs-module.h>

struct webrtc_mediasoup {
  char *server, *key;
  bool use_auth;
  char *username, *password;
  char *output;
};

static const char *webrtc_mediasoup_name(void *unused)
{
  UNUSED_PARAMETER(unused);
  return "mediasoup WebRTC Server";
}

static void webrtc_mediasoup_update(void *data, obs_data_t *settings)
{
  struct webrtc_mediasoup *service = data;

  bfree(service->server);
  bfree(service->key);
  bfree(service->username);
  bfree(service->password);
  bfree(service->output);

  service->server = bstrdup(obs_data_get_string(settings, "server"));
  service->key = bstrdup(obs_data_get_string(settings, "key"));
  service->use_auth = obs_data_get_bool(settings, "use_auth");
  service->username = bstrdup(obs_data_get_string(settings, "username"));
  service->password = bstrdup(obs_data_get_string(settings, "password"));
  service->output = bstrdup("mediasoup_output");
}

static void webrtc_mediasoup_destroy(void *data)
{
  struct webrtc_mediasoup *service = data;

  bfree(service->server);
  bfree(service->key);
  bfree(service->username);
  bfree(service->password);
  bfree(service->output);
  bfree(service);
}

static void *webrtc_mediasoup_create(obs_data_t *settings, obs_service_t *service)
{
  struct webrtc_mediasoup *data = bzalloc(sizeof(struct webrtc_mediasoup));
  webrtc_mediasoup_update(data, settings);

  UNUSED_PARAMETER(service);
  return data;
}

static bool use_auth_modified(obs_properties_t *ppts, obs_property_t *p,
            obs_data_t *settings)
{
  bool use_auth = obs_data_get_bool(settings, "use_auth");
  p = obs_properties_get(ppts, "username");
  obs_property_set_visible(p, use_auth);
  p = obs_properties_get(ppts, "password");
  obs_property_set_visible(p, use_auth);
  return true;
}

static obs_properties_t *webrtc_mediasoup_properties(void *unused)
{
  UNUSED_PARAMETER(unused);

  obs_properties_t *ppts = obs_properties_create();
  obs_property_t *p;

  obs_properties_add_text(ppts, "server", "URL", OBS_TEXT_DEFAULT);

  obs_properties_add_text(ppts, "key", obs_module_text("StreamKey"),
        OBS_TEXT_PASSWORD);

  p = obs_properties_add_bool(ppts, "use_auth",
            obs_module_text("UseAuth"));
  obs_properties_add_text(ppts, "username", obs_module_text("Username"),
        OBS_TEXT_DEFAULT);
  obs_properties_add_text(ppts, "password", obs_module_text("Password"),
        OBS_TEXT_PASSWORD);
  obs_property_set_modified_callback(p, use_auth_modified);
  return ppts;
}

static const char *webrtc_mediasoup_url(void *data)
{
  struct webrtc_mediasoup *service = data;
  return service->server;
}

static const char *webrtc_mediasoup_key(void *data)
{
 struct webrtc_mediasoup *service = data;
  return service->key;
}

static const char *webrtc_mediasoup_username(void *data)
{
  struct webrtc_mediasoup *service = data;
  if (!service->use_auth)
    return NULL;
  return service->username;
}

static const char *webrtc_mediasoup_password(void *data)
{
  struct webrtc_mediasoup *service = data;
  if (!service->use_auth)
    return NULL;
  return service->password;
}

static const char *webrtc_mediasoup_get_output_type(void *data)
{
  struct webrtc_mediasoup *service = data;
  return service->output;
}

struct obs_service_info webrtc_mediasoup_service = {
  .id             = "webrtc_mediasoup",
  .get_name       = webrtc_mediasoup_name,
  .create         = webrtc_mediasoup_create,
  .destroy        = webrtc_mediasoup_destroy,
  .update         = webrtc_mediasoup_update,
  .get_properties = webrtc_mediasoup_properties,
  .get_url        = webrtc_mediasoup_url,
  .get_key        = webrtc_mediasoup_key,
  .get_username   = webrtc_mediasoup_username,
  .get_password   = webrtc_mediasoup_password,
  .get_output_type = webrtc_mediasoup_get_output_type
};