use std::{
    io::{self, Cursor, Write},
    time::Duration,
};

#[derive(Debug, PartialEq)]
enum RtpPeerStatus {
    Initial,
    ControlConnected,
    MidiConnected,
    Connected,
    WaitingCk,
    Disconnected,
}

#[derive(Debug, Copy, Clone)]
pub enum RtpPeerEventBasic {
    SendCk,
}

#[derive(Debug, Copy, Clone)]
pub enum RtpPeerEvent<'a> {
    DoNothing,
    ControlData(&'a [u8]),
    MidiData(&'a [u8]),
    SendCk,
}

#[derive(Debug, Copy, Clone)]
pub enum RtpPeerEventResponse<'a> {
    DoNothing,
    MidiData(&'a [u8]),
    ControlData(&'a [u8]),
    ScheduleTimeout(Duration, RtpPeerEventBasic),
    Disconnect,
}

#[derive(Debug)]
pub(crate) struct RtpPeer {
    status: RtpPeerStatus,
    initiator_id: u16,
    sequence_nr: u32,
    sequence_ack: u32,
    remote_sequence_nr: u32,
    timestamp_start: u64,
    latency: u64,
    // Part of the struct, to prevent mallocs at return.
    // No mem management needed for this type.
    buffer: [u8; 1500],
}

impl RtpPeer {
    pub fn new() -> RtpPeer {
        RtpPeer {
            status: RtpPeerStatus::Initial,
            initiator_id: 0,
            sequence_nr: 0,
            sequence_ack: 0,
            remote_sequence_nr: 0,
            timestamp_start: 0,
            latency: 0,
            buffer: [0; 1500],
        }
    }

    pub fn event(&mut self, event: &RtpPeerEvent) -> RtpPeerEventResponse {
        println!("Got event: {:?}", event);
        match event {
            RtpPeerEvent::MidiData(data) => {
                return self.parse_packet(data);
            }
            _ => {
                println!("Rest")
            }
        }
        RtpPeerEventResponse::DoNothing
    }

    fn parse_packet(&mut self, data: &[u8]) -> RtpPeerEventResponse {
        println!("GOT Data {:?}", data);

        let mut cursor = Cursor::new(&mut self.buffer[..]);
        // Write::write(&mut cursor, b"test");
        cursor.write(&[0xFF, 0xFF]).unwrap();
        cursor.write(b"IN").unwrap();
        cursor.write(&[0xFA, 0x57]).unwrap();
        cursor.write(&[0xBE, 0xEF]).unwrap();
        cursor.write(b"response").unwrap();
        cursor.write(&[0x00]).unwrap();

        let length = cursor.position() as usize;
        return RtpPeerEventResponse::MidiData(&self.buffer[..length]);
    }
}

#[cfg(test)]
mod tests {
    use crate::rtppeer::{RtpPeerEventResponse, RtpPeerStatus};

    use super::{RtpPeer, RtpPeerEvent};
    #[test]
    fn test_rtppeer_new() {
        let mut rtppeer = RtpPeer::new();

        assert!(rtppeer.status == RtpPeerStatus::Initial);
        let ret = rtppeer.event(&RtpPeerEvent::MidiData(&[
            // rtpmidi connect message
            0xFF, 0xFF, b'I', b'N', // command in
            0xFA, 0x57, 0xBE, 0xEF, // Version, and Id
            b't', b'e', b's', b't', b'i', b'n', b'g', 0x00, // The name
        ]));
        println!("{:?}", ret);
        let sdata = match ret {
            RtpPeerEventResponse::MidiData(sdata) => sdata,
            _ => panic!("Bad type"),
        };
        assert_eq!(sdata.len(), 3);

        let ret = rtppeer.event(&RtpPeerEvent::SendCk);

        println!("{:?}", ret);
    }
}
