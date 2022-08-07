/* The basis of this module is the source code of the ngx_http_response_module.
 * That module is (c) Kirill A. Korinskiy.
 */

/*
 * Copyright (C) Kirill A. Korinskiy
 */

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * The data structure that holds the configuration that the user
 * provides for the Dali module.
 *
 * There is only one customizable value.
 */
typedef struct {
  size_t length;
} ngx_http_dali_conf_t;

/*
 * Declare some functions that will do the real work of
 * managing the configuration parsing process. Declare before
 * implementation because we want to make pointers to them.
 */
static void *ngx_http_dali_create_conf(ngx_conf_t *);
static char *ngx_http_dali_merge_conf(ngx_conf_t *, void *, void *);
static char *ngx_http_dali_enable(ngx_conf_t *, ngx_command_t *, void *);

/*
 * Specify the configuration options available for the user
 * of this module.
 */
static ngx_command_t ngx_http_dali_commands[] = {
    {ngx_string("dali"), NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     ngx_http_dali_enable, NGX_HTTP_LOC_CONF_OFFSET, 0, NULL},
    ngx_null_command};

/*
 * This struct will be used to tell nginx which of its
 * many *configuration* phases this module will join.
 */
static ngx_http_module_t ngx_http_dali_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    ngx_http_dali_create_conf, /* create location configuration */
    ngx_http_dali_merge_conf   /* merge location configuration */
};

/*
 * The previous data structure was used to tell
 * nginx about which of its configuration phases
 * to join. This data structure will tell nginx
 * that we want to work with configuration and
 * that we are a module!
 */
ngx_module_t ngx_http_dali_module = {
    NGX_MODULE_V1,
    &ngx_http_dali_module_ctx, /* module context */
    ngx_http_dali_commands,    /* module directives */
    NGX_HTTP_MODULE,           /* module type */
    NULL,                      /* init master */
    NULL,                      /* init module */
    NULL,                      /* init process */
    NULL,                      /* init thread */
    NULL,                      /* exit thread */
    NULL,                      /* exit process */
    NULL,                      /* exit master */
    NGX_MODULE_V1_PADDING};

/*
 * ngx_http_dali_handler
 *
 * This is the function that will execute to generate an
 * http response.
 *
 * Input: Information about the request being satisfied.
 * Output: An error code indicating to nginx whether we
 * were successful in generating that response.
 */
static ngx_int_t ngx_http_dali_handler(ngx_http_request_t *r) {
  ngx_http_dali_conf_t *conf = NULL;
  ngx_uint_t status = NGX_HTTP_OK;
  ngx_int_t ngx_discard_rc = NGX_OK;
  ngx_int_t ngx_send_header_rc = NGX_OK;
  ngx_chain_t *output_chain = NULL;
  ngx_buf_t *buffer = NULL;
  ngx_fd_t dev_zero_fd;
  ngx_str_t dev_zero_path;

  ngx_str_set(&r->headers_out.content_type, "application/octet-stream");

  /*
   * We could fail to read the module configuration.
   */
  conf = ngx_http_get_module_loc_conf(r, ngx_http_dali_module);
  if (!conf) {
    ngx_log_error(
        NGX_LOG_CRIT, r->connection->log, 0,
        "Dali could not access configuration data when handling a request");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  /*
   * We could fail to read/discard the request's body!
   */
  ngx_discard_rc = ngx_http_discard_request_body(r);
  if (ngx_discard_rc > NGX_OK) {
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                  "Dali could not read and discard the request's body");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  /*
   * We could fail to allocate space required for the meta structures.
   */
  output_chain = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
  buffer = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
  buffer->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));

  if (!output_chain || !buffer || !buffer->file) {
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                  "Dali could not allocate memory for meta structures");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  ngx_str_set(&dev_zero_path, "/dev/zero");
  dev_zero_fd =
      ngx_open_file(dev_zero_path.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
  if (dev_zero_fd == NGX_INVALID_FILE) {
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                  "Dali could not open /dev/zero");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  /*
   * Configure the response buffer and chain appropriately.
   */
  buffer->file_pos = 0;
  buffer->file_last = conf->length;
  buffer->in_file = 1;
  buffer->last_buf = 1;
  buffer->last_in_chain = 1;

  buffer->file->fd = dev_zero_fd;
  buffer->file->name = dev_zero_path;
  buffer->file->log = r->connection->log;
  buffer->file->directio = false;

  output_chain->buf = buffer;
  output_chain->next = NULL;

  /* sendfile does not work with character device files. Disable. */
  r->connection->sendfile = 0;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "Dali module responding");

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                "Dali sending a %d byte response", conf->length);

  /*
   * Setup some values for the header of our response.
   */
  r->headers_out.content_length_n = (conf->length);
  r->headers_out.status = status;

  /*
   * Send the headers of the response.
   */
  ngx_send_header_rc = ngx_http_send_header(r);
  if (ngx_send_header_rc == NGX_ERROR) {
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                  "Dali could not send the response header");
    return ngx_send_header_rc;
  }

  r->allow_ranges = 1;
  if (ngx_send_header_rc > NGX_OK || r->header_only) {
    return ngx_send_header_rc;
  }

  /*
   * Kick off the nginx processing chain that will ultimately
   * send our response body back to the user.
   */
  return ngx_http_output_filter(r, output_chain);
}

