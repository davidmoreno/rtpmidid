use std::io::{Error, ErrorKind, Result};
use std::{cell::Cell, io, sync::Arc};

use async_trait::async_trait;
use tokio::{
    net::UdpSocket,
    sync::mpsc::{self, Receiver, Sender},
};

use crate::{
    midiinout::MidiInOut,
    midistream::MidiStream,
    rtppeer::{self, Event, Response, RtpPeer},
};

pub enum MidiMessage {
    MidiData(MidiStream),
    Close,
}

// This is a rtpmidi client. ie. it connects to a server, its the initiator role
pub struct RtpMidiServer {
    writer_tx: Sender<MidiMessage>,
    reader_rx: Receiver<MidiMessage>,
}
pub struct RtpMidiServerImpl {
    writer_rx: Receiver<MidiMessage>,
    reader_tx: Sender<MidiMessage>,
}

impl RtpMidiServer {
    pub async fn new(name: &str, address: &str, port: u16) -> io::Result<RtpMidiServer> {
        let (writer_tx, writer_rx) = mpsc::channel(100);
        let (reader_tx, reader_rx) = mpsc::channel(100);

        let public = RtpMidiServer {
            writer_tx,
            reader_rx,
        };
        let mut private = RtpMidiServerImpl {
            writer_rx,
            reader_tx,
        };
        let name_cp: String = name.into();
        let address_cp: String = address.into();
        tokio::spawn(async move {
            if let Err(x) = private.listen_loop(name_cp, address_cp, port).await {
                error!("Error when listening: {}", x);
            };
        });

        Ok(public)
    }
}

#[async_trait]
impl MidiInOut for RtpMidiServer {
    async fn write(&mut self, data: &mut MidiStream) -> Result<()> {
        match self.writer_tx.send(MidiMessage::Close).await {
            Ok(()) => Ok(()),
            Err(_) => Err(Error::from(ErrorKind::UnexpectedEof)),
        }
    }
    async fn read(&mut self, data: &mut MidiStream) -> Result<()> {
        match self
            .reader_rx
            .recv()
            .await
            .ok_or(Error::from(ErrorKind::UnexpectedEof))?
        {
            MidiMessage::MidiData(msg) => {
                msg.copy_to(data);
            }
            MidiMessage::Close => {
                panic!("Not implemented");
            }
        }
        Ok(())
    }
}

impl RtpMidiServerImpl {
    async fn listen_loop(&mut self, name: String, address: String, port: u16) -> io::Result<()> {
        let mut peer = rtppeer::RtpPeer::new(&name);
        let control_socket = UdpSocket::bind(format!("{}:{}", address, port)).await?;
        let midi_socket = UdpSocket::bind(format!("{}:{}", address, port + 1)).await?;

        let mut control_data = vec![0u8; 1500];
        let mut midi_data = vec![0u8; 1500];
        loop {
            let control_socket_recv = control_socket.recv_from(&mut control_data);
            let midi_socket_recv = midi_socket.recv_from(&mut midi_data);
            let mut event = Event::DoNothing;

            let addr = tokio::select! {
                recv = control_socket_recv => {
                    let (length, addr) = recv?;
                    event = Event::NetworkControlData(&control_data[..length]);
                    // control_socket_recv = control_socket.recv_from(&mut control_data);
                    addr
                },
                recv = midi_socket_recv => {
                    let (length, addr) = recv?;
                    event = Event::NetworkMidiData(&midi_data[..length]);
                    // midi_socket_recv = midi_socket.recv_from(&mut midi_data);
                    addr
                }
            };

            match peer.event(&event) {
                Response::NetworkControlData(data) => {
                    debug!("Send to control: {:02X?}", data);
                    control_socket.send_to(data, addr).await?;
                }
                Response::NetworkMidiData(data) => {
                    debug!("Send to midi: {:02X?}", data);
                    midi_socket.send_to(data, addr).await?;
                }
                Response::MidiData(data) => {
                    info!("MIDI DATA: {:02X?}", data);
                }
                Response::Disconnect(why) => {
                    error!(
                        "Disconnect: {:?}. Ignoring at this phase of development.",
                        why
                    );
                }
                response => {
                    println!("WIP Response: {:?}", response);
                }
            };
        }
    }
}
