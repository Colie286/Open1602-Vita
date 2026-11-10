// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/connectivity.hpp"
#include "enet_library.hpp"
namespace o1602::net {
namespace { Connectivity* backend = nullptr; }
void install_connectivity(Connectivity* value) { backend = value; }
Connectivity* connectivity() { return backend; }
bool network_in_use() { return detail::EnetLibrary::in_use(); }
}
