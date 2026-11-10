// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace o1602::platform::vita {
// No SDK headers: exercise partial startup and rollback on the host as well.
struct NetworkOps {
    int (*module_load)();
    int (*net_init)();
    int (*ctl_init)();
    void (*ctl_term)();
    int (*net_term)();
    int (*module_unload)();
    void (*record)(const char*, int);
};

class NetworkLifecycle {
public:
    bool ready() const noexcept { return ctl_; }
    bool start(const NetworkOps& ops) {
        if (ready()) return true;
        if (!module_) {
            const int module=ops.module_load();ops.record("module_rc",module);
            if (module<0) return false;
            module_=true;
        }
        if (!net_) {
            const int net=ops.net_init();ops.record("net_init_rc",net);
            if (net<0) { stop(ops);return false; }
            net_=true;
        }
        const int ctl=ops.ctl_init();ops.record("ctl_init_rc",ctl);
        if (ctl<0) { stop(ops);return false; }
        ctl_=true;
        return true;
    }
    bool stop(const NetworkOps& ops) {
        if (ctl_) { ops.ctl_term();ops.record("ctl_term",0);ctl_=false; }
        if (net_) {
            const int rc=ops.net_term();ops.record("net_term_rc",rc);
            if (rc<0) return false;  // preserve ownership for a retry; do not unload beneath it
            net_=false;
        }
        if (module_) {
            const int rc=ops.module_unload();ops.record("module_unload_rc",rc);
            if (rc<0) return false;
            module_=false;
        }
        return true;
    }
private:
    bool module_=false,net_=false,ctl_=false;
};
} // namespace o1602::platform::vita
