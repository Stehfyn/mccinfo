#pragma once

namespace mccinfo {
namespace fsm {
namespace events {

struct launcher_start{};
struct launcher_terminate{};
struct launch_complete{};
struct launch_abort {};
struct mcc_start {};
struct mcc_terminate{};
struct mcc_found{};

} // namespace events
} // namespace fsm
} // namespace mccinfo