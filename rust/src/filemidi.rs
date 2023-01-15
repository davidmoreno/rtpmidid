use async_trait::async_trait;
use std::io::Result;
use tokio::{
    fs::{File, OpenOptions},
    io::{AsyncReadExt, AsyncWriteExt},
};

use crate::{midiinout::MidiInOut, midistream::MidiStream};

struct FileMidi {
    input: File,
    // If has Some, use it, if not input is both input and output
    output: Option<File>,
}

impl FileMidi {
    pub async fn open(filename: &str) -> Result<FileMidi> {
        let mut options = OpenOptions::new();
        options.read(true);
        options.write(true);
        let input = options.open(filename).await?;

        Ok(FileMidi {
            input,
            output: None,
        })
    }
    pub async fn open_io(input_filename: &str, output_filename: &str) -> Result<FileMidi> {
        let mut options = OpenOptions::new();
        options.read(true);
        let input = options.open(input_filename).await?;

        let mut options = OpenOptions::new();
        options.write(true);
        let output = options.open(input_filename).await?;

        Ok(FileMidi {
            input,
            output: Some(output),
        })
    }
}

#[async_trait]
impl MidiInOut for FileMidi {
    async fn write(&mut self, data: &mut MidiStream) -> Result<()> {
        let output = match &mut self.output {
            Some(file) => file,
            None => &mut self.input,
        };
        debug!("Write {:?}", data.read_slice());
        output.write(data.read_slice()).await?;
        Ok(())
    }
    async fn read(&mut self, data: &mut MidiStream) -> Result<()> {
        let length = self.input.read(data.write_slice()).await?;
        data.advance_read(length);
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
            let mut filemidi = FileMidi::open(filename).await.unwrap();
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
