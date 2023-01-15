use crate::rtppeer::RtpPeer;

// This is a rtpmidi client. ie. it connects to a server, its the initiator role
pub struct RtpMidiClient {
    peer: RtpPeer,
}
