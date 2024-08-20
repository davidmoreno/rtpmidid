// This is an autogenerated file. It should be included at header
public:
/// for the header / class

enum state_e{
    Error,
    WaitToStart,
    PrepareNextDNS,
    ResolveNextIpPort,
    ConnectControl,
    ConnectMidi,
    AllConnected,
    DisconnectControl,
    SendCkShort,
    WaitSendCkShort,
    WaitSendCkLong,
    DisconnectBecauseCKTimeout,
    SendCkLong,
};

static const char *to_string(state_e state);

enum event_e{
    Started,
    NextReady,
    ResolveListExhausted,
    ConnectListExhausted,
    ResolveFailed,
    Resolved,
    ConnectFailed,
    Connected,
    SendCK,
    WaitSendCK,
    LatencyMeasured,
    Timeout,
    WaitSendCK1,
    Connect,
};

static const char *to_string(event_e event);

state_e state = state_e::WaitToStart;

void handle_event(event_e event);

protected:
void state_error();
void state_wait_to_start();
void state_prepare_next_dns();
void state_resolve_next_ip_port();
void state_connect_control();
void state_connect_midi();
void state_all_connected();
void state_disconnect_control();
void state_send_ck_short();
void state_wait_send_ck_short();
void state_wait_send_ck_long();
void state_disconnect_because_cktimeout();
void state_send_ck_long();