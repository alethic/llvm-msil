use std::hint::black_box;
fn sum(p: &[u32]) -> u32 { p.iter().fold(0u32, |s, v| s.wrapping_add(v.wrapping_mul(3))) }
fn privf(x: u64) -> u64 { x.wrapping_mul(0x9E3779B97F4A7C15).rotate_left(17) ^ 0x1234 }
fn main() {
  let x = 5u64;
  let a = privf(black_box(x)); let b = privf(black_box(x + 1));
  let v = [1u32, 2, 3, 4];
  let e = a ^ b ^ (sum(&v) as u64 + sum(&v) as u64);
  let d: u128 = 1000000000000000000000000000007u128 / 97;
  let f: i128 = (-1.5e30f64) as i128;
  let y = (d ^ (f as u128)) ^ (e as u128);
  let z = y ^ (y >> 64);
  println!("{}", (z as u32) & 127);
}
