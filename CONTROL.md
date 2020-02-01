# Control protocol

rtpmidi can be controlled using a UNIX socket file. By default it is at
`/var/run/rtpmidid.sock`.

There are two options for the protocol: line based and JSON based. Answers are
always in JSON.

Line based is more basic and allows limited options, only plain parameters, space
separated. JSON allows more complex operation.

The line based option is for manual testing, for example:

```shell
$ socat /var/run/rtpmidid.sock
stats
{
    "version": "0.0.1",
    "uptime": 200.0,
    "alsa": [

    ],
    "remote": [

    ]
}
```

When using the JSON prototol it uses JSON RPC

```
$ echo '{"method": "stats", "params": [], "id": 1}' | socat /var/run/rtpmidid.sock
{"version": "0.0.1","uptime": 200.0,"alsa": [],"remote": []}
```

# Commands

## stats

Shows stats about the current connections.
