/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/experimental/TestUtil.h>
#include <gtest/gtest.h>

#include "bistro/bistro/physical/test/utils.h"
#include "bistro/bistro/physical/AllTasksPhysicalResourceMonitor.h"

DECLARE_string(nvidia_smi);
DECLARE_int32(incremental_sleep_ms);

using namespace facebook::bistro;

TEST(AllTasksPhysicalResourceMonitor, FetchAllAndExtractTasks) {
  folly::test::ChangeToTempDir td;
  const uint32_t kTimeoutMs = 5000;  // Minimize flaky tests.
  const std::chrono::milliseconds kPeriod(1000);  // Don't run twice.

  std::vector<pid_t> pids = {1, 3, 7};

  // No `nvidia-smi`, no GPU data.
  FLAGS_nvidia_smi = "";
  {
    auto r =
      AllTasksPhysicalResourceMonitor(kTimeoutMs, kPeriod).getDataOrThrow();
    EXPECT_TRUE(r->gpuInfos_.empty());
    EXPECT_TRUE(r->pidToGpuInfos_.empty());
    EXPECT_TRUE(r->taskGpus(pids.begin(), pids.end()).empty());
  }

  makeShellScript(
    "a",
    "if test $# = 2 -a $1 = --format=csv,noheader,nounits -a "
        "$2 = --query-gpu=gpu_bus_id,memory.used,utilization.gpu,name; then "
      "echo '0000:28:00.0, 1234, 70, Tesla K40m'; "
      "echo '0000:33:00.0, 567, 30, MX400'; "
      "echo '0000:0E:00.0, 890, 99, Quadro M6000'; "
    "elif test $# = 2 -a $1 = --format=csv,noheader,nounits -a "
        "$2 = --query-compute-apps=pid,gpu_bus_id; then "
      "echo '5, 0000:28:00.0'; "
      "echo '6, 0000:28:00.0'; "
      "echo '2, 0000:28:00.0'; "
      "echo '1, 0000:33:00.0'; "
      "echo '3, 0000:33:00.0'; "
      "echo '5, 0000:33:00.0'; "
      "echo '7, 0000:0E:00.0'; "
      "echo '3, 0000:0E:00.0'; "
      "echo '2, 0000:0E:00.0'; "
    "else "
      "exit 37;"
    "fi"
  );
  FLAGS_nvidia_smi = "a";

  std::vector<cpp2::GPUInfo> all_gpus;
  all_gpus.emplace_back();
  *all_gpus.back().pciBusID_ref() = "0000:28:00.0";
  *all_gpus.back().name_ref() = "Tesla K40m";
  *all_gpus.back().memoryMB_ref() = 1234;
  *all_gpus.back().compute_ref() = 0.7;
  all_gpus.emplace_back();
  *all_gpus.back().pciBusID_ref() = "0000:33:00.0";
  *all_gpus.back().name_ref() = "MX400";
  *all_gpus.back().memoryMB_ref() = 567;
  *all_gpus.back().compute_ref() = 0.3;
  all_gpus.emplace_back();
  *all_gpus.back().pciBusID_ref() = "0000:0E:00.0";
  *all_gpus.back().name_ref() = "Quadro M6000";
  *all_gpus.back().memoryMB_ref() = 890;
  *all_gpus.back().compute_ref() = 0.99;

  auto expected_task_gpus = all_gpus;
  expected_task_gpus.erase(expected_task_gpus.begin());

  {
    auto r =
      AllTasksPhysicalResourceMonitor(kTimeoutMs, kPeriod).getDataOrThrow();
    EXPECT_EQ(all_gpus, r->gpuInfos_);
    auto task_gpus = r->taskGpus(pids.begin(), pids.end());
    std::sort(
        task_gpus.begin(),
        task_gpus.end(),
        [](const cpp2::GPUInfo& a, const cpp2::GPUInfo& b) {
          return *a.pciBusID_ref() > *b.pciBusID_ref();
        });
    EXPECT_EQ(expected_task_gpus, task_gpus);
  }
}
