using System;
static class P {
  static int Main(string[] a) {
    AppDomain.CurrentDomain.ProcessExit += (s, e) => Console.WriteLine("ProcessExit ran");
    switch (a[0]) {
      case "failfast": Environment.FailFast("msil: __fastfail"); break;
      case "throw": throw null;
      case "spin": for (int i = 0; i < 1000; i++) System.Threading.Thread.SpinWait(1); Console.WriteLine("spin ok"); break;
    }
    return 0;
  }
}
