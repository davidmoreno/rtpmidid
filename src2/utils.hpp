#include "json_fwd.hpp"
#include "rtpmidid/rtppeer.hpp"

namespace rtpmididns {
json_t peer_status(rtpmidid::rtppeer_t &peer, const std::string &hostname,
                   const std::string &port);
};