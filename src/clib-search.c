//
// clib-search.c
//
// Copyright (c) 2012-2021 clib authors
// MIT licensed
//

#include "asprintf/asprintf.h"
#include "case/case.h"
#include "commander/commander.h"
#include "common/clib-cache.h"
#include "common/clib-package.h"
#include "console-colors/console-colors.h"
#include "debug/debug.h"
#include "fs/fs.h"
#include "http-get/http-get.h"
#include "logger/logger.h"
#include "parson/parson.h"
#include "strdup/strdup.h"
#include "tempdir/tempdir.h"
#include "version.h"
#include "wiki-registry/wiki-registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLIB_WIKI_URL "https://github.com/clibs/clib/wiki/Packages"
#define CLIB_SEARCH_CACHE_TIME 1 * 24 * 60 * 60

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) ||               \
    defined(__MINGW64__) || defined(__CYGWIN__)
#define setenv(k, v, _) _putenv_s(k, v)
#define realpath(a, b) _fullpath(a, b, strlen(a))
#endif

debug_t debugger;

static int opt_color;
static int opt_cache;
static int opt_json;

static void setopt_nocolor(command_t *self) { opt_color = 0; }

static void setopt_nocache(command_t *self) { opt_cache = 0; }

static void setopt_json(command_t *self) { opt_json = 1; }

#define COMPARE(v)                                                             \
  {                                                                            \
    if (NULL == v) {                                                           \
      rc = 0;                                                                  \
      goto cleanup;                                                            \
    }                                                                          \
    case_lower(v);                                                             \
    for (int i = 0; i < count; i++) {                                          \
      if (strstr(v, args[i])) {                                                \
        rc = 1;                                                                \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
  }

static int matches(int count, char *args[], wiki_package_ptr_t pkg) {
  // Display all packages if there's no query
  if (0 == count)
    return 1;

  char *description = NULL;
  char *name = NULL;
  char *repo = NULL;
  char *href = NULL;
  int rc = 0;

  name = clib_package_parse_name(wiki_package_get_repo(pkg));
  COMPARE(name);

  description = strdup(wiki_package_get_description(pkg));
  COMPARE(description);

  repo = strdup(wiki_package_get_repo(pkg));
  COMPARE(repo);

  href = strdup(wiki_package_get_href(pkg));
  COMPARE(href);

cleanup:
  free(description);
  free(name);
  free(repo);
  free(href);
  return rc;
}

static char *wiki_html_cache() {

  if (clib_cache_has_search() && opt_cache) {
    char *data = clib_cache_read_search();

    if (data) {
      return data;
    }
  }

  debug(&debugger, "setting cache from %s", CLIB_WIKI_URL);
  http_get_response_t *res = http_get(CLIB_WIKI_URL);
  if (!res->ok)
    return NULL;

  char *html = strdup(res->data);
  if (NULL == html)
    return NULL;
  http_get_free(res);

  if (NULL == html)
    return html;
  clib_cache_save_search(html);
  debug(&debugger, "wrote cache");
  return html;
}

static void display_package(const wiki_package_ptr_t pkg,
                            cc_color_t fg_color_highlight,
                            cc_color_t fg_color_text) {
  cc_fprintf(fg_color_highlight, stdout, "  %s\n", wiki_package_get_repo(pkg));
  printf("  url: ");
  cc_fprintf(fg_color_text, stdout, "%s\n", wiki_package_get_href(pkg));
  printf("  desc: ");
  cc_fprintf(fg_color_text, stdout, "%s\n", wiki_package_get_description(pkg));
  printf("\n");
}

static void add_package_to_json(const wiki_package_ptr_t pkg,
                                JSON_Array *json_list) {
  JSON_Value *json_pkg_root = json_value_init_object();
  JSON_Object *json_pkg = json_value_get_object(json_pkg_root);

  json_object_set_string(json_pkg, "repo", wiki_package_get_repo(pkg));
  json_object_set_string(json_pkg, "href", wiki_package_get_href(pkg));
  json_object_set_string(json_pkg, "description", wiki_package_get_description(pkg));
  json_object_set_string(json_pkg, "category", wiki_package_get_category(pkg));

  json_array_append_value(json_list, json_pkg_root);
}

int main(int argc, char *argv[]) {
  opt_color = 1;
  opt_cache = 1;

  debug_init(&debugger, "clib-search");

  clib_cache_init(CLIB_SEARCH_CACHE_TIME);

  command_t program;
  command_init(&program, "clib-search", CLIB_VERSION);
  program.usage = "[options] [query ...]";

  command_option(&program, "-n", "--no-color", "don't colorize output",
                 setopt_nocolor);

  command_option(&program, "-c", "--skip-cache", "skip the search cache",
                 setopt_nocache);

  command_option(&program, "-j", "--json", "generate a serialized JSON output",
                 setopt_json);

  command_parse(&program, argc, argv);

  for (int i = 0; i < program.argc; i++)
    case_lower(program.argv[i]);

  // set color theme
  cc_color_t fg_color_highlight = opt_color ? CC_FG_DARK_CYAN : CC_FG_NONE;
  cc_color_t fg_color_text = opt_color ? CC_FG_DARK_GRAY : CC_FG_NONE;


  wiki_registry_ptr_t registry = wiki_registry_create(CLIB_WIKI_URL);
  wiki_registry_fetch(registry);

  // TODO, implement caching for the new registries.
  /*
    char *html = wiki_html_cache();
    if (NULL == html) {
        command_free(&program);
        logger_error("error", "failed to fetch wiki HTML");
        return 1;
    }
  list_t *pkgs = wiki_registry_parse(html);
  free(html);
  debug(&debugger, "found %zu packages", pkgs->len);
   */

  wiki_package_ptr_t pkg;
  wiki_registry_iterator_t it = wiki_registry_iterator_new(registry);

  JSON_Array *json_list = NULL;
  JSON_Value *json_list_root = NULL;

  if (opt_json) {
    json_list_root = json_value_init_array();
    json_list = json_value_get_array(json_list_root);
  }

  printf("\n");

  while ((pkg = wiki_registry_iterator_next(it))) {
    if (matches(program.argc, program.argv, pkg)) {
      if (opt_json) {
        add_package_to_json(pkg, json_list);
      } else {
        display_package(pkg, fg_color_highlight, fg_color_text);
      }
    } else {
      debug(&debugger, "skipped package %s", wiki_package_get_repo(pkg));
    }
  }

  if (opt_json) {
    char *serialized = json_serialize_to_string_pretty(json_list_root);
    puts(serialized);

    json_free_serialized_string(serialized);
    json_value_free(json_list_root);
  }

  wiki_registry_iterator_destroy(it);
  wiki_registry_free(registry);
  command_free(&program);
  return 0;
}
