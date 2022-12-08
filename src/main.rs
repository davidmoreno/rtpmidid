mod rtppeer;

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
use std::io;
use std::str;
use tokio::net::UdpSocket;

use crate::rtppeer::RtpPeerEvent;
use crate::rtppeer::RtpPeerEventResponse;

#[tokio::main]
async fn main() -> io::Result<()> {
    println!("Hello, world!");

    let socket = UdpSocket::bind("0.0.0.0:6789").await?;

    let mut rtppeer = rtppeer::RtpPeer::new();
    let mut buf = vec![0u8; 1500];
    loop {
        let (length, addr) = socket.recv_from(&mut buf).await?;
        let event = RtpPeerEvent::MidiData(&buf[0..length as usize]);
        match rtppeer.event(&event) {
            RtpPeerEventResponse::MidiData(data) => {
                socket.send_to(data, addr).await?;
            }
            _ => {
                println!("WIP");
            }
        }
    }

    // Ok(())
}
