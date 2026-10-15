//! Controller reading for rebinding on the Controls page (XInput, the only backend the game reads).
//!
//! `read_first_connected` polls the four XInput slots. `captured_source` compares a sample with the
//! baseline taken when capture started and names the first control that was newly pressed, so a
//! button already held when capture starts does not bind by accident. The rules are pure and tested
//! without a controller.

/// Trigger value (0..255) that counts as a press for capture: half travel.
pub const TRIGGER_PRESS: u8 = 128;
/// Stick deflection (raw, -32768..32767) that counts as a press for capture: about 60 % of travel.
pub const STICK_PRESS: i32 = 20_000;

/// The XInput button bits used by the controls file (XInput names in `gt4controls`).
const BUTTON_BITS: &[(u16, &str)] = &[
    (0x0001, "DpadUp"),
    (0x0002, "DpadDown"),
    (0x0004, "DpadLeft"),
    (0x0008, "DpadRight"),
    (0x0010, "Start"),
    (0x0020, "Back"),
    (0x0040, "LThumb"),
    (0x0080, "RThumb"),
    (0x0100, "LB"),
    (0x0200, "RB"),
    (0x1000, "A"),
    (0x2000, "B"),
    (0x4000, "X"),
    (0x8000, "Y"),
];

/// One reading of a controller.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PadSample {
    pub buttons: u16,
    pub left_trigger: u8,
    pub right_trigger: u8,
    pub left_x: i16,
    pub left_y: i16,
    pub right_x: i16,
    pub right_y: i16,
}

/// What the captured control must be: a button-type input (buttons, triggers) or a stick axis.
/// The Controls page only lists button-type inputs, so `StickAxis` is exercised by tests for now.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[cfg_attr(not(test), allow(dead_code))]
pub enum Target {
    Button,
    StickAxis,
}

/// The XInput source for the first control newly pressed in `now` compared with `baseline`.
/// Buttons are checked before triggers; for stick targets only the axes are checked.
pub fn captured_source(baseline: &PadSample, now: &PadSample, target: Target) -> Option<String> {
    match target {
        Target::Button => {
            for (bit, name) in BUTTON_BITS {
                if now.buttons & bit != 0 && baseline.buttons & bit == 0 {
                    return Some(format!("xinput:{name}"));
                }
            }
            if now.left_trigger >= TRIGGER_PRESS && baseline.left_trigger < TRIGGER_PRESS {
                return Some("xinput:LT".to_string());
            }
            if now.right_trigger >= TRIGGER_PRESS && baseline.right_trigger < TRIGGER_PRESS {
                return Some("xinput:RT".to_string());
            }
            None
        }
        Target::StickAxis => {
            let axes = [
                (i32::from(now.left_x) - i32::from(baseline.left_x), "LX"),
                (i32::from(now.left_y) - i32::from(baseline.left_y), "LY"),
                (i32::from(now.right_x) - i32::from(baseline.right_x), "RX"),
                (i32::from(now.right_y) - i32::from(baseline.right_y), "RY"),
            ];
            axes.iter()
                .find(|(delta, _)| delta.abs() >= STICK_PRESS)
                .map(|(_, name)| format!("xinput:{name}"))
        }
    }
}

/// Reads the first connected controller, or `None` when no pad is connected.
#[cfg(windows)]
pub fn read_first_connected() -> Option<PadSample> {
    use windows::Win32::UI::Input::XboxController::{XInputGetState, XINPUT_STATE};
    for index in 0..4u32 {
        let mut state = XINPUT_STATE::default();
        // SAFETY: `state` is a valid, writable XINPUT_STATE for the duration of the call.
        let result = unsafe { XInputGetState(index, &mut state) };
        if result == 0 {
            let pad = state.Gamepad;
            return Some(PadSample {
                buttons: pad.wButtons.0,
                left_trigger: pad.bLeftTrigger,
                right_trigger: pad.bRightTrigger,
                left_x: pad.sThumbLX,
                left_y: pad.sThumbLY,
                right_x: pad.sThumbRX,
                right_y: pad.sThumbRY,
            });
        }
    }
    None
}

#[cfg(not(windows))]
pub fn read_first_connected() -> Option<PadSample> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pad() -> PadSample {
        PadSample::default()
    }

    #[test]
    fn a_newly_pressed_face_button_is_captured_by_name() {
        let baseline = pad();
        let mut now = pad();
        now.buttons = 0x1000; // A
        assert_eq!(captured_source(&baseline, &now, Target::Button), Some("xinput:A".to_string()));
        now.buttons = 0x8000; // Y
        assert_eq!(captured_source(&baseline, &now, Target::Button), Some("xinput:Y".to_string()));
    }

    #[test]
    fn a_button_held_before_capture_does_not_bind() {
        let mut baseline = pad();
        baseline.buttons = 0x2000; // B already held
        let now = baseline;
        assert_eq!(captured_source(&baseline, &now, Target::Button), None);
    }

    #[test]
    fn triggers_bind_at_half_travel_and_not_before() {
        let baseline = pad();
        let mut now = pad();
        now.left_trigger = TRIGGER_PRESS - 1;
        assert_eq!(captured_source(&baseline, &now, Target::Button), None);
        now.left_trigger = TRIGGER_PRESS;
        assert_eq!(captured_source(&baseline, &now, Target::Button), Some("xinput:LT".to_string()));
        let mut right = pad();
        right.right_trigger = 255;
        assert_eq!(captured_source(&baseline, &right, Target::Button), Some("xinput:RT".to_string()));
    }

    #[test]
    fn sticks_bind_only_for_stick_targets() {
        let baseline = pad();
        let mut now = pad();
        now.right_y = -30_000;
        assert_eq!(captured_source(&baseline, &now, Target::StickAxis), Some("xinput:RY".to_string()));
        assert_eq!(captured_source(&baseline, &now, Target::Button), None);
        let mut small = pad();
        small.left_x = 5_000;
        assert_eq!(captured_source(&baseline, &small, Target::StickAxis), None, "inside the capture threshold");
    }
}
