// SPDX-License-Identifier: GPL-3.0-only
//! core-api/fm1core.h (ABI 6) in Rust: the panel and the descriptor, field for field (`repr(C)`), with the
//! layout checked against the C header's (sizes and offsets computed from it by hand, asserted at compile time).

use std::ffi::c_char;

pub const LCD_W: usize = 240;
pub const LCD_H: usize = 240;
pub const NKEY: usize = 27;
pub const NBTN: usize = 14;
pub const NENC: usize = 7;
pub const NCOL: usize = 11;
pub const NLINE: usize = 4;
pub const NB: usize = 14;
pub const NE: usize = 8;

pub const ABI: u32 = 6;
pub const FLASH_STAGED: &[u8] = b"fm1core:staged";


#[repr(C)]
pub struct Panel {
    pub keys: u32,
    pub buttons: u32,
    pub keys_tap: u32,
    pub buttons_tap: u32,
    pub enc: [i32; NENC],
    pub master: i32,
    pub key_vel: [u8; NKEY],
    pub key_lean: [u8; NKEY],
    pub led: [[u8; NLINE]; NCOL],
    pub led_play_green: u8,
    pub lcd: [u16; LCD_W * LCD_H],
    pub lcd_writes: u32,
    pub btn_id: [u8; NB],
    pub enc_id: [u8; NE - 1],
    pub enc_dir: [i8; NE - 1],
    pub ready: u8,
}

const _: () = assert!(std::mem::size_of::<Panel>() == 115_384);
const _: () = assert!(std::mem::offset_of!(Panel, master) == 44);
const _: () = assert!(std::mem::offset_of!(Panel, led) == 102);
const _: () = assert!(std::mem::offset_of!(Panel, lcd) == 148);
const _: () = assert!(std::mem::offset_of!(Panel, lcd_writes) == 115_348);
const _: () = assert!(std::mem::offset_of!(Panel, ready) == 115_380);

/// fm1param_t: never instantiated here (params = NULL), only pointed to
#[repr(C)]
pub struct Param {
    _opaque: [u8; 0],
}

pub type OptionsFn = extern "C" fn(*const c_char, i32, i32, i32);
pub type BootOptionsFn = extern "C" fn(i32, *const c_char, i32) -> i32;

#[repr(C)]
pub struct Core {
    pub abi_version: u32,
    pub id: *const c_char,
    pub name: *const c_char,
    pub version: *const c_char,
    pub source_url: *const c_char,
    pub family: *const c_char,
    pub license: *const c_char,
    pub gated: *const c_char,
    pub flash_size: u32,
    pub button_names: [*const c_char; NB],
    pub params: *const Param,
    pub nparams: u32,
    pub panel: *mut Panel,
    pub keymap: *const i8,
    pub options: Option<OptionsFn>,
    pub boot_options: Option<BootOptionsFn>,
    pub init: Option<extern "C" fn(i32)>,
    pub tick: Option<extern "C" fn(u32)>,
    pub frame: Option<extern "C" fn()>,
    pub idle: Option<extern "C" fn()>,
    pub audio: Option<extern "C" fn(*mut i16, u32)>,
    pub midi_in: Option<extern "C" fn(u32) -> i32>,
    pub midi_out_take: Option<extern "C" fn(*mut u32) -> i32>,
    pub dump: Option<extern "C" fn()>,
    pub stats: Option<extern "C" fn(*mut u32, *mut u32)>,
    pub ui_info: Option<extern "C" fn(*mut c_char, u32)>,
    pub shutdown: Option<extern "C" fn()>,
    pub halted: Option<extern "C" fn() -> i32>,
    pub flash_stage: Option<extern "C" fn(*const u8, u32) -> i32>,
    pub flash: Option<extern "C" fn() -> *mut u8>,
    pub flash_dirty: Option<extern "C" fn() -> u32>,
    pub flash_sync: Option<extern "C" fn() -> i32>,
    pub param_epoch: Option<extern "C" fn() -> u32>,
    pub knob_target: Option<extern "C" fn(i32) -> i32>,
    pub credits: *const c_char,
}

const _: () = assert!(std::mem::offset_of!(Core, flash_size) == 64);
const _: () = assert!(std::mem::offset_of!(Core, button_names) == 72);
const _: () = assert!(std::mem::offset_of!(Core, panel) == 200);
const _: () = assert!(std::mem::offset_of!(Core, options) == 216);
const _: () = assert!(std::mem::offset_of!(Core, flash_sync) == 352);
const _: () = assert!(std::mem::offset_of!(Core, credits) == 376);
const _: () = assert!(std::mem::size_of::<Core>() == 384);

// the descriptor is built once and never written again; its pointers are to statics of this module
unsafe impl Sync for Core {}

/// The FM-1's wiring (hal/fm1_input.h FM1_KEYMAP, core-glue/common/panel.h FM1P_KEYMAP_INIT): key id (0..13 matrix
/// button, 14 + n note key n) at (row, column). fm1-soc's periph.rs KEYMAP is the same table (checked in a test).
pub static KEYMAP: [[i8; NCOL]; 6] = [
    [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1],
    [5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1],
    [34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12],
    [23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33],
    [0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22],
    [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1],
];

/// Felucca's PANEL_DEFAULT (panel.c), the FM-1's factory panel (core-glue/common/panel.h fm1p_panel_default):
/// label (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+) -> matrix button, role (SELECT ALGO
/// PRESETS K1..K4) -> matrix encoder, all clockwise +
pub const BTN_ID: [u8; NB] = [2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1];
pub const ENC_ID: [u8; NE - 1] = [0, 1, 6, 2, 3, 4, 5];
