// A facade that forwards System.Object and Marshal to FakeCore.
.assembly extern FakeCore { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 0:0:0:0 }
.assembly FakeFacade { .ver 4:0:0:0 }
.class extern forwarder System.Object { .assembly extern FakeCore }
.class extern forwarder System.Runtime.InteropServices.Marshal { .assembly extern FakeCore }
