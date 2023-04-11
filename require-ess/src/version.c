/* Copyright (C) 2020-2022 European Spallation Source, ERIC */

#include "version.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cleanup_semver(semver_t *s) {
  free(s->version_str);
  free(s);
}

static semver_t *init_semver(const char *version) {
  semver_t *s = (semver_t *)calloc(1, sizeof(semver_t));

  s->version_str = calloc(strlen(version) + 1, sizeof(char));
  strcpy(s->version_str, version);
  s->test_str = "";
  s->revision = -1;

  return s;
}

static void fetch_part(char *version_str, const regmatch_t *groups,
                       unsigned int ix, int *part) {
  char tmp;

  tmp = version_str[groups[ix].rm_eo];
  version_str[groups[ix].rm_eo] = 0;
  *part = atoi(version_str + groups[ix].rm_so);
  version_str[groups[ix].rm_eo] = tmp;
}

semver_t *parse_semver(const char *version) {
  static const char *version_regex =
      "^(([0-9]+)\\.([0-9]+)\\.([0-9]+))?([^+]*)(\\+([0-9]+))?$";
  static const unsigned int max_regex_groups = 7 + 1;
  static const unsigned int major_ix = 2;
  static const unsigned int minor_ix = 3;
  static const unsigned int patch_ix = 4;
  static const unsigned int test_label_ix = 5;
  static const unsigned int revision_ix = 7;
  semver_t *s = init_semver(version);

  regex_t compiled;
  regmatch_t groups[max_regex_groups];

  if (s->version_str == NULL || s->version_str[0] == 0) {
    return NULL;
  }

  if (regcomp(&compiled, version_regex, REG_EXTENDED)) {
    return NULL;
  }

  if (regexec(&compiled, s->version_str, max_regex_groups, groups, 0) == 0) {
    fetch_part(s->version_str, groups, major_ix, &s->major);
    fetch_part(s->version_str, groups, minor_ix, &s->minor);
    fetch_part(s->version_str, groups, patch_ix, &s->patch);
    if (groups[test_label_ix].rm_so != groups[test_label_ix].rm_eo) {
      s->test_str = s->version_str;
    }
    if (groups[revision_ix].rm_so != (regoff_t)-1) {
      fetch_part(s->version_str, groups, revision_ix, &s->revision);
      s->version_str[groups[revision_ix].rm_so - 1] = 0;
    }
  }
  return s;
}
