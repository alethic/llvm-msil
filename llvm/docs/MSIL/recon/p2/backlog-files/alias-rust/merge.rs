#![no_std]
use core::hint::black_box;

#[inline(never)]
#[no_mangle]
pub extern "C" fn sum_a(p: *const u32, n: usize) -> u32 {
    let mut s = 0u32;
    for i in 0..n { s = s.wrapping_add(unsafe { *p.add(i) }.wrapping_mul(3)); }
    s
}
#[inline(never)]
#[no_mangle]
pub extern "C" fn sum_b(p: *const u32, n: usize) -> u32 {
    let mut s = 0u32;
    for i in 0..n { s = s.wrapping_add(unsafe { *p.add(i) }.wrapping_mul(3)); }
    s
}
#[inline(never)]
fn priv_a(x: u64) -> u64 { x.wrapping_mul(0x9E3779B97F4A7C15).rotate_left(17) ^ 0x1234 }
#[inline(never)]
fn priv_b(x: u64) -> u64 { x.wrapping_mul(0x9E3779B97F4A7C15).rotate_left(17) ^ 0x1234 }

#[no_mangle]
pub extern "C" fn entry(x: u64) -> u64 {
    let a = priv_a(black_box(x));
    let b = priv_b(black_box(x + 1));
    let v = [1u32, 2, 3, 4];
    let s = sum_a(v.as_ptr(), 4) as u64 + sum_b(black_box(v.as_ptr()), 4) as u64;
    a ^ b ^ s
}
#[no_mangle]
pub extern "C" fn div128(a: u128, b: u128) -> u128 { a / b }
#[no_mangle]
pub extern "C" fn f2i128(a: f64) -> i128 { a as i128 }

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! { loop {} }
