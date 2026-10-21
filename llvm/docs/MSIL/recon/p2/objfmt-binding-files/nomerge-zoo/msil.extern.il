.assembly 'msil.extern' { .ver 0:0:0:0 }
.assembly extern 'a' { .ver 0:0:0:0 }
.assembly extern 'b' { .ver 0:0:0:0 }
.assembly extern 'zoo4' { .ver 0:0:0:0 }
.class extern forwarder '<sym>??_C@_04GHJNJNPO@main?$AA@' { .assembly extern 'a' }
.class extern forwarder '<sym>??_C@_04JCHODAIH@add3?$AA@' { .assembly extern 'b' }
.class extern forwarder '<sym>??_C@_05CJBACGMB@hello?$AA@' { .assembly extern 'a' }
.class extern forwarder '<sym>add3' { .assembly extern 'b' }
.class extern forwarder '<sym>alias_twice' { .assembly extern 'b' }
.class extern forwarder '<sym>b_extra' { .assembly extern 'b' }
.class extern forwarder '<sym>common_c' { .assembly extern 'a' }
.class extern forwarder '<sym>counter' { .assembly extern 'b' }
.class extern forwarder '<sym>main' { .assembly extern 'a' }
.class extern forwarder '<sym>maybe' { .assembly extern 'zoo4' }
.class extern forwarder '<sym>msg' { .assembly extern 'a' }
.class extern forwarder '<sym>msg_b' { .assembly extern 'b' }
.class extern forwarder '<sym>pcounter' { .assembly extern 'a' }
.class extern forwarder '<sym>table' { .assembly extern 'a' }
.class extern forwarder '<sym>twice' { .assembly extern 'b' }
.class extern forwarder '<sym>weakfn' { .assembly extern 'b' }