/*
 * ngx_http_dali_create_conf
 *
 * Thsi function will allocate the space necessary for
 * the data structure that will hold configuration information
 * about our module. We set the initial value of the
 * configuration options to `UNSET` so that nginx knows
 * what to do with them when running various helper functions
 * (see ngx_http_dali_merge_conf, below)
 *
 * Input: A pointer to the overall server configuration (unused)
 * Output: A pointer to the (newly) allocated memory
 * that will hold the configuration for this module.
 */
static void *ngx_http_dali_create_conf(ngx_conf_t *cf) {
  ngx_http_dali_conf_t *conf;

  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
                "ngx_http_dali_create_conf starting");
  conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_dali_conf_t));
  if (conf == NULL) {
    return NULL;
  }
  conf->length = NGX_CONF_UNSET_SIZE;
  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
                "ngx_http_dali_create_conf returning: %uxL", (uintptr_t)conf);
  return conf;
}

/*
 * ngx_http_dali_merge_conf
 *
 * So-called location configurations can nest in nginx. This
 * function will handle "merging" nested configuration options.
 *
 * Input: A pointer to the overall server configuration (unused)
 *        A pointer to the enclosing location configuration
 *        A pointer to the immediate configuration
 * Output: A status indicator telling nginx whether we could
 * successfully merge the two configurations into the immediate
 * configuration.
 */
static char *ngx_http_dali_merge_conf(ngx_conf_t *cf, void *parent,
                                      void *child) {
  ngx_http_dali_conf_t *prev = parent;
  ngx_http_dali_conf_t *conf = child;

  /* We always want to inherit the smaller size in nested configurations.
   */
  if (prev->length > 0 && prev->length < conf->length) {
    conf->length = prev->length;
  }
  return NGX_CONF_OK;
}

/*
 * ngx_http_dali_enable
 *
 * This function is invoked by nginx when it sees a `dali`
 * directive in the configuration file.
 *
 * Input: The overall server configuration
 *        The text of the raw configuration command being processed
 *        A pointer to the extra information specified for this
 *        callback above (see ngx_http_dali_commands).
 * Output: The result of processing the command (indirectly through
 * a helper function provided be nginx that actually processes the
 * command after we do some prework.
 */
static char *ngx_http_dali_enable(ngx_conf_t *cf, ngx_command_t *cmd,
                                  void *conf) {
  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
                "ngx_http_dali_enable starting (conf: %uxL)", (uintptr_t)conf);
  ngx_http_core_loc_conf_t *clcf;

  // Behind the scenes there is a tremendous amount of trickery required
  // in order to make this module implementation work out properly. In other
  // words, don't think too deeply about it.
  clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  /*
   * Tell nginx that this dali module is going to handle requests that
   * get matched to the location being configured! This is key!
   */
  clcf->handler = ngx_http_dali_handler;
  return ngx_conf_set_size_slot(cf, cmd, conf);
}
