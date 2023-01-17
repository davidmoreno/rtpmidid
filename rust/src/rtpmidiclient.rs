use crate::rtppeer::RtpPeer;

// This is a rtpmidi client. ie. it connects to a server, its the initiator role
pub struct RtpMidiClient {
    peer: RtpPeer,
}

impl RtpMidiClient {
    pub async fn new(name: &str, remote_host: &str, remote_port: &str) -> RtpMidiClient {
        let peer = RtpPeer::new(name);

        RtpMidiClient { peer }
    }
}
