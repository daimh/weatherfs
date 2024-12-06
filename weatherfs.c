/*
weatherfs: realtime weather info by zipcode in a FUSE filesystem

MIT License

Copyright (C) 2024 Manhong Dai http://github.com/daimh/weatherfs

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#define FUSE_USE_VERSION 31
#define ZIPCODE_LEN_MAX 5
#define URL_LEN_MAX 128
#define FILE_SIZE_MAX 10240

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <curl/curl.h>
#include <jansson.h>
#include <syslog.h>

static struct options {
	const char *conf;
	int show_help;
	int show_version;
} options;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--conf=%s", conf),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	OPTION("--version", show_version),
	FUSE_OPT_END
};

const char* apikey = NULL;
char (*ziparr)[ZIPCODE_LEN_MAX+1] = NULL;
size_t zipcnt = 0;

static int cmpzip(const void *p1, const void *p2) {
	return strcmp((const char *) p1, (const char *) p2);
}

static void *wfs_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg) {
	(void) conn;
	cfg->kernel_cache = 1;
	return NULL;
}

static int wfs_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi) {
	(void) fi;
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (bsearch(path+1, ziparr, zipcnt, ZIPCODE_LEN_MAX+1,
			cmpzip) != NULL) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = FILE_SIZE_MAX;
	} else {
		res = -ENOENT;
	}
	return res;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags) {
	(void) offset;
	(void) fi;
	(void) flags;
	size_t i;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	for (i=0; i < zipcnt; i++)
		filler(buf, ziparr[i], NULL, 0, 0);
	return 0;
}

static int wfs_open(const char *path, struct fuse_file_info *fi) {
	if (bsearch(path+1, ziparr, zipcnt, ZIPCODE_LEN_MAX+1, cmpzip) == NULL)
		return -ENOENT;
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;
	return 0;
}

struct buffer {
	char *buf;
	const size_t size;
	const off_t offset;
	size_t total;
};

json_t* load2json(struct buffer *chunk) {
	json_error_t err;
	assert(!chunk->offset);
	json_t *root = json_loads(chunk->buf, 0, &err);
	if (!root)
		return NULL;
	if(!json_is_object(root)) {
		syslog(LOG_USER, "weatherfs: failed to load json response");
		json_decref(root);
		return root;
	}
	return root;
}

int get_real(json_t *root, const char *field, double *dbl) {
	json_t *val = json_object_get(root, field);
	if (!json_is_real(val))
		return 0;
	*dbl = json_real_value(val);
	return 1;
}

void strcpy2buffer(struct buffer *chunk, const char *src);

void memcpy2buffer(struct buffer *chunk, const char *src, const size_t len) {
	size_t ret = chunk->size;
	assert(len + 2 <= ret);
	if (chunk->total + len > chunk->offset + chunk->size) {
		assert(chunk->offset == 0);
		chunk->total = 0;
		return strcpy2buffer(chunk, "Fuse buffer is too small");
	}
	if (chunk->offset >= len)
		return;
	if (ret + chunk->offset > len)
		ret = len - chunk->offset;
	memcpy(chunk->buf, src + chunk->offset, ret);
	chunk->buf[ret++] = '\n';
	chunk->buf[ret] = 0;
	chunk->total += ret;
}

void strcpy2buffer(struct buffer *chunk, const char *src) {
	return memcpy2buffer(chunk, src, strlen(src));
}

size_t write_callback(char *data, size_t size, size_t nmemb, void *chunk) {
	size_t realsize = size * nmemb;
	memcpy2buffer(chunk, data, realsize);
	return realsize;
}

int openweathermap(struct buffer *chunk, char *url) {
	CURL *curl;
	CURLcode res;
	curl = curl_easy_init();
	if(!curl) {
		strcpy2buffer(chunk, "Failed to init curl");
		return 0;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, chunk->size);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if(res != CURLE_OK) {
		strcpy2buffer(chunk, curl_easy_strerror(res));
		return 0;
	}
	return 1;
}

size_t copy2chunk(const char *src, char *buf, size_t size, off_t offset) {
	size_t len = strlen(src) + 1;
	if (offset >= len)
		return 0;
	if (offset + size > len)
		size = len - offset;
	memcpy(buf, src + offset, size);
	buf[size-1] = '\n';
	return size;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	(void) fi;
	char url[URL_LEN_MAX];
	struct buffer chunk = {buf, size, offset, 0};
	double lat, lon;
	json_t *root;
	if (bsearch(path+1, ziparr, zipcnt, ZIPCODE_LEN_MAX+1, cmpzip) == NULL)
		return -ENOENT;
	assert(URL_LEN_MAX > snprintf(url, URL_LEN_MAX,
		"https://api.openweathermap.org/geo/1.0/zip?zip=%s,US&appid=%s",
		path+1, apikey));
	if (!openweathermap(&chunk, url))
		return chunk.total + 1;
	root = load2json(&chunk);
	if (!get_real(root, "lat", &lat) || !get_real(root, "lon", &lon))
		return chunk.total + 1;
	assert(URL_LEN_MAX > snprintf(
		url,
		URL_LEN_MAX,
		"https://api.openweathermap.org/data/2.5/weather?lat=%lf&lon=%lf&appid=%s",
		lat,
		lon,
		apikey));
	json_decref(root);
	chunk.total = 0;
	if (!openweathermap(&chunk, url))
		return chunk.total + 1;
	root = load2json(&chunk);
	chunk.total = json_dumpb(root, chunk.buf, FILE_SIZE_MAX-1, JSON_INDENT(2));
	chunk.buf[chunk.total++] = '\n';
	chunk.buf[chunk.total] = 0;
	return chunk.total;
}

static const struct fuse_operations wfs_oper = {
	.init		= wfs_init,
	.getattr	= wfs_getattr,
	.readdir	= wfs_readdir,
	.open		= wfs_open,
	.read		= wfs_read,
};

static void show_version() {
	printf("weatherfs 20241207\n"
		"Copyright (C) 2024 Manhong Dai\n"
		"License MIT\n\n");
}

static void show_help(const char *progname) {
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
		"    --conf=<s>          Name of the setting json file\n"
		"                        (default: \"weatherfs.json\")\n");
}

int read_conf(const char *conf) {
	json_error_t err;
	json_t *root = json_load_file(conf, 0, &err);
	json_t *val, *element;
	size_t i;
	if(!json_is_object(root)) {
		fprintf(stderr, "Failed to load the conf file\n");
		json_decref(root);
		return 0;
	}
	val = json_object_get(root, "apikey");
	if(!json_is_string(val)) {
		fprintf(stderr, "Failed to find apikey in the conf file\n");
		json_decref(root);
		return 0;
	}
	apikey = strdup(json_string_value(val));
	val = json_object_get(root, "zipcode");
	if(!json_is_array(val)) {
		fprintf(stderr, "Failed to find zipcodes in the conf file\n");
		json_decref(root);
		return 0;
	}
	zipcnt = json_array_size(val);
	ziparr = reallocarray(ziparr, zipcnt, ZIPCODE_LEN_MAX+1);
	for(i = 0; i < zipcnt; i++) {
		element = json_array_get(val, i);
		if(!json_is_string(element)) {
			fprintf(stderr, "Failed to find zipcode in the conf file\n");
			json_decref(root);
			return 0;
		}
		snprintf(ziparr[i], ZIPCODE_LEN_MAX+1, "%s", json_string_value(element));
	}
	json_decref(root);
	qsort(ziparr, zipcnt, ZIPCODE_LEN_MAX+1, cmpzip);
	return 1;
}

int main(int argc, char *argv[]) {
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	options.conf = strdup("weatherfs.json");
	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;
	if (options.show_version) {
		show_version();
		assert(!fuse_opt_add_arg(&args, "--version"));
	}
	if (options.show_help) {
		show_help(argv[0]);
		assert(!fuse_opt_add_arg(&args, "--help"));
		args.argv[0][0] = '\0';
	} else if (strlen(options.conf) == 0) {
		printf("missing --conf\n");
		return 1;
	} else if (!read_conf(options.conf)) {
		return 1;
	}
	ret = fuse_main(args.argc, args.argv, &wfs_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
