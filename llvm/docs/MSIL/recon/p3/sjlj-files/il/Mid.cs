using System;
public static class Mid {
  static bool Note(Exception e) { Console.WriteLine("  C# filter sees " + e.GetType().FullName); return false; }
  // A C# frame between the thrower and the IL filter: a filter that declines, then a finally.
  public static int CallThrough(IntPtr b, int kind) {
    try { return T.Deep(1, b, kind); }
    catch (Exception e) when (Note(e)) { return -1; }
    finally { Console.WriteLine("  C# finally ran"); }
  }
  // A C# frame that catches everything and rethrows with 'throw;'.
  public static int CatchRethrow(IntPtr b, int kind) {
    try { return T.Deep(1, b, kind); }
    catch (Exception e) { Console.WriteLine("  C# catch(Exception) got " + e.GetType().FullName + "; rethrow"); throw; }
  }
}
