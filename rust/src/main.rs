mod filemidi;
mod midiinout;
mod midistream;
mod rtpmidiclient;
mod rtpmidiserver;
mod rtppeer;

#[macro_use]
extern crate log;

use env_logger::fmt::Color;
use log::Level;
use log::LevelFilter;
use std::io;
use std::io::Write;

use crate::rtpmidiserver::RtpMidiServer;

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
    match logger_builder.try_init() {
        Ok(()) => (),
        Err(msg) => {
            warn!("Error setting up logging: {}", msg)
        }
    }
}

#[tokio::main]
async fn main() -> io::Result<()> {
    let mut server = RtpMidiServer::new("rtpmidid", "0.0.0.0", 5014).await?;

    info!("Done?");

    Ok(())
}
