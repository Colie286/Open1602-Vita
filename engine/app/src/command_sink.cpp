// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_sink.hpp"

#include <utility>

namespace o1602::app {

void CommandSink::submit(sim::Command command) {
    if (m_session != nullptr) {
        m_session->submit(std::move(command));
    } else if (m_simulation != nullptr) {
        m_simulation->submit(std::move(command));
    }
}

}  // namespace o1602::app
