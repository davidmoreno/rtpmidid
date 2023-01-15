# Arq

## Modules

Each module has to be independant, only interface in out
Out may be async, in whatever. And control channel for each, for example for network data.
Can easy connect modules, A -> B, C <-> A.... whatever. 
When remove module, disconnect all.

Modules for:

* Alsa seq
* Direct MIDI
* rtpmidi
* jack midi

```rust
trait MidiInOut {
    async fn write(&self, data: &MidiStream) -> Result<(), Err>;
    async fn read(&self, data: &MidiStream) -> Result<(), Err>;
    async fn close(&self);
}
```

# connect

```rust
fn connect(reader: &MidiInOut, writer: &MidiInOut) -> Result<(), Err>{
    MidiStream data;
    while(true){
        read(data).await?;
        write(data).await?;)
    }
}
```
