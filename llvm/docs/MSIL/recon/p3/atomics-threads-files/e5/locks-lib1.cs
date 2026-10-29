public static class L1 { public static object Get(int i) { return string.Intern("msil.atomic.lock." + i.ToString()); } }
