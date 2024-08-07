# rtpmidid::rtpclient_t

Can add `address:port` pairs and then try to connect to each of the resolved
hosts and port for each pair (can be in name:portname mode, and that could be
thousands of options, or at least several A and AAAA records).

Then for each real IP/IPv6 and port, try to create the local ports for that
protocol (IP or IPv6). Then sends the connect packet to the control port,
and on reception sends the connect to midi. When received the answer, then
the connection is made. If any of these fail, try next ip:port. If all ip:port
fails for this address, try next addres. And if all these fail, then we
really failed.

Externally we can be ready, connecting or connected. No failure should be knopwn
by users of this class. At most it will take some time or fail.

External interface is resumed:

```c++
auto status_change_connection = client->peer.status_change_event.connect(callback_when_connected_or_fail);
client->add_server_address(address, port);
client->add_server_address(address2, port);
```

And then use the `client->peer` as needed.

After connecting keeps running the state machine mainly for CK packets management.

## state machine

```mermaid
stateDiagram
    WaitToStart --> PrepareNextDNS: Started
    PrepareNextDNS --> ResolveNextIpPort: NextReady
    PrepareNextDNS --> Error: ResolveListExhausted
    ResolveNextIpPort --> PrepareNextDNS: ConnectListExhausted
    ResolveNextIpPort --> ResolveNextIpPort: ResolveFailed
    ResolveNextIpPort --> ConnectControl: Resolved
    ConnectControl --> ResolveNextIpPort: ConnectFailed
    ConnectControl --> ConnectMidi: Connected
    ConnectMidi --> AllConnected: Connected
    ConnectMidi --> DisconnectControl: ConnectFailed
    DisconnectControl --> ResolveNextIpPort: ConnectFailed

    AllConnected --> SendCkShort: SendCK

    # the ck sends 6 short packets (wait only 2 seconds), and then long ones (60 seconds).
    SendCkShort --> WaitSendCkShort: WaitSendCK (*6)
    SendCkShort --> WaitSendCkLong: LatencyMeasured
    SendCkShort --> DisconnectBecauseCKTimeout: Timeout

    WaitSendCkShort --> SendCkShort: SendCK

    SendCkLong --> WaitSendCkLong: WaitSendCK1
    SendCkLong --> DisconnectBecauseCKTimeout: Timeout
    WaitSendCkLong --> SendCkLong: SendCK

    DisconnectBecauseCKTimeout --> ConnectControl: ConnectFailed

    Error --> PrepareNextDNS: Connect
```
