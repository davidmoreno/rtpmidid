use std::io::Error;
use std::io::ErrorKind;
use std::io::Result;
use std::vec;

const BUFFER_SIZE: usize = 1500;

pub struct MidiStream {
    data: Vec<u8>,
    read_cursor: usize,
    write_cursor: usize,
}

impl MidiStream {
    pub fn new() -> MidiStream {
        MidiStream {
            data: vec![0; BUFFER_SIZE],
            read_cursor: 0,
            write_cursor: 0,
        }
    }

    pub fn write(&mut self, data: &[u8]) -> Result<()> {
        if data.len() > BUFFER_SIZE - self.write_cursor {
            return Err(Error::from(ErrorKind::UnexpectedEof));
        }
        self.data[self.write_cursor..(self.write_cursor + data.len())].copy_from_slice(data);
        self.write_cursor += data.len();
        Ok(())
    }

    pub fn read(&mut self, data: &mut [u8]) -> Result<usize> {
        let mut length = self.write_cursor - self.read_cursor;

        if data.len() < length {
            length = data.len()
        }

        data[..length].copy_from_slice(&self.data[self.read_cursor..self.read_cursor + length]);
        self.read_cursor += length;

        Ok(length)
    }

    pub fn read_slice(&self) -> &[u8] {
        return &self.data[0..self.write_cursor];
    }
    pub fn write_slice(&mut self) -> &mut [u8] {
        return &mut self.data[self.write_cursor..];
    }
    pub fn advance_read(&mut self, length: usize) -> Result<()> {
        if self.write_cursor + length > BUFFER_SIZE {
            return Err(Error::from(ErrorKind::UnexpectedEof));
        }

        self.write_cursor += length;
        Ok(())
    }

    fn clear(&mut self) {
        self.read_cursor = 0;
        self.write_cursor = 0;
    }
}
