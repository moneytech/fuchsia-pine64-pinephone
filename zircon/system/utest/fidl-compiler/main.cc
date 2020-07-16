// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zxtest/c/zxtest.h>

// Run tests from both frameworks: unittest and zxtest.
int main(int argc, char** argv) {
  int exitcode = EXIT_SUCCESS;

  const bool success = unittest_run_all_tests(argc, argv);
  if (!success) {
    exitcode = EXIT_FAILURE;
  }

  const bool zxtest_success = RUN_ALL_TESTS(argc, argv) == 0;
  if (!zxtest_success) {
    exitcode = EXIT_FAILURE;
  }
  
  return exitcode;
}
