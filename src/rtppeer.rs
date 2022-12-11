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
use byteorder::{BigEndian, WriteBytesExt};
use std::{
    convert::TryInto,
    io::{Cursor, Write},
    str,
    time::{Duration, Instant},
};

#[derive(Debug, PartialEq, Copy, Clone)]
enum Status {
    Initial,
    ControlConnected,
    Connected,
    WaitingCk,
    Disconnected,
}

#[derive(Debug, PartialEq)]
enum Channel {
    Midi,
    Control,
}

pub enum PacketType {
    Unknown,
    IN,
    NO,
    OK,
    CK,
}

#[derive(Debug, Copy, Clone)]
pub enum BasicEvent {
    SendCk,
}

#[derive(Debug, Copy, Clone)]
pub enum Event<'a> {
    DoNothing,
    ControlData(&'a [u8]),
    MidiData(&'a [u8]),
    SendCk,
}

#[derive(Debug, Copy, Clone)]
pub enum DisconnectReason {
    BadPacket,
    BadVersion,
    BadPeer,
    Requested,
}

#[derive(Debug, Copy, Clone)]
pub enum Response<'a> {
    DoNothing,
    MidiData(&'a [u8]),
    ControlData(&'a [u8]),
    ScheduleTimeout(Duration, BasicEvent),
    Disconnect(DisconnectReason),
}

#[derive(Debug)]
pub(crate) struct RtpPeer {
    status: Status,
    initiator_id: u32,
    local_ssid: u32,
    local_name: String,
    remote_ssid: u32,
    remote_name: String,

    sequence_nr: u32,
    sequence_ack: u32,
    remote_sequence_nr: u32,
    timestamp_start: Instant,
    latency: u64,

    // Part of the struct, to prevent mallocs at return.
    // No mem management needed for this type.
    buffer: [u8; 1500],
}

impl RtpPeer {
    pub fn new(name: String) -> RtpPeer {
        RtpPeer {
            status: Status::Initial,
            initiator_id: 0,
            local_ssid: rand::random::<u32>(),
            remote_ssid: 0,
            local_name: name,
            remote_name: String::from(""),
            sequence_nr: 0,
            sequence_ack: 0,
            remote_sequence_nr: 0,
            timestamp_start: Instant::now(),
            latency: 0,
            buffer: [0; 1500],
        }
    }

    pub fn event(&mut self, event: &Event) -> Response {
        debug!("GOT Event {:?}", event);
        match event {
            Event::ControlData(data) => {
                return self.parse_packet(Channel::Control, data);
            }
            Event::MidiData(data) => {
                return self.parse_packet(Channel::Midi, data);
            }
            _ => {
                println!("Rest")
            }
        }
        Response::DoNothing
    }

    fn parse_packet(&mut self, channel: Channel, data: &[u8]) -> Response {
        debug!("GOT Data {:?}", data);

        if data.len() < 16 {
            return Response::Disconnect(DisconnectReason::BadPacket);
        }
        let packet_type = Self::get_packet_type(&data[0..4].try_into().unwrap());
        match packet_type {
            PacketType::IN => {
                return self.parse_command_in(channel, data);
            }
            PacketType::OK => {}
            PacketType::CK => return self.parse_command_ck(channel, data),
            PacketType::NO => {}
            PacketType::Unknown => {
                return Response::Disconnect(DisconnectReason::BadPacket);
            }
        }
        return Response::Disconnect(DisconnectReason::BadPacket);
    }

    fn get_packet_type(data: &[u8; 4]) -> PacketType {
        if data[0] == 0xFF && data[1] == 0xFF && data[2] == b'I' && data[3] == b'N' {
            return PacketType::IN;
        }
        if data[0] == 0xFF && data[1] == 0xFF && data[2] == b'O' && data[3] == b'K' {
            return PacketType::OK;
        }
        if data[0] == 0xFF && data[1] == 0xFF && data[2] == b'N' && data[3] == b'O' {
            return PacketType::NO;
        }
        if data[0] == 0xFF && data[1] == 0xFF && data[2] == b'C' && data[3] == b'K' {
            return PacketType::CK;
        }
        PacketType::Unknown
    }

    fn get_current_timestamp(&self) -> u64 {
        let now = Instant::now();
        now.elapsed().as_micros().try_into().unwrap()
    }

    fn parse_command_in(&mut self, channel: Channel, data: &[u8]) -> Response {
        let fixedpart: [u8; 16] = data[0..16].try_into().unwrap();
        let version: u32 = (fixedpart[4] as u32) << 24
            | (fixedpart[5] as u32) << 16
            | (fixedpart[6] as u32) << 8
            | (fixedpart[7] as u32);
        let initiator_id: u32 = (fixedpart[8] as u32) << 24
            | (fixedpart[9] as u32) << 16
            | (fixedpart[10] as u32) << 8
            | (fixedpart[11] as u32);
        let ssid: u32 = (fixedpart[12] as u32) << 24
            | (fixedpart[13] as u32) << 16
            | (fixedpart[14] as u32) << 8
            | (fixedpart[15] as u32);

        if version != 0x02 {
            error!("Invalid protocol version {} (must be 2)", version);
            return Response::Disconnect(DisconnectReason::BadVersion);
        }

        // println!(
        //     "{:?} {:X} {:X} {:X} {:X}",
        //     fixedpart, protocol, version, initiator_id, ssid
        // );

        let name = match str::from_utf8(&data[16..]) {
            Ok(name) => name,
            Err(_) => return Response::Disconnect(DisconnectReason::BadPacket),
        };
        // println!("Name is <{}>", name);

        match (self.status, channel) {
            (Status::Initial, Channel::Control) => {
                self.initiator_id = initiator_id;
                self.remote_ssid = ssid;
                self.remote_name = String::from(name);
                self.status = Status::ControlConnected;

                debug!(
                    "Connect request: initiator_id: {}, remote_ssid: {}, remote_name: {}",
                    initiator_id, ssid, name
                );
                let len = {
                    let mut cursor = Cursor::new(&mut self.buffer[..]);
                    cursor.write(&[0xFF, 0xFF]).unwrap();
                    cursor.write(b"OK").unwrap();
                    cursor.write_u32::<BigEndian>(2).unwrap();
                    cursor.write_u32::<BigEndian>(self.initiator_id).unwrap();
                    cursor.write_u32::<BigEndian>(self.local_ssid).unwrap();
                    cursor.write(self.local_name.as_bytes()).unwrap();
                    cursor.write_u8(0).unwrap();
                    cursor.position() as usize
                };

                Response::ControlData(&self.buffer[0..len])
            }
            (Status::ControlConnected, Channel::Midi) => {
                if self.initiator_id != initiator_id || self.remote_ssid != ssid {
                    warn!("Message for the worng peer, not me.");
                    return Response::Disconnect(DisconnectReason::BadPeer);
                }

                let len = {
                    let mut cursor = Cursor::new(&mut self.buffer[..]);
                    cursor.write(&[0xFF, 0xFF]).unwrap();
                    cursor.write(b"OK").unwrap();
                    cursor.write_u32::<BigEndian>(2).unwrap();
                    cursor.write_u32::<BigEndian>(self.initiator_id).unwrap();
                    cursor.write_u32::<BigEndian>(self.local_ssid).unwrap();
                    cursor.write(self.local_name.as_bytes()).unwrap();
                    cursor.write_u8(0).unwrap();
                    cursor.position() as usize
                };
                self.status = Status::Connected;

                Response::MidiData(&self.buffer[0..len])
            }
            (_, channel) => {
                error!(
                    "Bad status, channel combo: {:?}, {:?}",
                    self.status, channel
                );
                Response::Disconnect(DisconnectReason::BadPacket)
            }
        }
    }
    fn parse_command_ck(&mut self, channel: Channel, data: &[u8]) -> Response {
        if data.len() < 36 {
            error!("Packet too small");
            return Response::Disconnect(DisconnectReason::BadPacket);
        }
        if channel != Channel::Midi {
            error!("CK packets should be on Midi channel only");
            return Response::Disconnect(DisconnectReason::BadPacket);
        }
        let fixedpart: [u8; 36] = data[0..36].try_into().unwrap();

        match fixedpart[8] {
            // receive first packet, just copy timestamp, add mine, and send
            1 => {
                let len = {
                    let timestamp: u64 = self.get_current_timestamp();
                    let mut cursor = Cursor::new(&mut self.buffer[..]);
                    cursor.write_u8(0xFF).unwrap();
                    cursor.write_u8(0xFF).unwrap();
                    cursor.write(b"CK").unwrap();
                    cursor.write_u32::<BigEndian>(self.local_ssid).unwrap();
                    cursor.write_u8(2).unwrap();
                    cursor.write_u8(0).unwrap();
                    cursor.write_u8(0).unwrap();
                    cursor.write_u8(0).unwrap();
                    cursor.write(&fixedpart[12..20]).unwrap();
                    cursor.write_u64::<BigEndian>(timestamp).unwrap();
                    cursor.write_u64::<BigEndian>(0).unwrap();
                    cursor.position() as usize
                };
                return Response::MidiData(&self.buffer[0..len]);
            }
            // I dont send yet, so not implemented
            2 => {
                panic!("WIP");
            }
            // response, I can use current time - 2nd time, to know latency
            3 => {
                // Get what I wrote there
                let t1 = u64::from_be_bytes(fixedpart[20..28].try_into().unwrap());
                let t2: u64 = self.get_current_timestamp();

                self.latency = t2 - t1;
                debug!("Latency is {} microseconds", self.latency);

                return Response::DoNothing;
            }
            _ => {
                error!("Invalid ck count");
                return Response::Disconnect(DisconnectReason::BadPacket);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Event, RtpPeer};
    use crate::{
        rtppeer::{Response, Status},
        setup_logging,
    };

    #[test]
    fn test_rtppeer_new() {
        setup_logging();
        let mut rtppeer = RtpPeer::new("test".to_string());

        assert!(rtppeer.status == Status::Initial);
        let ret = rtppeer.event(&Event::ControlData(&[
            // rtpmidi connect message
            0xFF, 0xFF, b'I', b'N', // command in
            0x00, 0x00, 0x00, 0x02, // Version,
            0x12, 0x34, 0x56, 0x78, // Initiator
            0xAA, 0xBB, 0xCC, 0xDD, // SSID
            b't', b'e', b's', b't', b'i', b'n', b'g', 0x00, // The name
        ]));
        println!("{:?}", ret);
        let sdata = match ret {
            Response::ControlData(sdata) => sdata,
            _ => panic!("Bad type"),
        };
        assert_eq!(sdata.len(), 21);
        assert!(rtppeer.status == Status::ControlConnected);

        let ret = rtppeer.event(&Event::MidiData(&[
            // rtpmidi connect message
            0xFF, 0xFF, b'I', b'N', // command in
            0x00, 0x00, 0x00, 0x02, // Version,
            0x12, 0x34, 0x56, 0x78, // Initiator
            0xAA, 0xBB, 0xCC, 0xDD, // SSID
            b't', b'e', b's', b't', b'i', b'n', b'g', 0x00, // The name
        ]));
        println!("{:?}", ret);
        let sdata = match ret {
            Response::ControlData(sdata) => sdata,
            _ => panic!("Bad type"),
        };
        assert_eq!(sdata.len(), 21);
        assert!(rtppeer.status == Status::Connected);
    }
}
