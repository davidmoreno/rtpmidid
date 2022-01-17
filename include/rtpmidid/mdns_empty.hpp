#pragma once

#include <string>
#include <rtpmidid/signal.hpp>


namespace rtpmidid
{
  struct announcement_t
  {
    std::string name;
    int port;
  };

  class mdns
  {

  public:
    // name, address, port
    signal_t<const std::string &, const std::string &, const std::string &>
        discover_event;
    signal_t<const std::string &> remove_event;

    mdns();
    ~mdns();
    void setup_mdns_browser();
    void announce_all();
    void announce_rtpmidi(const std::string &name, const int32_t port);
    void unannounce_rtpmidi(const std::string &name, const int32_t port);
  };
}