// SPDX-License-Identifier: GPL-3.0-or-later
//
// enet_initialize() once for everything of the process that uses ENet (transports, LAN search).
#pragma once

#include <enet/enet.h>
#include <mutex>

namespace o1602::net::detail {

class EnetLibrary {
public:
    static bool in_use() {
        const std::lock_guard lock(mutex());
        return count() != 0;
    }
    /// False if ENet cannot start; every true is matched by a release().
    static bool acquire() {
        const std::lock_guard lock(mutex());
        if (count() == 0 && enet_initialize() != 0) {
            return false;
        }
        ++count();
        return true;
    }
    static void release() {
        const std::lock_guard lock(mutex());
        if (count() == 0) {
            return;
        }
        --count();
        if (count() == 0) {
            enet_deinitialize();
        }
    }

private:
    static std::mutex& mutex() {
        static std::mutex m;
        return m;
    }
    static int& count() {
        static int c = 0;
        return c;
    }
};

}  // namespace o1602::net::detail
