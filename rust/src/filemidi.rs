use async_trait::async_trait;
use std::io::Result;
use tokio::{
    fs::{File, OpenOptions},
    io::AsyncWriteExt,
};

use crate::{midiinout::MidiInOut, midistream::MidiStream};

struct FileMidi {
    file: File,
}

impl FileMidi {
    pub async fn new(filename: &str) -> Result<FileMidi> {
        let mut options = OpenOptions::new();
        // options.read(true);
        options.write(true);
        let mut file = options.open(filename).await?;

        Ok(FileMidi { file: file })
    }
}

#[async_trait]
impl MidiInOut for FileMidi {
    async fn write(&mut self, data: &mut MidiStream) -> Result<()> {
        debug!("Write {:?}", data.read_slice());
        self.file.write(data.read_slice()).await?;
        Ok(())
    }
    async fn read(&mut self, data: &mut MidiStream) -> Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::MidiInOut;
    use crate::{filemidi::FileMidi, midistream::MidiStream, setup_logging};
    use async_trait::async_trait;
    use nix::sys::stat;
    use nix::unistd;
    use std::io::Result;
    use std::time::Duration;
    use tokio;
    use tokio::fs::File;
    use tokio::io::AsyncReadExt;

    #[tokio::test]
    async fn test_basic_filemidi() {
        let filename = "/tmp/testmidi.midi";
        unistd::unlink(filename);
        unistd::mkfifo(filename, stat::Mode::S_IRWXU).unwrap();

        tokio::spawn(async {
            let mut filemidi = FileMidi::new(filename).await.unwrap();
            let mut mididata = MidiStream::new();
            mididata.write(&[0x90, 0x64, 0x7f]).unwrap();
            info!("Writing");
            filemidi.write(&mut mididata).await.unwrap();
            info!("Wrote");
            // tokio::time::sleep(Duration::from_millis(1000)).await;
        });

        // tokio::time::sleep(Duration::from_millis(100)).await;

        info!("Read");
        let mut reader = File::open(filename).await.unwrap();
        let mut data = vec![0; 256];
        let len = reader.read(&mut data).await.unwrap();
        info!("Read from filemidi {}: {:?}", len, &data[0..len]);
        assert_eq!(len, 3);
    }
}
