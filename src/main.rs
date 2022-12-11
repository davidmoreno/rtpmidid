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

#[macro_use]
extern crate log;

use clap::Parser;
use env_logger::fmt::Color;
use log::info;
use log::Level;
use log::LevelFilter;
use std::io;
use std::io::Write;
use tokio::net::UdpSocket;

use crate::rtppeer::Event;
use crate::rtppeer::Response;

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
    setup_logging();

    let args = Arguments::parse();
    info!(
        "Rtpmidi client monitor. Listening at {}:{}",
        args.address, args.port
    );

    listen_rtpmidi(&args.address, args.port).await?;
    Ok(())
}

pub fn setup_logging() {
    let mut logger_builder = env_logger::builder();
    logger_builder.format(|buf, record| {
        let mut style = buf.style();

        match record.level() {
            Level::Debug => style.set_color(Color::Blue),
            Level::Error => style.set_color(Color::Red),
            Level::Warn => style.set_color(Color::Yellow),
            _ => &mut style,
        };

        writeln!(
            buf,
            "{} [{}] [{}:{}] - {}",
            chrono::Local::now().format("%Y-%m-%dT%H:%M:%S"),
            style.value(record.level()),
            record.file().unwrap_or("unknown"),
            record.line().unwrap_or(0),
            record.args()
        )
    });
    if cfg!(debug_assertions) {
        logger_builder.filter_level(LevelFilter::Debug);
    } else {
        logger_builder.filter_level(LevelFilter::Info);
    }
    logger_builder.try_init().unwrap();
}

async fn listen_rtpmidi(address: &str, port: u16) -> io::Result<()> {
    let control_socket = UdpSocket::bind(format!("{}:{}", address, port)).await?;
    let midi_socket = UdpSocket::bind(format!("{}:{}", address, port + 1)).await?;

    let mut rtppeer = rtppeer::RtpPeer::new("Monitor".to_string());
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

        match rtppeer.event(&event) {
            Response::NetworkControlData(data) => {
                control_socket.send_to(data, addr).await?;
            }
            Response::NetworkMidiData(data) => {
                midi_socket.send_to(data, addr).await?;
            }
            _ => {
                println!("WIP");
            }
        };
    }

    // Ok(())
}
