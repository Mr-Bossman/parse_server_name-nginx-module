
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Nginx, Inc.
 */

#include <nginx.h>

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


typedef struct {
    ngx_flag_t      enabled;
} ngx_stream_parse_server_name_srv_conf_t;


typedef struct {
    ngx_str_t       host;
    ngx_log_t      *log;
    ngx_pool_t     *pool;
} ngx_stream_parse_server_name_ctx_t;


static void *ngx_stream_parse_server_name_create_srv_conf(ngx_conf_t *cf);
static ngx_int_t ngx_stream_parse_server_name_servername(ngx_stream_session_t *s,
    ngx_str_t *servername);
static ngx_int_t ngx_stream_parse_server_name_parse(
    ngx_stream_parse_server_name_ctx_t *ctx, ngx_buf_t *buf);
static ngx_int_t ngx_stream_parse_server_name_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_parse_server_name_init(ngx_conf_t *cf);

static ngx_command_t  ngx_stream_parse_server_name_commands[] = {

    { ngx_string("parse_server_name"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_parse_server_name_srv_conf_t, enabled),
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_parse_server_name_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_stream_parse_server_name_init,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    ngx_stream_parse_server_name_create_srv_conf,     /* create server configuration */
    NULL                                   /* merge server configuration */
};


ngx_module_t  ngx_stream_parse_server_name_module = {
    NGX_MODULE_V1,
    &ngx_stream_parse_server_name_module_ctx,         /* module context */
    ngx_stream_parse_server_name_commands,            /* module directives */
    NGX_STREAM_MODULE,                     /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_stream_parse_server_name_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_parse_server_name_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_parse_server_name_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;

    return conf;
}
static u_char *get_http_host(u_char *data, size_t len)
{
    while (len > 0) {
        len--;
        if(*data++ == '\n') {
            break;
        }
    }
    if (len > 6 && ngx_strncmp(data, (u_char*)"Host: ", 6) == 0) {
        data += 6;
        return data;
    }
    return NULL;
}

static ngx_int_t ngx_stream_parse_server_name_parse(
    ngx_stream_parse_server_name_ctx_t *ctx, ngx_buf_t *buf)
{
    u_char *p, *last, *url;
    size_t len, i = 0;
    p = buf->pos;
    last = buf->last;
    len = p - last;

    u_char data[256];
    memset(data, 0, 256);
    url = get_http_host(p, len);
    if (url == NULL) {
        return NGX_AGAIN;
    }
    while (url < last && *url != '\r' && i < 256) {
        data[i] = url[i];
        i++;
    }

    size_t size = ngx_strnlen(data, 256);
    ctx->host.data = ngx_pnalloc(ctx->pool, size);
    if (ctx->host.data == NULL) {
        return NGX_ERROR;
    }
    (void)ngx_cpymem(ctx->host.data, data, size);
    ctx->host.len = size;

    (void)ngx_hex_dump(data, p, ngx_min(len*2, 127));
    ngx_log_debug(NGX_LOG_DEBUG_STREAM, ctx->log, 0, "stream parse_server_name: %s", data);
// sed -n 's/.*stream parse_server_name: //gp'  /usr/local/nginx/logs/debug.log |  xxd -r -p | hexdump -C
    return NGX_OK;
}

static ngx_int_t
ngx_stream_parse_server_name_handler(ngx_stream_session_t *s)
{
    ngx_int_t                                   rc;
    ngx_connection_t                           *c;
    ngx_stream_parse_server_name_ctx_t         *ctx;
    ngx_stream_parse_server_name_srv_conf_t    *sscf;

    c = s->connection;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, c->log, 0, "parse_server_name preread handler");

    sscf = ngx_stream_get_module_srv_conf(s, ngx_stream_parse_server_name_module);

    if (!sscf->enabled) {
        return NGX_DECLINED;
    }

    if (c->buffer == NULL) {
        return NGX_AGAIN;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_parse_server_name_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(c->pool, sizeof(ngx_stream_parse_server_name_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
        ctx->pool = c->pool;
        ctx->log = c->log;
        ngx_stream_set_ctx(s, ctx, ngx_stream_parse_server_name_module);
    }

    rc = ngx_stream_parse_server_name_parse(ctx, c->buffer);

    if (rc == NGX_OK) {
        return ngx_stream_parse_server_name_servername(s, &ctx->host);
    } else {
    return rc;
    }

    return NGX_AGAIN;
}

static ngx_int_t
ngx_stream_parse_server_name_servername(ngx_stream_session_t *s,
    ngx_str_t *servername)
{
    ngx_int_t                    rc;
    ngx_str_t                    host;
    ngx_connection_t            *c;
    ngx_stream_core_srv_conf_t  *cscf;

    c = s->connection;

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "parse_server_name server name: \"%V\"", servername);

    if (servername->len == 0) {
        return NGX_OK;
    }

    host = *servername;

    rc = ngx_stream_validate_host(&host, c->pool, 1);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        return NGX_OK;
    }

    rc = ngx_stream_find_virtual_server(s, &host, &cscf);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        return NGX_OK;
    }

    s->srv_conf = cscf->ctx->srv_conf;

    ngx_set_connection_log(c, cscf->error_log);

    return NGX_OK;
}

static ngx_int_t
ngx_stream_parse_server_name_init(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_parse_server_name_handler;

    return NGX_OK;
}
