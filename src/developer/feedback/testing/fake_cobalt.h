// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_FAKE_COBALT_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_FAKE_COBALT_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/test/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

// A wrapper for getting events from a mock cobalt component in integration tests.
class FakeCobalt {
 public:
  FakeCobalt(std::shared_ptr<sys::ServiceDirectory> environment_services);
  ~FakeCobalt();

  template <typename EventCodeType>
  std::vector<EventCodeType> GetAllEventsOfType(size_t num_expected,
                                                fuchsia::cobalt::test::LogMethod log_method);

 private:
  template <typename EventCodeType>
  void GetNewEventsOfType(const fuchsia::cobalt::test::LoggerQuerier_WatchLogs_Result& result,
                          std::vector<EventCodeType>* all_events);

  fuchsia::cobalt::test::LoggerQuerierSyncPtr logger_querier_;
};

FakeCobalt::FakeCobalt(std::shared_ptr<sys::ServiceDirectory> environment_services) {
  environment_services->Connect(logger_querier_.NewRequest());
}

FakeCobalt::~FakeCobalt() {
  using fuchsia::cobalt::test::LogMethod;
  FX_CHECK(logger_querier_) << "logger_querier_ disconnected. Cannot reset mock_cobalt, aborting";

  // Reset the logger so tests can be run repeatedly.
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_EVENT) == ZX_OK)
      << "Failed to reset EVENT events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_EVENT_COUNT) == ZX_OK)
      << "Failed to reset EVENT_COUNT events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_ELAPSED_TIME) == ZX_OK)
      << "Failed to reset ELAPSED_TIME events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_FRAME_RATE) == ZX_OK)
      << "Failed to reset FRAME_RATE events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_MEMORY_USAGE) == ZX_OK)
      << "Failed to reset MEMORY_USAGE events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_INT_HISTOGRAM) == ZX_OK)
      << "Failed to reset INT_HISTOGRAM events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_COBALT_EVENT) == ZX_OK)
      << "Failed to reset COBALT_EVENT events, aborting";
  FX_CHECK(logger_querier_->ResetLogger(kProjectId, LogMethod::LOG_COBALT_EVENTS) == ZX_OK)
      << "Failed to reset COBALT_EVENTS events, aborting";
}

template <typename EventCodeType>
std::vector<EventCodeType> FakeCobalt::GetAllEventsOfType(
    size_t num_expected, fuchsia::cobalt::test::LogMethod log_method) {
  fuchsia::cobalt::test::LoggerQuerier_WatchLogs_Result result;
  std::vector<EventCodeType> all_events;

  // We may need to run WatchLogs() multiple times to collect all of the events generated by
  // our component. This is due to the fact that we are communicating with both the
  // fuchsia.cobalt/Logger and fuchsia.cobalt.test/LoggerQuerier APIs and are provided no
  // guarantees regarding the order in which messages are received. Thus it's conceivable (and
  // will actually happen quite often) that the call to WatchLogs() (and maybe even ResetLogger())
  // will get to the component serving both APIs before either of the calls to LogEvent() arrive
  // and a response containing zero or one Cobalt events is received. So, if you wish to remove
  // this while loop it is a prerequisite that you have figured out a way to guarantee the
  // ordering of independent, asynchronous messages, made it so that you component only ever
  // logs to Cobalt, or don't care about flakes in your tests.
  while (all_events.size() < num_expected) {
    logger_querier_->WatchLogs(kProjectId, log_method, &result);
    GetNewEventsOfType<EventCodeType>(result, &all_events);
  }

  return all_events;
}

template <typename EventCodeType>
void FakeCobalt::GetNewEventsOfType(
    const fuchsia::cobalt::test::LoggerQuerier_WatchLogs_Result& result,
    std::vector<EventCodeType>* all_events) {
  FXL_CHECK(result.is_response());

  const auto& response = result.response();
  for (const auto& event : response.events) {
    FXL_CHECK(event.metric_id == MetricIDForEventCode(static_cast<EventCodeType>(0)))
        << "Expected metric id: "
        << std::to_string(MetricIDForEventCode(static_cast<EventCodeType>(0))) << "\n"
        << "Actual metic id:    " << std::to_string(event.metric_id);
    for (const auto& event_code : event.event_codes) {
      all_events->push_back(static_cast<EventCodeType>(event_code));
    }
  }
}

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_FAKE_COBALT_H_
