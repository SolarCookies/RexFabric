// RexFabric's log category ("rexfabric").

#pragma once

#include <rex/logging.h>

namespace rexfabric {

rex::LogCategoryId LogCategory();

}  // namespace rexfabric

#define RF_DEBUG(...) REXLOG_CAT_DEBUG(::rexfabric::LogCategory(), __VA_ARGS__)
#define RF_INFO(...) REXLOG_CAT_INFO(::rexfabric::LogCategory(), __VA_ARGS__)
#define RF_WARN(...) REXLOG_CAT_WARN(::rexfabric::LogCategory(), __VA_ARGS__)
#define RF_ERROR(...) REXLOG_CAT_ERROR(::rexfabric::LogCategory(), __VA_ARGS__)
