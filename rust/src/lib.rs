#![no_std]

use core::panic::PanicInfo;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

const HISTORY_SLOTS: usize = 16;
const SLOT_LEN: usize = 64;

struct History {
    entries: [[u8; SLOT_LEN]; HISTORY_SLOTS],
    lens: [usize; HISTORY_SLOTS],
    head: usize,
    count: usize,
}

impl History {
    const fn new() -> Self {
        Self {
            entries: [[0; SLOT_LEN]; HISTORY_SLOTS],
            lens: [0; HISTORY_SLOTS],
            head: 0,
            count: 0,
        }
    }

    fn push(&mut self, src: &[u8]) {
        let slot = self.head;
        let len = src.len().min(SLOT_LEN.saturating_sub(1));

        let dest = &mut self.entries[slot];
        for b in dest.iter_mut() {
            *b = 0;
        }
        dest[..len].copy_from_slice(&src[..len]);

        self.lens[slot] = len;
        self.head = (self.head + 1) % HISTORY_SLOTS;
        if self.count < HISTORY_SLOTS {
            self.count += 1;
        }
    }
}

static LOCK: AtomicBool = AtomicBool::new(false);
static HISTORY_COUNT: AtomicU64 = AtomicU64::new(0);
static mut HISTORY: History = History::new();
static BANNER: &[u8] = b"Rust subsystem online (no_std)\0";

fn lock() {
    while LOCK
        .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        .is_err()
    {
        core::hint::spin_loop();
    }
}

fn unlock() {
    LOCK.store(false, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn rust_boot_banner() -> *const u8 {
    BANNER.as_ptr()
}

#[no_mangle]
pub extern "C" fn rust_history_push(bytes: *const u8, len: usize) {
    if bytes.is_null() || len == 0 {
        return;
    }

    let slice = unsafe { core::slice::from_raw_parts(bytes, len) };

    lock();
    unsafe {
        HISTORY.push(slice);
    }
    unlock();

    HISTORY_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn rust_history_count() -> u64 {
    HISTORY_COUNT.load(Ordering::Relaxed)
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
