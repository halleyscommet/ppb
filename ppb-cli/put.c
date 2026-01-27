#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>

#include "vendor/cJSON.h"

#define CONFIG_SIZE 65536
#define URL_SIZE 512
#define TOKEN_SIZE 512

typedef struct {
    char url[URL_SIZE];
    char token[TOKEN_SIZE];
    int verbose;
    int show_response;
} Config;

typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

static void copy_string(char *dest, size_t cap, const char *src)
{
    if (!dest || !cap || !src) return;
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    ResponseBuffer *mem = (ResponseBuffer *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    
    if (!ptr) {
        fprintf(stderr, "Error: not enough memory for response\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

void print_help(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --url <URL>          Override server URL\n");
    printf("  --token <TOKEN>      Override auth token\n");
    printf("  --server <NAME>      Use server config by name\n");
    printf("  --config <PATH>      Use custom config file\n");
    printf("  --init-config        Write default config then exit\n");
    printf("  -v, --verbose        Verbose output\n");
    printf("  -r, --response       Show server response\n");
    printf("  -h, --help           Show this help message\n\n");
    printf("Environment variables:\n");
    printf("  PPB_URL              Server URL\n");
    printf("  PPB_TOKEN            Auth token\n\n");
    printf("Config files (checked in order):\n");
    printf("  .ppb-config.json\n");
    printf("  ~/.ppb/config.json\n\n");
    printf("Usage example:\n");
    printf("  cat file.txt | %s --server prod --response\n", prog);
    printf("\nPrecedence: CLI > env > config > defaults.\n");
}

char *get_config_path(const char *custom_path) {
    static char path[512];
    char *home = getenv("HOME");
    
    if (custom_path) {
        strncpy(path, custom_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        return path;
    }
    
    if (access(".ppb-config.json", F_OK) == 0) {
        strcpy(path, ".ppb-config.json");
        return path;
    }
    
    if (home) {
        snprintf(path, sizeof(path), "%s/.ppb/config.json", home);
        return path; // return default home path even if not present yet
    }
    return NULL;
}

static int ensure_parent_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash) return 0;
    size_t len = (size_t)(slash - path);
    if (len >= 512) return -1;
    char dir[512];
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (dir[0] == '\0') return 0;
    if (mkdir(dir, 0700) == -1 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void create_default_config_if_missing(const char *config_path, int verbose)
{
    if (!config_path) return;
    if (access(config_path, F_OK) == 0) return;

    if (!strstr(config_path, "/.ppb/config.json")) {
        return; // only auto-create the default home config to avoid unexpected files
    }

    if (ensure_parent_dir(config_path) != 0) {
        if (verbose) fprintf(stderr, "Warning: failed to create config directory for %s\n", config_path);
        return;
    }

    FILE *f = fopen(config_path, "w");
    if (!f) {
        if (verbose) fprintf(stderr, "Warning: could not write default config to %s\n", config_path);
        return;
    }

    const char *default_json =
        "{\n"
        "  \"default_server\": \"https://epa.st/upload\",\n"
        "  \"default_token\": \"\",\n"
        "  \"servers\": {\n"
        "    \"local\": {\n"
        "      \"url\": \"http://127.0.0.1:5000\",\n"
        "      \"token\": \"\"\n"
        "    }\n"
        "  }\n"
        "}\n";

    fwrite(default_json, 1, strlen(default_json), f);
    fclose(f);
    chmod(config_path, 0600);
    if (verbose) fprintf(stderr, "[*] Created default config at %s\n", config_path);
}

static int write_default_config(const char *config_path, int verbose)
{
    if (!config_path) {
        fprintf(stderr, "Error: no config path resolved; use --config to specify one.\n");
        return 1;
    }

    if (access(config_path, F_OK) == 0) {
        if (verbose) fprintf(stderr, "[*] Config already exists at %s\n", config_path);
        return 0;
    }

    if (ensure_parent_dir(config_path) != 0) {
        fprintf(stderr, "Error: failed to create parent directory for %s\n", config_path);
        return 1;
    }

    FILE *f = fopen(config_path, "w");
    if (!f) {
        fprintf(stderr, "Error: could not write default config to %s\n", config_path);
        return 1;
    }

    const char *default_json =
        "{\n"
        "  \"default_server\": \"https://epa.st/upload\",\n"
        "  \"default_token\": \"\",\n"
        "  \"servers\": {\n"
        "    \"local\": {\n"
        "      \"url\": \"http://127.0.0.1:5000\",\n"
        "      \"token\": \"\"\n"
        "    }\n"
        "  }\n"
        "}\n";

    fwrite(default_json, 1, strlen(default_json), f);
    fclose(f);
    chmod(config_path, 0600);
    if (verbose) fprintf(stderr, "[*] Created default config at %s\n", config_path);
    return 0;
}

// Simple JSON value extractor - avoid full JSON parsing
static char *read_entire_file(const char *path, size_t max_bytes, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct stat st;
    if (stat(path, &st) != 0) { fclose(f); return NULL; }
    if ((size_t)st.st_size > max_bytes) { fclose(f); return NULL; }
    size_t len = (size_t)st.st_size;
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static void apply_server_object(cJSON *server_obj, Config *cfg)
{
    cJSON *url = cJSON_GetObjectItemCaseSensitive(server_obj, "url");
    cJSON *token = cJSON_GetObjectItemCaseSensitive(server_obj, "token");
    if (cJSON_IsString(url) && url->valuestring) copy_string(cfg->url, URL_SIZE, url->valuestring);
    if (cJSON_IsString(token) && token->valuestring) copy_string(cfg->token, TOKEN_SIZE, token->valuestring);
}

static void parse_config(const char *config_path, Config *cfg, const char *server_name)
{
    if (!config_path) return;
    size_t len = 0;
    char *buffer = read_entire_file(config_path, CONFIG_SIZE, &len);
    if (!buffer) {
        if (cfg->verbose)
            fprintf(stderr, "Note: config not readable at %s, using defaults\n", config_path);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(buffer, len);
    if (!root) {
        if (cfg->verbose)
            fprintf(stderr, "Note: config at %s is invalid JSON, ignoring\n", config_path);
        free(buffer);
        return;
    }

    if (server_name) {
        cJSON *servers = cJSON_GetObjectItemCaseSensitive(root, "servers");
        cJSON *server = cJSON_IsObject(servers) ? cJSON_GetObjectItemCaseSensitive(servers, server_name) : NULL;
        if (cJSON_IsObject(server)) {
            apply_server_object(server, cfg);
        }
    } else {
        cJSON *default_url = cJSON_GetObjectItemCaseSensitive(root, "default_server");
        cJSON *default_token = cJSON_GetObjectItemCaseSensitive(root, "default_token");
        if (cJSON_IsString(default_url) && default_url->valuestring) copy_string(cfg->url, URL_SIZE, default_url->valuestring);
        if (cJSON_IsString(default_token) && default_token->valuestring) copy_string(cfg->token, TOKEN_SIZE, default_token->valuestring);
    }

    cJSON_Delete(root);
    free(buffer);
}

int main(int argc, char *argv[])
{
    Config cfg = {
        .url = "https://epa.st/upload",
        .token = "",
        .verbose = 0,
        .show_response = 0
    };
    const char *server_name = NULL;
    const char *custom_config = NULL;
    char cli_url[URL_SIZE] = "";
    char cli_token[TOKEN_SIZE] = "";
    int cli_url_set = 0;
    int cli_token_set = 0;
    int init_config_only = 0;
    
    // Parse CLI args first (store overrides, apply later)
    int opt;
    struct option long_opts[] = {
        {"url", required_argument, 0, 'u'},
        {"token", required_argument, 0, 't'},
        {"server", required_argument, 0, 's'},
        {"config", required_argument, 0, 'c'},
        {"init-config", no_argument, 0, 'i'},
        {"verbose", no_argument, 0, 'v'},
        {"response", no_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "u:t:s:c:vrh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'u':
            copy_string(cli_url, URL_SIZE, optarg);
            cli_url_set = 1;
            break;
        case 't':
            copy_string(cli_token, TOKEN_SIZE, optarg);
            cli_token_set = 1;
            break;
        case 's':
            server_name = optarg;
            break;
        case 'c':
            custom_config = optarg;
            break;
        case 'i':
            init_config_only = 1;
            break;
        case 'v':
            cfg.verbose = 1;
            break;
        case 'r':
            cfg.show_response = 1;
            break;
        case 'h':
            print_help(argv[0]);
            return 0;
        default:
            print_help(argv[0]);
            return 1;
        }
    }

    // Config (lowest precedence after defaults)
    const char *config_path = get_config_path(custom_config);

    if (init_config_only) {
        int rc = write_default_config(config_path, cfg.verbose);
        return rc;
    }

    create_default_config_if_missing(config_path, cfg.verbose);
    if (config_path) {
        if (cfg.verbose)
            fprintf(stderr, "[*] Loading config from %s\n", config_path);
        parse_config(config_path, &cfg, server_name);
    }
    
    // Env overrides
    const char *env_url = getenv("PPB_URL");
    const char *env_token = getenv("PPB_TOKEN");
    if (env_url) copy_string(cfg.url, URL_SIZE, env_url);
    if (env_token) copy_string(cfg.token, TOKEN_SIZE, env_token);

    // CLI overrides (highest)
    if (cli_url_set) copy_string(cfg.url, URL_SIZE, cli_url);
    if (cli_token_set) copy_string(cfg.token, TOKEN_SIZE, cli_token);

    if (cfg.verbose) {
        fprintf(stderr, "[*] URL: %s\n", cfg.url);
        fprintf(stderr, "[*] Token: %s\n", cfg.token[0] ? "***" : "(not set)");
    }

    if (cfg.token[0] == '\0') {
        fprintf(stderr, "Error: token is not set. Use --token, PPB_TOKEN, or config file.\n");
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error: failed to initialize CURL\n");
        return 1;
    }
    
    if (cfg.verbose)
        fprintf(stderr, "[*] Initializing upload...\n");
    
    curl_easy_setopt(curl, CURLOPT_URL, cfg.url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, stdin);
    
    // Handle response
    ResponseBuffer response = {0};
    if (cfg.show_response) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    }
    
    char auth_header[TOKEN_SIZE + 32];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", cfg.token);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Error: upload failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (response.data)
            free(response.data);
        return 1;
    }
    
    // Check HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (cfg.verbose)
        fprintf(stderr, "[*] HTTP Status: %ld\n", http_code);
    
    if (cfg.show_response && response.data && response.size > 0) {
        printf("%s\n", response.data);
    }
    
    if (http_code >= 200 && http_code < 300) {
        if (cfg.verbose)
            fprintf(stderr, "[+] Upload successful\n");
    } else if (http_code == 401) {
        fprintf(stderr, "Error: unauthorized (401) - invalid token\n");
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.data)
        free(response.data);
    
    return (http_code >= 200 && http_code < 300) ? 0 : 1;
}
