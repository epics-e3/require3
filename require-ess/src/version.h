/* Copyright (C) 2020-2022 European Spallation Source, ERIC */

#pragma once

typedef struct semver_t {
  char *version_str;
  int major;
  int minor;
  int patch;
  int revision; // can be negative; implies that revision has not been
                // specified
  char *test_str;
} semver_t;

void cleanup_semver(semver_t *s);

semver_t *parse_semver(const char *version);
