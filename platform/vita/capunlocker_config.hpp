// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
namespace o1602::platform::vita {
// Read-only evidence of an enabled kernel entry, never proof that a hook loaded.
// Callers must also check the file and the actual affinity permission.
inline std::string capunlocker_entry(std::string_view config) {
    bool kernel=false;
    while(!config.empty()){
        const auto end=config.find('\n');auto line=config.substr(0,end);
        if(end==std::string_view::npos)config={};else config.remove_prefix(end+1);
        if(const auto comment=line.find('#');comment!=std::string_view::npos)line=line.substr(0,comment);
        const auto first=line.find_first_not_of(" \t\r");if(first==std::string_view::npos)continue;
        line=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
        if(line.front()=='*'){kernel=line=="*KERNEL";continue;}
        if(!kernel)continue;
        if(line.size()>1&&line.front()=='"'&&line.back()=='"')line=line.substr(1,line.size()-2);
        std::string lower(line);std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
        const auto slash=lower.find_last_of('/');const auto name=lower.substr(slash==std::string::npos?0:slash+1);
        if((lower.starts_with("ur0:")||lower.starts_with("ux0:"))&&
           (name=="capunlocker.skprx"||name=="coreunlocker80000h.skprx"))return std::string(line);
    }
    return {};
}
}
