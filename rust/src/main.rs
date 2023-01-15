mod filemidi;
mod midiinout;
mod midistream;

#[macro_use]
extern crate log;

use env_logger::fmt::Color;
use log::Level;
use log::LevelFilter;
use std::io::Write;

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

fn main() {}
