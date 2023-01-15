use crate::midistream::MidiStream;
use async_trait::async_trait;
use std::io::Result;

#[async_trait]
pub trait MidiInOut {
    async fn write(&mut self, data: &mut MidiStream) -> Result<()>;
    async fn read(&mut self, data: &mut MidiStream) -> Result<()>;
}

#[cfg(test)]
mod tests {
    use super::MidiInOut;
    use crate::{midistream::MidiStream, setup_logging};
    use async_trait::async_trait;
    use std::io::Result;
    use tokio;

    pub struct LoggerOut {}

    #[async_trait]
    impl MidiInOut for LoggerOut {
        async fn write(&mut self, data: &mut MidiStream) -> Result<()> {
            let mut bytes = vec![0; 1500];
            let len = data.read(&mut bytes)?;
            info!("Write data {}: {:?}", len, &bytes[0..len]);
            println!("TEST");
            Ok(())
        }
        async fn read(&mut self, data: &mut MidiStream) -> Result<()> {
            Ok(())
        }
    }

    #[tokio::test]
    async fn test_write() {
        setup_logging();

        let mut data = MidiStream::new();
        data.write(&[0x90, 0x65, 0x7f]).unwrap();
        let mut logger_out = LoggerOut {};
        logger_out.write(&mut data).await.unwrap();
    }
}
