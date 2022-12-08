/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
mod rtppeer;

use clap::Parser;
use std::io;
use tokio::net::UdpSocket;
use tokio::select;

use crate::rtppeer::RtpPeerEvent;
use crate::rtppeer::RtpPeerEventResponse;

#[derive(Parser, Debug)]
// #[command(author, version, about, long_about = None)]
struct Arguments {
    #[clap(short, long, default_value = "0.0.0.0")]
    address: String,
    #[clap(short, long, default_value = "5004")]
    port: u16,
}

#[tokio::main]
async fn main() -> io::Result<()> {
    let args = Arguments::parse();
    println!(
        "Rtpmidi client monitor. Listening at {}:{}",
        args.address, args.port
    );

    listen_rtpmidi(&args.address, args.port).await?;
    Ok(())
}

async fn listen_rtpmidi(address: &str, port: u16) -> io::Result<()> {
    let control_socket = UdpSocket::bind(format!("{}:{}", address, port)).await?;
    let midi_socket = UdpSocket::bind(format!("{}:{}", address, port + 1)).await?;

    let mut rtppeer = rtppeer::RtpPeer::new();
    let mut control_data = vec![0u8; 1500];
    let mut midi_data = vec![0u8; 1500];

    loop {
        let control_socket_recv = control_socket.recv_from(&mut control_data);
        let midi_socket_recv = midi_socket.recv_from(&mut midi_data);
        let mut event = RtpPeerEvent::DoNothing;

        let addr = tokio::select! {
            recv = control_socket_recv => {
                let (length, addr) = recv?;
                event = RtpPeerEvent::ControlData(&control_data[..length]);
                // control_socket_recv = control_socket.recv_from(&mut control_data);
                addr
            },
            recv = midi_socket_recv => {
                let (length, addr) = recv?;
                event = RtpPeerEvent::MidiData(&midi_data[..length]);
                // midi_socket_recv = midi_socket.recv_from(&mut midi_data);
                addr
            }
        };

        match rtppeer.event(&event) {
            RtpPeerEventResponse::ControlData(data) => {
                control_socket.send_to(data, addr).await?;
            }
            RtpPeerEventResponse::MidiData(data) => {
                midi_socket.send_to(data, addr).await?;
            }
            _ => {
                println!("WIP");
            }
        };
    }

    // Ok(())
}
