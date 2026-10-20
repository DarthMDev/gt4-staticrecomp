//! Discrete navigation from the pad, pure and tested without a controller. nav.js acts on the names this emits:
//! `a b x y lb rb start` on a press, and `up down left right` on a press with repeat (the D-pad or the left stick,
//! past half deflection). A direction fires at once, again after 420 ms held, then every 110 ms while held.

use std::collections::BTreeSet;
use std::time::{Duration, Instant};

use gt4l_platform::pad::{PadSample, PRESS};

/// Left-stick deflection that counts as a direction (fraction of full travel), as nav.js did.
pub const STICK_DEADZONE: f32 = 0.5;
pub const REPEAT_FIRST: Duration = Duration::from_millis(420);
pub const REPEAT_EVERY: Duration = Duration::from_millis(110);

/// Face and shoulder buttons: the controls-file source and the name nav.js routes.
const BUTTONS: &[(&str, &str)] = &[
    ("xinput:A", "a"),
    ("xinput:B", "b"),
    ("xinput:X", "x"),
    ("xinput:Y", "y"),
    ("xinput:LB", "lb"),
    ("xinput:RB", "rb"),
    ("xinput:Start", "start"),
];

/// D-pad sources and the names they navigate with, in the order a held direction is chosen.
const DPAD: &[(&str, &str)] = &[
    ("xinput:DpadUp", "up"),
    ("xinput:DpadDown", "down"),
    ("xinput:DpadLeft", "left"),
    ("xinput:DpadRight", "right"),
];

#[derive(Default)]
pub struct Navigator {
    previous_buttons: BTreeSet<&'static str>,
    held: Option<&'static str>,
    held_since: Option<Instant>,
    last_fire: Option<Instant>,
}

impl Navigator {
    /// The names to send for one sample taken at `now`, in order.
    pub fn update(&mut self, sample: &PadSample, now: Instant) -> Vec<&'static str> {
        let mut fired = Vec::new();
        let buttons_now: BTreeSet<&'static str> = BUTTONS
            .iter()
            .filter(|(source, _)| is_pressed(sample, source))
            .map(|(_, name)| *name)
            .collect();
        for name in &buttons_now {
            if !self.previous_buttons.contains(name) {
                fired.push(*name);
            }
        }
        self.previous_buttons = buttons_now;

        let direction = direction_of(sample);
        if direction != self.held {
            self.held = direction;
            self.held_since = Some(now);
            self.last_fire = Some(now);
            if let Some(name) = direction {
                fired.push(name);
            }
        } else if let (Some(name), Some(since), Some(last)) = (self.held, self.held_since, self.last_fire) {
            if now.duration_since(since) >= REPEAT_FIRST && now.duration_since(last) >= REPEAT_EVERY {
                self.last_fire = Some(now);
                fired.push(name);
            }
        }
        fired
    }
}

fn is_pressed(sample: &PadSample, source: &str) -> bool {
    sample.pressure.get(source).copied().unwrap_or(0) >= PRESS
}

/// The direction held now: the D-pad first, then the left stick (the dominant axis past the deadzone).
fn direction_of(sample: &PadSample) -> Option<&'static str> {
    if let Some((_, name)) = DPAD.iter().find(|(source, _)| is_pressed(sample, source)) {
        return Some(name);
    }
    let x = (f32::from(sample.left_x) / 32_767.0).clamp(-1.0, 1.0);
    let y = (f32::from(sample.left_y) / 32_767.0).clamp(-1.0, 1.0);
    if x.abs() <= STICK_DEADZONE && y.abs() <= STICK_DEADZONE {
        return None;
    }
    if x.abs() >= y.abs() {
        Some(if x < 0.0 { "left" } else { "right" })
    } else {
        // SDL's Y is positive down, so a negative value is up.
        Some(if y < 0.0 { "up" } else { "down" })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use gt4l_platform::pad::{sample, RawPad};

    fn pad_with(edit: impl FnOnce(&mut RawPad)) -> PadSample {
        let mut raw = RawPad::default();
        edit(&mut raw);
        sample(&raw)
    }

    #[test]
    fn a_button_fires_once_per_press() {
        let start = Instant::now();
        let mut nav = Navigator::default();
        assert_eq!(nav.update(&pad_with(|p| p.south = true), start), vec!["a"]);
        assert!(nav.update(&pad_with(|p| p.south = true), start).is_empty(), "held, no repeat");
        assert!(nav.update(&pad_with(|_| {}), start).is_empty());
        assert_eq!(nav.update(&pad_with(|p| p.south = true), start), vec!["a"], "a new press fires again");
    }

    #[test]
    fn sdl_face_buttons_map_to_the_names_nav_js_routes() {
        let start = Instant::now();
        let mut nav = Navigator::default();
        let names = nav.update(&pad_with(|p| {
            p.south = true;
            p.east = true;
            p.west = true;
            p.north = true;
            p.left_shoulder = true;
            p.right_shoulder = true;
            p.start = true;
        }), start);
        let mut sorted = names.clone();
        sorted.sort_unstable();
        assert_eq!(sorted, vec!["a", "b", "lb", "rb", "start", "x", "y"]);
    }

    #[test]
    fn the_dpad_fires_at_once_then_repeats_after_420_ms_and_every_110_ms() {
        let start = Instant::now();
        let mut nav = Navigator::default();
        let right = pad_with(|p| p.dpad_right = true);
        assert_eq!(nav.update(&right, start), vec!["right"]);
        assert!(nav.update(&right, start + Duration::from_millis(300)).is_empty());
        assert_eq!(nav.update(&right, start + Duration::from_millis(420)), vec!["right"], "first repeat");
        assert!(nav.update(&right, start + Duration::from_millis(500)).is_empty(), "110 ms gap");
        assert_eq!(nav.update(&right, start + Duration::from_millis(530)), vec!["right"], "every 110 ms");
    }

    #[test]
    fn the_left_stick_is_a_direction_past_half_deflection_only() {
        let start = Instant::now();
        let mut nav = Navigator::default();
        assert!(nav.update(&pad_with(|p| p.left_x = 10_000), start).is_empty(), "inside the 0.5 deadzone");
        assert_eq!(nav.update(&pad_with(|p| p.left_x = -30_000), start), vec!["left"]);
        let mut up = Navigator::default();
        // SDL: y negative is up.
        assert_eq!(up.update(&pad_with(|p| p.left_y = -30_000), start), vec!["up"]);
        let mut down = Navigator::default();
        assert_eq!(down.update(&pad_with(|p| p.left_y = 30_000), start), vec!["down"]);
    }

    #[test]
    fn a_held_direction_stops_when_released() {
        let start = Instant::now();
        let mut nav = Navigator::default();
        assert_eq!(nav.update(&pad_with(|p| p.dpad_up = true), start), vec!["up"]);
        assert!(nav.update(&pad_with(|_| {}), start + Duration::from_millis(900)).is_empty());
        assert_eq!(nav.update(&pad_with(|p| p.dpad_up = true), start + Duration::from_millis(900)), vec!["up"], "a new hold starts again");
    }
}
